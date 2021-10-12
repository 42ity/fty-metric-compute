/*  =========================================================================
    cmstats - Computing the stats on metrics

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

/// cmstats - Computing the stats on metrics

#include "cmstats.h"
#include "fty_mc_server.h"
#include <cmath>
#include <fty_log.h>
#include <fty_proto.h>
#include <fty_shm.h>
#include <ctime>

typedef void(compute_fn)(const fty_proto_t* bmsg, fty_proto_t* stat_msg);

static void s_destructor(void** self_p)
{
    fty_proto_destroy(reinterpret_cast<fty_proto_t**>(self_p));
}

static void* s_duplicator(const void* self)
{
    return fty_proto_dup(const_cast<fty_proto_t*>(reinterpret_cast<const fty_proto_t*>(self)));
}

// find minimum value
// \param bmsg - input new metric
// \param stat_msg - output statistic metric
static bool s_min(const fty_proto_t* bmsg, fty_proto_t* stat_msg)
{
    assert(bmsg);
    assert(stat_msg);
    double   bmsg_value = atof(fty_proto_value(const_cast<fty_proto_t*>(bmsg)));
    uint64_t count      = fty_proto_aux_number(stat_msg, AGENT_CM_COUNT, 0);
    double   stat_value = atof(fty_proto_value(stat_msg));

    if (std::isnan(stat_value) || count == 0 || (bmsg_value < stat_value)) {
        fty_proto_set_value(stat_msg, "%.2f", bmsg_value);
    }

    return true;
}

// find maximum value
// \param bmsg - input new metric
// \param stat_msg - output statistic metric
static bool s_max(const fty_proto_t* bmsg, fty_proto_t* stat_msg)
{
    assert(bmsg);
    assert(stat_msg);
    double   bmsg_value = atof(fty_proto_value(const_cast<fty_proto_t*>(bmsg)));
    uint64_t count      = fty_proto_aux_number(stat_msg, AGENT_CM_COUNT, 0);
    double   stat_value = atof(fty_proto_value(stat_msg));

    if (std::isnan(stat_value) || count == 0 || (bmsg_value > stat_value)) {
        fty_proto_set_value(stat_msg, "%.2f", bmsg_value);
    }

    return true;
}

// find average value
// \param bmsg - input new metric
// \param stat_msg - output statistic metric
static bool s_arithmetic_mean(const fty_proto_t* bmsg, fty_proto_t* stat_msg)
{
    assert(bmsg);
    assert(stat_msg);
    double   value = atof(fty_proto_value(const_cast<fty_proto_t*>(bmsg)));
    uint64_t count = fty_proto_aux_number(stat_msg, AGENT_CM_COUNT, 0);
    double   sum   = atof(fty_proto_aux_string(stat_msg, AGENT_CM_SUM, "0"));

    if (std::isnan(value) || std::isnan(sum)) {
        log_warning("s_arithmetic_mean: isnan value(%s) or sum (%s) for %s@%s, skipping",
            fty_proto_value(const_cast<fty_proto_t*>(bmsg)), fty_proto_aux_string(stat_msg, AGENT_CM_SUM, "0"),
            fty_proto_type(const_cast<fty_proto_t*>(bmsg)), fty_proto_name(const_cast<fty_proto_t*>(bmsg)));
        return false;
    }

    // 0 means that we have first value
    if (count == 0)
        sum = value;
    else
        sum += value;

    double avg = (sum / double(count + 1));
    if (std::isnan(avg)) {
        log_error("s_arithmetic_mean: isnan (avg) %f / (%" PRIu64 " + 1), for %s@%s, skipping", sum, count,
            fty_proto_type(const_cast<fty_proto_t*>(bmsg)), fty_proto_name(const_cast<fty_proto_t*>(bmsg)));
        return false;
    }

    // Sample was accepted
    fty_proto_aux_insert(stat_msg, AGENT_CM_SUM, "%f", sum);
    fty_proto_set_value(stat_msg, "%.2f", avg);
    return true;
}

// transform timestamp to readable string format
// \param tm_s - input timestamp in sec
std::string getTimeStampStr(const uint64_t tm_s) {
    std::string res;
    char buffer[32];
    std::strftime(buffer, sizeof(buffer), "%d/%m/%Y-%H:%M:%S",
        std::localtime(reinterpret_cast<const long int*>(&tm_s)));
    res = buffer;
    return res;
}

// compute consumption value
// \param bmsg - input new metric
// \param stat_msg - output statistic metric
static bool s_consumption(const fty_proto_t* bmsg, fty_proto_t* stat_msg)
{
    assert(bmsg);
    assert(stat_msg);

    double value = atof(fty_proto_value(const_cast<fty_proto_t*>(bmsg)));

    if (std::isnan(value)) {
        log_warning("s_consumption: isnan value(%s) for %s@%s, skipping",
            fty_proto_value(const_cast<fty_proto_t*>(bmsg)), fty_proto_type(const_cast<fty_proto_t*>(bmsg)),
            fty_proto_name(const_cast<fty_proto_t*>(bmsg)));
        return false;
    }

    // Compute value for the current interval
    double consumption = atof(fty_proto_value(stat_msg));
    uint64_t now_s = uint64_t(zclock_time()/1000);
    uint64_t last_metric_time_s = fty_proto_aux_number(stat_msg, AGENT_CM_LASTTS, 0);
    // Get last power received which is saved in the sum aux dictionary
    double last_metric_value = atof(fty_proto_aux_string(stat_msg, AGENT_CM_SUM, ""));
    // Save new value in the sum aux dictionary
    fty_proto_aux_insert(stat_msg, AGENT_CM_SUM, "%s", fty_proto_value(const_cast<fty_proto_t*>(bmsg)));
    double inc = last_metric_value * static_cast<double>(now_s - last_metric_time_s);
    if (inc > 0) consumption += inc;
    log_debug("s_consumption: update consumption %s: %.1f (inc=%.1f) %" PRIu64"(%s)-%" PRIu64"(%s) %" PRIu64,
        fty_proto_name(const_cast<fty_proto_t*>(bmsg)), consumption, inc, now_s, getTimeStampStr(now_s).c_str(),
        last_metric_time_s, getTimeStampStr(last_metric_time_s).c_str(), now_s - last_metric_time_s);
    // Sample was accepted
    fty_proto_set_value(stat_msg, "%.1f", consumption);
    fty_proto_aux_insert(stat_msg, AGENT_CM_LASTTS, "%" PRIu64, now_s);
    return true;
}

//  --------------------------------------------------------------------------
//  Create a new cmstats

cmstats_t* cmstats_new(void)
{
    cmstats_t* self = reinterpret_cast<cmstats_t*>(zmalloc(sizeof(cmstats_t)));
    assert(self);
    //  Initialize class properties here
    self->stats = zhashx_new();
    assert(self->stats);
    zhashx_set_destructor(self->stats, s_destructor);
    zhashx_set_duplicator(self->stats, s_duplicator);

    return self;
}


//  --------------------------------------------------------------------------
//  Destroy the cmstats

void cmstats_destroy(cmstats_t** self_p)
{
    assert(self_p);
    if (*self_p) {
        cmstats_t* self = *self_p;
        //  Free class properties here
        zhashx_destroy(&self->stats);
        //  Free object itself
        free(self);
        *self_p = nullptr;
    }
}

//  --------------------------------------------------------------------------
//  Print the cmstats

void cmstats_print(cmstats_t* self)
{
    assert(self);
    for (void* it = zhashx_first(self->stats); it != nullptr; it = zhashx_next(self->stats)) {
        log_debug("%s =>", reinterpret_cast<const char*>(const_cast<void*>(zhashx_cursor(self->stats))));
        fty_proto_print(reinterpret_cast<fty_proto_t*>(it));
    }
}

//  --------------------------------------------------------------------------
// Update statistics with "aggr_fun" and "step" for the incomming message "bmsg"

fty_proto_t* cmstats_put(cmstats_t* self, const char* addr_fun, const char* sstep, uint32_t step, fty_proto_t* bmsg)
{
    assert(self);
    assert(addr_fun);
    assert(bmsg);

    uint64_t now_ms = uint64_t(zclock_time());
    // round the now to earliest time start
    // ie for 12:16:29 / step 15*60 return 12:15:00
    //    for 12:16:29 / step 60*60 return 12:00:00
    //    ... etc
    // works well for any value of step
    uint64_t metric_time_new_s = (now_ms - (now_ms % (step * 1000))) / 1000;

    char* key;
    int   r = asprintf(&key, "%s_%s_%s@%s", fty_proto_type(bmsg), addr_fun, sstep, fty_proto_name(bmsg));
    assert(r != -1); // make gcc @ rhel happy
    std::string skey(key);
    zstr_free(&key);

    fty_proto_t* stat_msg = reinterpret_cast<fty_proto_t*>(zhashx_lookup(self->stats, skey.c_str()));

    // handle the first insert
    if (!stat_msg) {
        stat_msg = fty_proto_dup(bmsg);
        fty_proto_set_type(stat_msg, "%s_%s_%s", fty_proto_type(bmsg), addr_fun, sstep);
        fty_proto_set_time(stat_msg, metric_time_new_s);
        fty_proto_aux_insert(stat_msg, AGENT_CM_COUNT, "1");
        fty_proto_aux_insert(
            stat_msg, AGENT_CM_SUM, "%s", fty_proto_value(stat_msg)); // insert value as string into string
        fty_proto_aux_insert(stat_msg, AGENT_CM_TYPE, "%s", addr_fun);
        fty_proto_aux_insert(stat_msg, AGENT_CM_STEP, "%" PRIu32, step);
        fty_proto_aux_insert(stat_msg, AGENT_CM_LASTTS, "%" PRIu64, fty_proto_time(bmsg));
        fty_proto_set_ttl(stat_msg, 2 * step);

        // Power consumption treatment
        if (streq(addr_fun, "consumption")) {
            double consumption = 0.0;
            fty_proto_set_value(stat_msg, "%.1f", consumption);
            fty_proto_aux_insert(stat_msg, AGENT_CM_LASTTS, "%" PRIu64, now_ms/1000);
            fty_proto_set_unit(stat_msg, "Ws");
            log_debug("cmstats_put: Add new %s - %" PRIu64 "(%s)", skey.c_str(), now_ms/1000,
                getTimeStampStr(now_ms/1000).c_str());
        }

        zhashx_insert(self->stats, skey.c_str(), stat_msg);
        fty_proto_destroy(&stat_msg);
        return nullptr;
    }

    // there is already some value
    // so check if it's not already older than we need
    uint64_t metric_time_s      = fty_proto_time(stat_msg);
    uint64_t new_metric_time_s  = fty_proto_time(bmsg);
    uint64_t last_metric_time_s = fty_proto_aux_number(stat_msg, AGENT_CM_LASTTS, 0);
    if (new_metric_time_s <= last_metric_time_s) {
        //log_debug("cmstats_put: Message date too earlier for %s: %" PRIu64 "(%s)", skey.c_str(),
        //    last_metric_time_s, getTimeStampStr(last_metric_time_s).c_str());
        return nullptr;
    }
    // it is, return the stat value and "restart" the computation
    if (((now_ms - (metric_time_s * 1000)) >= (step * 1000))) {
        // duplicate "old" value for the interval, that has just ended
        fty_proto_t* ret = fty_proto_dup(stat_msg);

        // update statistics: restart it, as from now on we are going
        // to compute the statistics for the next interval
        fty_proto_set_time(stat_msg, metric_time_new_s);
        fty_proto_aux_insert(stat_msg, AGENT_CM_COUNT, "1");

        // If it is NOT power consumption data
        if (!streq(addr_fun, "consumption")) {
            fty_proto_aux_insert(stat_msg, AGENT_CM_SUM, "%s", fty_proto_value(bmsg));
            fty_proto_aux_insert(stat_msg, AGENT_CM_LASTTS, "%" PRIu64, new_metric_time_s);
        }
        // Else it is power consumption data
        else {
            // If at least one measure of power available
            if (last_metric_time_s != 0) {
                // Compute time between last measure and end of interval
                int64_t delta = static_cast<int64_t>(metric_time_new_s - last_metric_time_s);
                // If last measure before the current step, time is equal to the complete interval
                // (power don't change during the interval period)
                if (delta > static_cast<int64_t>(step)) delta = static_cast<int64_t>(step);
                else if (delta < 0) delta = 0;
                // Get last power received which is saved in the sum aux dictionary
                double last_metric_value = atof(fty_proto_aux_string(stat_msg, AGENT_CM_SUM, ""));
                // Save new value in the sum aux dictionary
                fty_proto_aux_insert(stat_msg, AGENT_CM_SUM, "%s", fty_proto_value(bmsg));
                // Compute last value missing for the returned interval
                double consumption = atof(fty_proto_value(stat_msg));
                double inc = last_metric_value * static_cast<double>(delta);
                log_debug("cmstats_put: End consumption for %s: inc=%.1f %" PRIu64 "(%s)-%" PRIu64 "(%s) %" PRIu64, skey.c_str(), inc,
                    metric_time_new_s, getTimeStampStr(metric_time_new_s).c_str(), last_metric_time_s, getTimeStampStr(last_metric_time_s).c_str(),
                    metric_time_new_s - last_metric_time_s);
                consumption += inc;
                fty_proto_set_value(ret, "%.1f", consumption);

                // and compute the first value for the new interval
                double value = atof(fty_proto_value(bmsg));
                consumption = value * static_cast<double>(now_ms/1000 - metric_time_new_s);
                if (consumption < 0) consumption = 0;
                fty_proto_set_value(stat_msg, "%.1f", consumption);
                fty_proto_aux_insert(stat_msg, AGENT_CM_LASTTS, "%" PRIu64, now_ms/1000);
                fty_proto_aux_insert(stat_msg, AGENT_CM_COUNT, "1");
                log_debug("cmstats_put: Update new consumption for %s: %.1f %" PRIu64 "(%s)-%" PRIu64 "(%s) %" PRIu64, skey.c_str(), consumption,
                    now_ms/1000, getTimeStampStr(now_ms/1000).c_str(), metric_time_new_s, getTimeStampStr(metric_time_new_s).c_str(),
                    now_ms/1000 - metric_time_new_s);

            }
        }
        return ret;
    }

    bool value_accepted = false;
    // if we're inside the interval, simply do the computation
    if (streq(addr_fun, "min"))
        value_accepted = s_min(bmsg, stat_msg);
    else if (streq(addr_fun, "max"))
        value_accepted = s_max(bmsg, stat_msg);
    else if (streq(addr_fun, "arithmetic_mean"))
        value_accepted = s_arithmetic_mean(bmsg, stat_msg);
    else if (streq(addr_fun, "consumption")) {
        log_debug("cmstats_put: Update consumption for %s", skey.c_str());
        value_accepted = s_consumption(bmsg, stat_msg);
    }
    // fail otherwise
    else
        assert(false);

    // increase the counter
    if (value_accepted) {
        fty_proto_aux_insert(stat_msg, AGENT_CM_COUNT, "%" PRIu64, fty_proto_aux_number(stat_msg, AGENT_CM_COUNT, 0) + 1);
        if (!streq(addr_fun, "consumption")) {
            fty_proto_aux_insert(stat_msg, AGENT_CM_LASTTS, "%" PRIu64, new_metric_time_s);
        }
    }
    return nullptr;
}

//  --------------------------------------------------------------------------
//  Remove from stats all entries related to the asset with asset_name

void cmstats_delete_asset(cmstats_t* self, const char* asset_name)
{
    assert(self);
    assert(asset_name);

    zlist_t* keys = zlist_new();
    // no autofree here, this list constains only _references_ to keys,
    // which are owned and cleanded up by self->stats on zhashx_delete

    for (fty_proto_t* stat_msg = reinterpret_cast<fty_proto_t*>(zhashx_first(self->stats)); stat_msg != nullptr;
         stat_msg              = reinterpret_cast<fty_proto_t*>(zhashx_next(self->stats))) {
        const char* key = reinterpret_cast<const char*>(zhashx_cursor(self->stats));
        if (streq(fty_proto_name(stat_msg), asset_name))
            zlist_append(keys, const_cast<char*>(key));
    }

    for (const char* key = reinterpret_cast<const char*>(zlist_first(keys)); key != nullptr;
         key             = reinterpret_cast<const char*>(zlist_next(keys))) {
        zhashx_delete(self->stats, key);
    }
    zlist_destroy(&keys);
}

//  --------------------------------------------------------------------------
//  Polling handler - publish && reset the computed values

void cmstats_poll(cmstats_t* self)
{
    assert(self);

    // What is it time now? [ms]
    uint64_t now_ms = uint64_t(zclock_time());

    for (fty_proto_t* stat_msg = reinterpret_cast<fty_proto_t*>(zhashx_first(self->stats)); stat_msg != nullptr;
         stat_msg              = reinterpret_cast<fty_proto_t*>(zhashx_next(self->stats))) {
        // take a key, actually it is the future subject of the message
        const char* key = reinterpret_cast<const char*>(zhashx_cursor(self->stats));

        // What is an assigned time for the metric ( in our case it is a left margin in the interval)
        uint64_t metric_time_s = fty_proto_time(stat_msg);
        uint64_t step          = fty_proto_aux_number(stat_msg, AGENT_CM_STEP, 0);
        // What SHOULD be an assigned time for the NEW stat metric (in our case it is a left margin in the NEW interval)
        uint64_t metric_time_new_s = (now_ms - (now_ms % (step * 1000))) / 1000;

        log_debug("cmstats_poll: key=%s\n\tnow_ms=%" PRIu64 ", metric_time_new_s=%" PRIu64 ", metric_time_s=%" PRIu64
                  ", (now_ms - (metric_time_s * 1000))=%" PRIu64 "s, step*1000=%" PRIu32 "ms",
            key, now_ms, metric_time_new_s, metric_time_s, (now_ms - metric_time_s * 1000), step * 1000);

        // Should this metic be published and computation restarted?
        if ((now_ms - (metric_time_s * 1000)) >= (step * 1000)) {
            // Yes, it should!
            fty_proto_t* ret = fty_proto_dup(stat_msg);
            log_debug("cmstats:\tPublishing message wiht subject=%s", key);

            // If consumption data, compute last value missing for the end of interval
            if (streq(fty_proto_aux_string(stat_msg, AGENT_CM_TYPE, ""), "consumption")) {
                uint64_t last_metric_time_s = fty_proto_aux_number(stat_msg, AGENT_CM_LASTTS, 0);
                // If we have receive a least one power measure
                if (last_metric_time_s != 0) {
                    // Compute time between last measure and end of interval
                    int64_t delta = static_cast<int64_t>(metric_time_new_s - last_metric_time_s);
                    // If last measure before the current step, time is equal to the complete interval
                    // (power don't change during the interval period)
                    if (delta > static_cast<int64_t>(step)) delta = static_cast<int64_t>(step);
                    else if (delta < 0) delta = 0;
                    // Get last power received which is saved in the sum aux dictionary
                    double last_metric_value = atof(fty_proto_aux_string(stat_msg, AGENT_CM_SUM, ""));
                    // Compute last value missing for the end interval
                    double consumption = atof(fty_proto_value(stat_msg));
                    double inc = last_metric_value * static_cast<double>(delta);
                    consumption += inc;
                    fty_proto_set_value(ret, "%.1f", consumption);
                    log_debug("cmstats_poll: End consumption for %s: new=%.1f inc=%.1f %" PRIu64"(%s)-%" PRIu64 "(%s) %" PRIu64, key, consumption, inc,
                        metric_time_new_s, getTimeStampStr(metric_time_new_s).c_str(), last_metric_time_s, getTimeStampStr(last_metric_time_s).c_str(),
                        metric_time_new_s - last_metric_time_s);

                    // and compute the first value for the new interval
                    consumption = last_metric_value * static_cast<double>(now_ms/1000 - metric_time_new_s);
                    if (consumption < 0) consumption = 0;
                    fty_proto_set_value(stat_msg, "%.1f", consumption);
                    fty_proto_aux_insert(stat_msg, AGENT_CM_LASTTS, "%" PRIu64, now_ms/1000);
                    log_debug("cmstats_poll: Update new consumption for %s: %.1f %" PRIu64 "(%s)-%" PRIu64 "(%s) %" PRIu64, key, consumption,
                        now_ms/1000, getTimeStampStr(now_ms/1000).c_str(), metric_time_new_s, getTimeStampStr(metric_time_new_s).c_str(),
                        now_ms/1000 - metric_time_new_s);
                }
            }
            else {
                // As we do not receive any message, start from ZERO
                fty_proto_aux_insert(stat_msg, AGENT_CM_SUM, "0");
                fty_proto_set_value(stat_msg, "0");
            }
            fty_proto_set_time(stat_msg, metric_time_new_s);
            fty_proto_aux_insert(stat_msg, AGENT_CM_COUNT, "0");
            fty_proto_print(ret);
            // Test if receive some data before publishing
            if (fty_proto_aux_number(ret, AGENT_CM_COUNT, 0) != 0) {
                int r = fty::shm::write_metric(ret);
                if (r == -1) {
                    log_error("cmstats:\tCannot publish statistics");
                }
            }
            else {
              log_info("No metrics for this step, do not publish");
            }
            fty_proto_destroy(&ret);
        }
    }
}

//  --------------------------------------------------------------------------
//  Save the cmstats to filename, return -1 if fail

int cmstats_save(cmstats_t* self, const char* filename)
{
    assert(self);

    zconfig_t* root = zconfig_new("cmstats", nullptr);
    int        i    = 1;
    for (fty_proto_t* bmsg = reinterpret_cast<fty_proto_t*>(zhashx_first(self->stats)); bmsg != nullptr;
         bmsg              = reinterpret_cast<fty_proto_t*>(zhashx_next(self->stats))) {
        // ZCONFIG doesn't allow spaces in keys! -> metric topic cannot be key
        // because it has an asset name inside!
        char* asset_key = nullptr;
        int   r         = asprintf(&asset_key, "%d", i);
        assert(r != -1); // make gcc @ rhel happy
        i++;
        const char* metric_topic = reinterpret_cast<const char*>(zhashx_cursor(self->stats));

        zconfig_t* item = zconfig_new(asset_key, root);
        zconfig_put(item, "metric_topic", metric_topic);
        zconfig_put(item, "type", fty_proto_type(bmsg));
        zconfig_put(item, "element_src", fty_proto_name(bmsg));
        zconfig_put(item, "value", fty_proto_value(bmsg));
        zconfig_put(item, "unit", fty_proto_unit(bmsg));
        zconfig_putf(item, "ttl", "%" PRIu32, fty_proto_ttl(bmsg));

        zhash_t* aux = fty_proto_aux(bmsg);
        for (const char* aux_value = reinterpret_cast<const char*>(zhash_first(aux)); aux_value != nullptr;
             aux_value             = reinterpret_cast<const char*>(zhash_next(aux))) {
            const char* aux_key = zhash_cursor(aux);
            char*       item_key;

            [[maybe_unused]] int r1 = asprintf(&item_key, "aux.%s", aux_key);
            assert(r1 != -1); // make gcc @ rhel happy

            zconfig_put(item, item_key, aux_value);
            zstr_free(&item_key);
        }
        zstr_free(&asset_key);
    }

    int r = zconfig_save(root, filename);
    zconfig_destroy(&root);
    return r;
}

//  --------------------------------------------------------------------------
//  Load the cmstats from filename

cmstats_t* cmstats_load(const char* filename)
{
    zconfig_t* root = zconfig_load(filename);

    if (!root)
        return nullptr;

    cmstats_t* self = cmstats_new();
    if (!self) {
        zconfig_destroy(&root);
        return nullptr;
    }
    zconfig_t* key_config = zconfig_child(root);
    for (; key_config != nullptr; key_config = zconfig_next(key_config)) {
        // 1. create bmsg
        const char*  metric_topic = zconfig_get(key_config, "metric_topic", "");
        fty_proto_t* bmsg         = fty_proto_new(FTY_PROTO_METRIC);
        fty_proto_set_type(bmsg, "%s", zconfig_get(key_config, "type", ""));
        fty_proto_set_name(bmsg, "%s", zconfig_get(key_config, "element_src", ""));
        fty_proto_set_value(bmsg, "%s", zconfig_get(key_config, "value", ""));
        fty_proto_set_unit(bmsg, "%s", zconfig_get(key_config, "unit", ""));
        fty_proto_set_ttl(bmsg, uint32_t(atoi(zconfig_get(key_config, "ttl", "0"))));

        double value = atof(fty_proto_value(bmsg));
        if (std::isnan(value)) {
            log_warning("cmstats_load:\tisnan (%s) for %s@%s, ignoring", fty_proto_value(bmsg), fty_proto_type(bmsg),
                fty_proto_name(bmsg));
            fty_proto_destroy(&bmsg);
            continue;
        }

        // 2. put aux things
        zconfig_t* bmsg_config = zconfig_child(key_config);
        for (; bmsg_config != nullptr; bmsg_config = zconfig_next(bmsg_config)) {
            const char* bmsg_key = zconfig_name(bmsg_config);
            if (strncmp(bmsg_key, "aux.", 4) != 0)
                continue;

            fty_proto_aux_insert(bmsg, (bmsg_key + 4), "%s", zconfig_value(bmsg_config));
        }

        value = atof(fty_proto_aux_string(bmsg, AGENT_CM_SUM, "0"));
        if (std::isnan(value)) {
            fty_proto_aux_insert(bmsg, AGENT_CM_SUM, "0");
        }

        zhashx_update(self->stats, metric_topic, bmsg);
        fty_proto_destroy(&bmsg);
    }

    zconfig_destroy(&root);
    return self;
}
