/*  =========================================================================
    fty_mc_server - Computation server implementation

    Copyright (C) 2016 - 2020 Eaton

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License along
    with this program; if not, write to the Free Software Foundation, Inc.,
    51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
    =========================================================================
*/

/// fty_mc_server - Computation server implementation

#include "fty_mc_server.h"
#include "cmstats.h"
#include "cmsteps.h"
#include <fty_log.h>
#include <fty_shm.h>
#include <malamute.h>
#include <cmath>
#include <mutex>

static std::mutex g_cm_mutex;

struct cm_t
{
    char*         name;     // server name
    cmstats_t*    stats;    // computed statictics for all types and steps
    cmsteps_t*    steps;    // info about supported steps
    zlist_t*      types;    // info about supported statistic types (min, max, ...)
    mlm_client_t* client;   // malamute client
    char*         filename; // state file name
};

/// Destroy the "CM" entity
void cm_destroy(cm_t** self_p)
{
    if (self_p && *self_p) {
        cm_t* self = *self_p;

        // free structure items
        mlm_client_destroy(&self->client);
        zlist_destroy(&self->types);
        cmsteps_destroy(&self->steps);
        cmstats_destroy(&self->stats);
        zstr_free(&self->name);
        zstr_free(&self->filename);

        // free structure itself
        free(self);
        *self_p = nullptr;
    }
}

/// Create new empty not verbose "CM" entity
cm_t* cm_new(const char* name)
{
    assert(name);
    cm_t* self = reinterpret_cast<cm_t*>(zmalloc(sizeof(*self)));
    if (self) {
        memset(self, 0, sizeof(*self));
        self->name = strdup(name);
        if (self->name)
            self->stats = cmstats_new();
        if (self->stats)
            self->steps = cmsteps_new();
        if (self->steps)
            self->types = zlist_new();
        if (self->types) {
            zlist_autofree(self->types);
            self->client = mlm_client_new();
        }
        if (self->client) {
            return self;
        }
    }
    cm_destroy(&self);
    return NULL;
}

void s_handle_metric(fty_proto_t* bmsg, cm_t* self, bool shm)
{
    // get rid of messages with empty or null name
    if (fty_proto_name(bmsg) == nullptr || streq(fty_proto_name(bmsg), "")) {
        if (shm) {
            log_warning("%s: invalid \'name\' = (%s), \tfrom shm", self->name,
                fty_proto_name(bmsg) ? fty_proto_name(bmsg) : "null");
        } else {
            log_warning("%s: invalid \'name\' = (%s), \tsubject=%s, sender=%s", self->name,
                fty_proto_name(bmsg) ? fty_proto_name(bmsg) : "null", mlm_client_subject(self->client),
                mlm_client_sender(self->client));
        }
        return;
    }

    // PQSWMBT-3723: do not compute/agregate min/max/mean + average metrics for sensor temp. and humidity
    // as: 'temperature.default@sensor-xxx', 'humidity.default@sensor-xxx'
    {
        const char* name = fty_proto_name(bmsg);
        const char* type = fty_proto_type(bmsg);              // aka quantity
        if (name && type && (strstr(name, "sensor-") == name) // starts with
            && (streq(type, "temperature.default") || streq(type, "humidity.default"))) {
            log_trace("%s: %s@%s metric excluded from computation", self->name, type, name);
            return;
        }
    } // end PQSWMBT-3723

    // sometimes we do have nan in values, report if we get something like that on METRICS
    double value = atof(fty_proto_value(bmsg));
    if (std::isnan(value)) {
        if (shm) {
            log_warning("%s: isnan ('%s') from shm", self->name, fty_proto_value(bmsg));
        } else {
            log_warning("%s: isnan ('%lf'), subject='%s', sender='%s'", self->name, value,
                mlm_client_subject(self->client), mlm_client_sender(self->client));
        }
        return;
    }

    log_debug("%s: handle %s@%s (shm: %s)", self->name, fty_proto_type(bmsg), fty_proto_name(bmsg), (shm ? "true" : "false"));

    const char *quantity = fty_proto_type(bmsg);
    for (uint32_t* step_p = cmsteps_first(self->steps); step_p != nullptr; step_p = cmsteps_next(self->steps))
    {
        const char* step = reinterpret_cast<const char*>(cmsteps_cursor(self->steps));

        for (const char* type = reinterpret_cast<const char*>(zlist_first(self->types));
             type != nullptr;
             type = reinterpret_cast<const char*>(zlist_next(self->types))
        ) {
            // If consumption calculation, filter data which is not realpower
            if (type && quantity && streq(type, "consumption") && !streq(quantity, "realpower.default")) {
                continue;
            }
            fty_proto_t* stat_msg = cmstats_put(self->stats, type, step, *step_p, bmsg);
            if (stat_msg) {
                log_debug("%s: publish %s@%s", self->name, fty_proto_type(stat_msg), fty_proto_name(stat_msg));
                int r = fty::shm::write_metric(stat_msg);
                if (r == -1) {
                    log_error("%s: publish %s@%s failed", self->name, fty_proto_type(stat_msg), fty_proto_name(stat_msg));
                }
                fty_proto_destroy(&stat_msg);
            }
        }
    }
}


void fty_metric_compute_metric_pull(zsock_t* pipe, void* args)
{
    cm_t* self = reinterpret_cast<cm_t*>(args);
    if (!self) {
        log_fatal("args == NULL");
        return;
    }

    zpoller_t* poller = zpoller_new(pipe, nullptr);
    zsock_signal(pipe, 0);

    while (!zsys_interrupted) {
        uint64_t timeout = uint64_t(fty_get_polling_interval() * 1000);
        void* which = zpoller_wait(poller, int(timeout));

        if (which == nullptr) {
            if (zpoller_terminated(poller) || zsys_interrupted) {
                break;
            }
            if (zpoller_expired(poller)) {
                // All metrics realpower.default or temperature, or humidity
                // who are not already produced by metric compute
                static const std::string metrics_pattern =
                    "(^realpower\\.default"
                    "|^power\\.default"
                    "|current\\.(output|input)\\.L(1|2|3)"
                    "|voltage\\.(output|input)\\.L(1|2|3)-N"
                    "|voltage\\.input\\.(1|2)"  // For ATS only
                    "|.*temperature"
                    "|.*humidity)"
                    "((?!_arithmetic_mean|_max_|_min_|_consumption_).)*";

                fty::shm::shmMetrics metrics;
                fty::shm::read_metrics(".*", metrics_pattern, metrics);
                log_debug("poll metrics (size: %zu)", metrics.size());
                for (auto& metric : metrics) {
                    g_cm_mutex.lock();
                    s_handle_metric(metric, self, true);
                    g_cm_mutex.unlock();
                }
            }
        }
        else if (which == pipe) {
            zmsg_t* message = zmsg_recv(pipe);
            if (message) {
                char* cmd = zmsg_popstr(message);
                if (cmd) {
                    if (streq(cmd, "$TERM")) {
                        zstr_free(&cmd);
                        zmsg_destroy(&message);
                        break;
                    }
                    zstr_free(&cmd);
                }
                zmsg_destroy(&message);
            }
        }
    }

    zpoller_destroy(&poller);
}

//  --------------------------------------------------------------------------
//  fty_mc_server actor

void fty_mc_server(zsock_t* pipe, void* args)
{
    cm_t* self = cm_new(reinterpret_cast<const char*>(args));
    if (!self) {
        log_fatal("cm_new failed");
        return;
    }

    zpoller_t* poller = zpoller_new(pipe, mlm_client_msgpipe(self->client), nullptr);
    assert(poller);
    zsock_signal(pipe, 0);

    zactor_t* metric_pull = nullptr;

    // Time in [ms] when last cmstats_poll was called
    // -1 means it was never called yet
    int64_t last_poll_ms = -1;
    while (!zsys_interrupted) {
        // What time left before publishing?
        // If steps where not defined ( cmsteps_gcd == 0 ) then nothing to publish,
        // so, we can wait forever (-1) for first message to come
        // in [ms]
        g_cm_mutex.lock();
        int interval_ms = -1;
        if (cmsteps_gcd(self->steps) != 0) {
            // So, some steps where defined

            // What is the "now" time in [s]
            int64_t now_s = zclock_time() / 1000;

            // Compute the left border of the interval:
            // length_of_the_minimal_interval - part_of_interval_already_passed
            interval_ms = int(cmsteps_gcd(self->steps) - (now_s % cmsteps_gcd(self->steps))) * 1000;

            log_debug("%s: now=%" PRIu64 "s, cmsteps_gcd=%" PRIu32 "s, interval=%dms", self->name, now_s,
                cmsteps_gcd(self->steps), interval_ms);
        }
        g_cm_mutex.unlock();

        // wait for interval left
        void* which = zpoller_wait(poller, interval_ms);

        if (!which && zpoller_terminated(poller))
            break;

        // poll when zpoller expired
        // the second condition necessary
        //
        //  X2 is an expected moment when some metrics should be published
        //  in t=X1 message comes, its processing takes time and cycle will begin from the beginning
        //  at moment X4, but in X3 some message had already come -> so zpoller_expired(poller) == false
        //
        // -NOW----X1------X2---X3--X4-----

        g_cm_mutex.lock();

        if ((!which && zpoller_expired(poller)) ||
            (last_poll_ms > 0 && ((zclock_time() - last_poll_ms) > cmsteps_gcd(self->steps) * 1000))) {

            if (zpoller_expired(poller)) {
                log_debug("%s: zpoller_expired, calling cmstats_poll", self->name);
            } else {
                log_debug("%s: time (not zpoller) expired, calling cmstats_poll", self->name);
            }

            // Publish metrics and reset the computation where needed
            cmstats_poll(self->stats);
            // State is saved every time, when something is published
            // Something is published every "steps_gcd" interval
            // In the most of the cases (steps_gcd = "minimal_interval")
            if (self->filename) {
                int r = cmstats_save(self->stats, self->filename);
                if (r == -1) {
                    log_error("%s: Failed to save %s: %s", self->name, self->filename, strerror(errno));
                } else {
                    log_info("%s: '%s' saved succesfully", self->name, self->filename);
                }
            }
            // Record the time, when something was published last time
            last_poll_ms = zclock_time();
            // if poller expired, we can continue in order to wait for new message
            if (!which && zpoller_expired(poller)) {
                g_cm_mutex.unlock();
                continue;
            }
            // else if poller not expired, we need to process the message!!
        }

        if (which == pipe) {
            zmsg_t* msg     = zmsg_recv(pipe);
            char*   command = zmsg_popstr(msg);

            log_debug("%s: command=%s", self->name, command);

            if (streq(command, "$TERM")) {
                g_cm_mutex.unlock();
                log_info("Got $TERM");
                zstr_free(&command);
                zmsg_destroy(&msg);
                break;
            }
            else if (streq(command, "DIR")) {
                char*    dir   = zmsg_popstr(msg);
                zfile_t* f     = zfile_new(dir, "state.zpl");
                zstr_free(&self->filename);
                self->filename = strdup(zfile_filename(f, nullptr));

                if (zfile_exists(self->filename)) {
                    cmstats_t* cms = cmstats_load(self->filename);
                    if (!cms) {
                        log_error("%s: Failed to load '%s'", self->name, self->filename);
                    } else {
                        log_info("%s: Loaded '%s'", self->name, self->filename);
                        cmstats_destroy(&self->stats);
                        self->stats = cms;
                    }
                } else {
                    log_info("%s: State file '%s' doesn't exists", self->name, self->filename);
                }

                zfile_destroy(&f);
                zstr_free(&dir);
            }
            else if (streq(command, "PRODUCER")) {
                char* stream = zmsg_popstr(msg);
                int   r      = mlm_client_set_producer(self->client, stream);
                if (r == -1) {
                    log_error("%s: Can't set producer on stream '%s'", self->name, stream);
                }
                zstr_free(&stream);
            }
            else if (streq(command, "CONSUMER")) {
                char* stream  = zmsg_popstr(msg);
                char* pattern = zmsg_popstr(msg);
                int   rv      = mlm_client_set_consumer(self->client, stream, pattern);
                if (rv == -1) {
                    log_error("%s: Can't set consumer on stream '%s', '%s'", self->name, stream, pattern);
                }
                zstr_free(&pattern);
                zstr_free(&stream);
            }
            else if (streq(command, "CREATE_PULL")) {
                metric_pull = zactor_new(fty_metric_compute_metric_pull, self);
            }
            else if (streq(command, "CONNECT")) {
                char* endpoint = zmsg_popstr(msg);
                if (!endpoint) {
                    log_error("%s: Missing endpoint", self->name);
                } else {
                    int r = mlm_client_connect(self->client, endpoint, 5000, self->name);
                    if (r == -1) {
                        log_error("%s: Connection to endpoint '%s' failed", self->name, endpoint);
                    }
                }
                zstr_free(&endpoint);
            }
            else if (streq(command, "STEPS")) {
                for (;;) {
                    char* step = zmsg_popstr(msg);
                    if (!step)
                        break;
                    int r = cmsteps_put(self->steps, step);
                    if (r == -1) {
                        log_info("%s: Ignoring unrecognized step='%s'", self->name, step);
                    }
                    zstr_free(&step);
                }
            }
            else if (streq(command, "TYPES")) {
                for (;;) {
                    char* type = zmsg_popstr(msg);
                    // TODO: may be we need some check here for supported types
                    if (!type)
                        break;
                    zlist_append(self->types, type);
                    zstr_free(&type);
                }
            }
            else {
                log_warning("%s: Unkown command=%s, ignoring", self->name, command);
            }

            zstr_free(&command);
            zmsg_destroy(&msg);
            g_cm_mutex.unlock();
            continue;
        } // end of comand pipe processing

        zmsg_t* msg = mlm_client_recv(self->client);
        if (!msg) {
            log_error("%s: mlm_client_recv() == nullptr", self->name);
            g_cm_mutex.unlock();
            continue;
        }

        // ignore linuxmetrics
        if (streq(mlm_client_sender(self->client), "fty_info_linuxmetrics")) {
            zmsg_destroy(&msg);
            g_cm_mutex.unlock();
            continue;
        }

        fty_proto_t* bmsg = fty_proto_decode(&msg);
        zmsg_destroy(&msg);

        // If we received an asset message
        // * "delete", "retire" or non active asset  -> drop all computations on that asset
        // *  other                -> ignore it, as it doesn't impact this agent
        if (fty_proto_id(bmsg) == FTY_PROTO_ASSET) {
            const char* op = fty_proto_operation(bmsg);
            if (streq(op, "delete")
                || streq(op, "retire")
                || !streq(fty_proto_aux_string(bmsg, FTY_PROTO_ASSET_STATUS, "active"), "active")) {
                cmstats_delete_asset(self->stats, fty_proto_name(bmsg));
            }

            fty_proto_destroy(&bmsg);
            g_cm_mutex.unlock();
            continue;
        }

        // If we received a metric message
        // update statistics for all steps and types
        // All statistics are computed for "left side of the interval"
        if (fty_proto_id(bmsg) == FTY_PROTO_METRIC) {
            s_handle_metric(bmsg, self, false);
            fty_proto_destroy(&bmsg);
            g_cm_mutex.unlock();
            continue;
        }

        // We received some unexpected message
        log_warning("%s: Unexpected message from sender=%s, subject=%s", self->name, mlm_client_sender(self->client),
            mlm_client_subject(self->client));

        fty_proto_destroy(&bmsg);
        g_cm_mutex.unlock();
    }
    // end of main loop, so we are going to die soon

    g_cm_mutex.lock();
    if (self->filename) {
        int r = cmstats_save(self->stats, self->filename);
        if (r == -1)
            log_error("%s: Failed to save '%s': %s", self->name, self->filename, strerror(errno));
        else
            log_info("%s: Saved succesfully '%s'", self->name, self->filename);
    }
    g_cm_mutex.unlock();

    zactor_destroy(&metric_pull);
    zpoller_destroy(&poller);
    cm_destroy(&self);
}
