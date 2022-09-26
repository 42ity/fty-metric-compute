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
    zlist_t*      types;    // info about supported statistic types (min, max, average, consumption, ...)
    mlm_client_t* client;   // malamute client
    char*         filename; // state file name
};

/// Destroy the "CM" entity
static void cm_destroy(cm_t** self_p)
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
        *self_p = NULL;
    }
}

/// Create new empty not verbose "CM" entity
static cm_t* cm_new(const char* name)
{
    if (!name) {
        log_error("name is NULL");
        return NULL;
    }

    cm_t* self = reinterpret_cast<cm_t*>(zmalloc(sizeof(*self)));
    do {
        if (!self) break;
        memset(self, 0, sizeof(*self));

        self->name = strdup(name);
        if (!self->name) break;
        self->client = mlm_client_new();
        if (!self->client) break;
        self->stats = cmstats_new();
        if (!self->stats) break;
        self->steps = cmsteps_new();
        if (!self->steps) break;
        self->types = zlist_new();
        if (!self->types) break;

        zlist_autofree(self->types);

        return self;
    } while(0);

    cm_destroy(&self);
    return NULL;
}

static void s_handle_metric(cm_t* self, fty_proto_t* bmsg)
{
    if (!(bmsg && (fty_proto_id(bmsg) == FTY_PROTO_METRIC))) {
        log_debug("bmsg is not a METRIC");
        return;
    }

    const char* quantity = fty_proto_type(bmsg);
    const char* name = fty_proto_name(bmsg); // asset name
    const char* value = fty_proto_value(bmsg);

    // quick checks
    try {
        if (!(quantity && (*quantity))) {
            throw std::runtime_error("quantity is Null/Empty");
        }
        if (!(name && (*name))) {
            throw std::runtime_error("name is Null/Empty");
        }
        if (!(value && (*value))) {
            throw std::runtime_error("value is Null/Empty");
        }
        double fvalue = atof(value);
        if (std::isnan(fvalue)) {
            throw std::runtime_error("value is NaN");
        }
    }
    catch (const std::exception& e) {
        log_error("%s: %s@%s (value: %s) is invalid: %s",
            self->name, quantity, name, value, e.what());
        return;
    }

    // PQSWMBT-3723: do not compute/agregate min/max/mean + average metrics for sensor temp. and humidity
    // as: 'temperature.default@sensor-xxx', 'humidity.default@sensor-xxx'
    {
        if ((strstr(name, "sensor-") == name) // starts with
            && (streq(quantity, "temperature.default") || streq(quantity, "humidity.default"))
        ) {
            log_trace("%s: %s@%s metric excluded from computation", self->name, quantity, name);
            return;
        }
    } // end PQSWMBT-3723

    log_debug("%s: handle %s@%s (value: %s)", self->name, quantity, name, value);

    for (uint32_t* step_p = cmsteps_first(self->steps); step_p; step_p = cmsteps_next(self->steps))
    {
        const char* str_step = reinterpret_cast<const char*>(cmsteps_cursor(self->steps));
        const uint32_t step = *step_p;

        for (void* it_type = zlist_first(self->types); it_type; it_type = zlist_next(self->types))
        {
            const char* type = reinterpret_cast<const char*>(it_type);

            // If consumption calculation, filter data which is not realpower
            if (type && streq(type, "consumption") && !streq(quantity, "realpower.default")) {
                continue;
            }

            fty_proto_t* stat_msg = cmstats_put(self->stats, type, str_step, step, bmsg);

            if (stat_msg) {
                log_debug("%s: publish %s@%s", self->name, fty_proto_type(stat_msg), fty_proto_name(stat_msg));
                int r = fty::shm::write_metric(stat_msg);
                if (r != 0) {
                    log_error("%s: publish %s@%s failed", self->name, fty_proto_type(stat_msg), fty_proto_name(stat_msg));
                }
                fty_proto_destroy(&stat_msg);
            }
        }
    }
}

static void s_handle_all_metrics(cm_t* self)
{
    // All metrics realpower.default, temperature, humidity, ...
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
    log_debug("pull metrics (size: %zu)", metrics.size());

    for (auto& metric : metrics) {
        s_handle_metric(self, metric);
    }
}

static void s_pull_metrics(zsock_t* pipe, void* args)
{
    cm_t* self = reinterpret_cast<cm_t*>(args);
    if (!self) {
        log_error("args is NULL");
        return;
    }

    zpoller_t* poller = zpoller_new(pipe, NULL);
    if (!poller) {
        log_error("poller new failed");
        return;
    }

    log_info("pull_metrics actor started");

    zsock_signal(pipe, 0);

    while (!zsys_interrupted)
    {
        int timeout = fty_get_polling_interval() * 1000; //ms
        void* which = zpoller_wait(poller, timeout);

        if (which == NULL) {
            if (zpoller_terminated(poller) || zsys_interrupted) {
                break; //$TERM
            }

            if (zpoller_expired(poller)) {
                g_cm_mutex.lock();
                s_handle_all_metrics(self);
                g_cm_mutex.unlock();
            }
        }
        else if (which == pipe) {
            zmsg_t* msg = zmsg_recv(pipe);
            char* cmd = msg ? zmsg_popstr(msg) : NULL;
            bool term = (cmd && streq(cmd, "$TERM"));
            zstr_free(&cmd);
            zmsg_destroy(&msg);
            if (term) {
                break;
            }
        }
    }

    zpoller_destroy(&poller);

    log_info("pull_metrics actor ended");
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

    zpoller_t* poller = zpoller_new(pipe, mlm_client_msgpipe(self->client), NULL);
    if (!poller) {
        log_fatal("zpoller_new failed");
        cm_destroy(&self);
        return;
    }

    log_info("%s: actor started", self->name);

    zsock_signal(pipe, 0);

    zactor_t* pull_metrics = NULL;

    // Time in [ms] when last cmstats_poll was called
    // -1 means it was never called yet
    int64_t last_poll_ms = -1;

    while (!zsys_interrupted) {
        // What time left before publishing?
        // If steps where not defined ( cmsteps_gcd == 0 ) then nothing to publish,
        // so, we can wait forever (-1) for first message to come
        // in [ms]
        int interval_ms = -1;
        {
            g_cm_mutex.lock();
            uint32_t gcd_s = cmsteps_gcd(self->steps);
            g_cm_mutex.unlock();

            if (gcd_s != 0) { // some steps where defined
                int64_t now_s = zclock_time() / 1000;

                // Compute the remaining (right) border of the interval:
                // length_of_the_minimal_interval - part_of_interval_already_passed
                interval_ms = int(gcd_s - (now_s % gcd_s)) * 1000;

                log_debug("%s: cmsteps_gcd=%" PRIu32 "s, interval=%dms",
                    self->name, gcd_s, interval_ms);
            }
        }

        // wait for interval left
        void* which = zpoller_wait(poller, interval_ms);

        if (which == NULL) {
            if (zpoller_terminated(poller) || zsys_interrupted) {
                break; //$TERM
            }
        }

        g_cm_mutex.lock();

        {
            // poll when zpoller expired
            // the second condition necessary
            //
            //  X2 is an expected moment when some metrics should be published
            //  in t=X1 message comes, its processing takes time and cycle will begin from the beginning
            //  at moment X4, but in X3 some message had already come -> so zpoller_expired(poller) == false
            //
            // -NOW----X1------X2---X3--X4-----

            bool poller_expired = ((which == NULL) && zpoller_expired(poller));

            bool time_expired = false;
            if (!poller_expired && (last_poll_ms > 0)) {
                int64_t now_ms = zclock_time();
                uint32_t gcd_s = cmsteps_gcd(self->steps);
                if ((now_ms - last_poll_ms) >= (gcd_s * 1000)) {
                    time_expired = true;
                }
            }

            if (poller_expired || time_expired) {
                log_debug("%s: cmstats_poll due to %s",
                    self->name, (poller_expired ? "poller expired" : "time expired"));

                // Record the poll time
                last_poll_ms = zclock_time();

                // Publish metrics and reset the computation where needed
                cmstats_poll(self->stats);

                log_info("%s: cmstats_poll at %zu s. (lap time: %zu ms.)",
                    self->name, (last_poll_ms / 1000), (zclock_time() - last_poll_ms));

                // State is saved every steps_gcd time, when something is published
                if (self->filename) {
                    int r = cmstats_save(self->stats, self->filename);
                    if (r != 0) {
                        log_error("%s: Failed to save '%s' (r: %d, %s)",
                            self->name, self->filename, r, strerror(errno));
                    }
                    else {
                        log_info("%s: Saved succesfully '%s'",
                            self->name, self->filename);
                    }
                }

                // process message if any
            }
        }

        bool term{false};

        if (which == pipe) {
            zmsg_t* msg = zmsg_recv(pipe);
            char* command = zmsg_popstr(msg);

            log_debug("%s: command=%s", self->name, command);

            if (streq(command, "$TERM")) {
                term = true;
            }
            else if (streq(command, "DIR")) {
                char* path = zmsg_popstr(msg);
                zfile_t* file = zfile_new(path, "state.zpl");
                zstr_free(&self->filename);
                self->filename = strdup(zfile_filename(file, NULL));
                zfile_destroy(&file);
                zstr_free(&path);

                log_info("%s: State file='%s'", self->name, self->filename);

                if (zfile_exists(self->filename)) {
                    cmstats_t* cms = cmstats_load(self->filename);
                    if (!cms) {
                        log_error("%s: Failed to load '%s'", self->name, self->filename);
                    }
                    else {
                        log_info("%s: State file '%s' loaded", self->name, self->filename);
                        cmstats_destroy(&self->stats);
                        self->stats = cms;
                    }
                }
                else {
                    log_info("%s: State file '%s' doesn't exists", self->name, self->filename);
                }
            }
            else if (streq(command, "CONSUMER")) {
                char* stream  = zmsg_popstr(msg);
                char* pattern = zmsg_popstr(msg);
                int r = -1;
                if (stream && pattern) {
                    r = mlm_client_set_consumer(self->client, stream, pattern);
                }
                if (r < 0) {
                    log_error("%s: Failed to set consumer on %s/%s", self->name, stream, pattern);
                }
                else {
                    log_info("%s: Set consumer on %s/%s", self->name, stream, pattern);
                }
                zstr_free(&pattern);
                zstr_free(&stream);
            }
            else if (streq(command, "CONNECT")) {
                char* endpoint = zmsg_popstr(msg);
                if (!endpoint) {
                    log_error("%s: Missing endpoint", self->name);
                }
                else {
                    int r = mlm_client_connect(self->client, endpoint, 5000, self->name);
                    if (r == -1) {
                        log_error("%s: Connection to endpoint '%s' failed", self->name, endpoint);
                    }
                }
                zstr_free(&endpoint);
            }
            else if (streq(command, "STEPS")) {
                char* step = zmsg_popstr(msg);
                while (step) {
                    log_info("%s: +STEP='%s'", self->name, step);
                    int r = cmsteps_put(self->steps, step);
                    if (r != 0) {
                        log_warning("%s: Ignoring unrecognized step='%s' (r: %d)", self->name, step, r);
                    }
                    zstr_free(&step);
                    step = zmsg_popstr(msg);
                }
                log_info("%s: cmsteps_gcd=%ds", self->name, cmsteps_gcd(self->steps));
            }
            else if (streq(command, "TYPES")) {
                char* type = zmsg_popstr(msg);
                while (type) {
                    log_info("%s: +TYPE='%s'", self->name, type);
                    zlist_append(self->types, type);
                    zstr_free(&type);
                    type = zmsg_popstr(msg);
                }
            }
            else if (streq(command, "CREATE_PULL")) {
                zactor_destroy(&pull_metrics);
                pull_metrics = zactor_new(s_pull_metrics, self);
                if (!pull_metrics) {
                    log_error("%s: Failed to create pull_metrics actor", self->name);
                }
            }
            else if (streq(command, "UNITTEST_SYNC_METRICS")) {
                // UT specific, sync our cmstats with the SharedMemory
                s_handle_all_metrics(self);
            }
            else {
                log_warning("%s: Unkown command=%s", self->name, command);
            }

            zstr_free(&command);
            zmsg_destroy(&msg);
        }
        else if (which == mlm_client_msgpipe(self->client))
        {
            zmsg_t* msg = mlm_client_recv(self->client);
            if (fty_proto_is(msg)) {
                fty_proto_t* proto = fty_proto_decode(&msg);
                if (proto && (fty_proto_id(proto) == FTY_PROTO_ASSET)) {
                    // we received an asset message
                    // * "delete", "retire" or non active asset -> drop all computations on that asset
                    // * other -> ignore it, as it doesn't impact this agent
                    const char* operation = fty_proto_operation(proto);
                    if (streq(operation, FTY_PROTO_ASSET_OP_DELETE)
                        || streq(operation, FTY_PROTO_ASSET_OP_RETIRE)
                        || !streq(fty_proto_aux_string(proto, FTY_PROTO_ASSET_STATUS, "active"), "active")
                    ) {
                        cmstats_delete_asset(self->stats, fty_proto_name(proto));
                    }
                }
                fty_proto_destroy(&proto);
            }
            zmsg_destroy(&msg);
        }

        g_cm_mutex.unlock();

        if (term) {
            break;
        }
    }

    log_info("%s: actor ended", self->name);

    // save stats on exit
    if (self->filename) {
        g_cm_mutex.lock();
        int r = cmstats_save(self->stats, self->filename);
        g_cm_mutex.unlock();

        if (r != 0) {
            log_error("%s: Failed to save '%s' (r: %d, %s)", self->name, self->filename, r, strerror(errno));
        }
        else {
            log_info("%s: Saved succesfully '%s'", self->name, self->filename);
        }
    }

    zactor_destroy(&pull_metrics);
    zpoller_destroy(&poller);
    cm_destroy(&self);
}
