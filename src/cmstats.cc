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

/*
@header
    cmstats - Computing the stats on metrics
@discuss
@end
*/

#include "fty_metric_compute_classes.h"
#include "fty_metric_compute.h"
#include <cmath>


typedef void (compute_fn) (const fty_proto_t *bmsg, fty_proto_t *stat_msg);

//  Structure of our class
struct _cmstats_t {
    zhashx_t *stats; // a hash of FTY_PROTO metrics for "AVG/MIN/MAX" ready to be published
};

static void
s_destructor (void **self_p)
{
    fty_proto_destroy ((fty_proto_t**) self_p);
}

static void*
s_duplicator (const void *self)
{
    return (void*) fty_proto_dup ((fty_proto_t*) self);
}


// find minimum value
// \param bmsg - input new metric
// \param stat_msg - output statistic metric
static bool
s_min (const fty_proto_t *bmsg, fty_proto_t *stat_msg)
{
    assert (bmsg);
    assert (stat_msg);
    double bmsg_value = atof (fty_proto_value ((fty_proto_t*) bmsg));
    uint64_t count = fty_proto_aux_number (stat_msg, AGENT_CM_COUNT, 0);
    double stat_value = atof (fty_proto_value (stat_msg));

    if (std::isnan (stat_value)
    ||  count == 0
    || (bmsg_value < stat_value)) {
        fty_proto_set_value (stat_msg, "%.2f", bmsg_value);
    }

    return true;
}

// find maximum value
// \param bmsg - input new metric
// \param stat_msg - output statistic metric
static bool
s_max (const fty_proto_t *bmsg, fty_proto_t *stat_msg)
{
    assert (bmsg);
    assert (stat_msg);
    double bmsg_value = atof (fty_proto_value ((fty_proto_t*) bmsg));
    uint64_t count = fty_proto_aux_number (stat_msg, AGENT_CM_COUNT, 0);
    double stat_value = atof (fty_proto_value (stat_msg));

    if (std::isnan (stat_value)
    ||  count == 0
    || (bmsg_value > stat_value)) {
        fty_proto_set_value (stat_msg, "%.2f", bmsg_value);
    }

    return true;
}

// find average value
// \param bmsg - input new metric
// \param stat_msg - output statistic metric
static bool
s_arithmetic_mean (const fty_proto_t *bmsg, fty_proto_t *stat_msg)
{
    assert (bmsg);
    assert (stat_msg);
    double value = atof (fty_proto_value ((fty_proto_t*) bmsg));
    uint64_t count = fty_proto_aux_number (stat_msg, AGENT_CM_COUNT, 0);
    double sum = atof (fty_proto_aux_string (stat_msg, AGENT_CM_SUM, "0"));

    if (std::isnan (value) || std::isnan (sum)) {
        log_warning ("s_arithmetic_mean: isnan value(%s) or sum (%s) for %s@%s, skipping",
            fty_proto_value ((fty_proto_t*) bmsg),
            fty_proto_aux_string (stat_msg, AGENT_CM_SUM, "0"),
            fty_proto_type ((fty_proto_t*) bmsg),
            fty_proto_name ((fty_proto_t*) bmsg)
        );
        return false;
    }

    // 0 means that we have first value
    if (count == 0)
        sum = value;
    else
        sum += value;

    double avg = (sum / (count+1));
    if (std::isnan (avg)) {
        log_error ("s_arithmetic_mean: isnan (avg) %f / (%" PRIu64 " + 1), for %s@%s, skipping",
            sum,
            count,
            fty_proto_type ((fty_proto_t*) bmsg),
            fty_proto_name ((fty_proto_t*) bmsg)
            );
        return false;
    }

    // Sample was accepted
    fty_proto_aux_insert (stat_msg, AGENT_CM_SUM, "%f", sum);
    fty_proto_set_value (stat_msg, "%.2f", avg);
    return true;
}

//  --------------------------------------------------------------------------
//  Create a new cmstats

cmstats_t *
cmstats_new (void)
{
    cmstats_t *self = (cmstats_t *) zmalloc (sizeof (cmstats_t));
    assert (self);
    //  Initialize class properties here
    self->stats = zhashx_new ();
    assert (self->stats);
    zhashx_set_destructor (self->stats, s_destructor);
    zhashx_set_duplicator (self->stats, s_duplicator);

    return self;
}


//  --------------------------------------------------------------------------
//  Destroy the cmstats

void
cmstats_destroy (cmstats_t **self_p)
{
    assert (self_p);
    if (*self_p) {
        cmstats_t *self = *self_p;
        //  Free class properties here
        zhashx_destroy (&self->stats);
        //  Free object itself
        free (self);
        *self_p = NULL;
    }
}

//  --------------------------------------------------------------------------
//  Print the cmstats

void
cmstats_print (cmstats_t *self)
{
    assert (self);
    for (void* it = zhashx_first (self->stats);
               it != NULL;
               it = zhashx_next (self->stats))
    {
        log_debug ("%s =>", (char*) zhashx_cursor (self->stats));
        fty_proto_print ((fty_proto_t*) it);
    }
}

//  --------------------------------------------------------------------------
// Update statistics with "aggr_fun" and "step" for the incomming message "bmsg"

fty_proto_t *
cmstats_put (
    cmstats_t *self,
    const char* addr_fun,
    const char *sstep,
    uint32_t step,
    fty_proto_t *bmsg)
{
    assert (self);
    assert (addr_fun);
    assert (bmsg);

    uint64_t now_ms = (uint64_t) zclock_time ();
    // round the now to earliest time start
    // ie for 12:16:29 / step 15*60 return 12:15:00
    //    for 12:16:29 / step 60*60 return 12:00:00
    //    ... etc
    // works well for any value of step
    uint64_t metric_time_new_s = (now_ms - (now_ms % (step * 1000))) / 1000;

    char *key;
    int r = asprintf (&key, "%s_%s_%s@%s",
            fty_proto_type (bmsg),
            addr_fun,
            sstep,
            fty_proto_name (bmsg));
    assert (r != -1);   // make gcc @ rhel happy

    fty_proto_t *stat_msg = (fty_proto_t*) zhashx_lookup (self->stats, key);

    // handle the first insert
    if (!stat_msg) {
        stat_msg = fty_proto_dup (bmsg);
        fty_proto_set_type (stat_msg, "%s_%s_%s",
            fty_proto_type (bmsg),
            addr_fun,
            sstep);
        fty_proto_set_time (stat_msg, metric_time_new_s);
        fty_proto_aux_insert (stat_msg, AGENT_CM_COUNT, "1");
        fty_proto_aux_insert (stat_msg, AGENT_CM_SUM, "%s", fty_proto_value (stat_msg)); // insert value as string into string
        fty_proto_aux_insert (stat_msg, AGENT_CM_TYPE, "%s", addr_fun);
        fty_proto_aux_insert (stat_msg, AGENT_CM_STEP, "%" PRIu32, step);
        fty_proto_aux_insert (stat_msg, AGENT_CM_LASTTS, "%" PRIu64, fty_proto_time(bmsg));
        fty_proto_set_ttl (stat_msg, 2 * step);
        zhashx_insert (self->stats, key, stat_msg);
        zstr_free (&key);
        fty_proto_destroy (&stat_msg);
        return NULL;
    }
    zstr_free (&key);

    // there is already some value
    // so check if it's not already older than we need
    uint64_t metric_time_s = fty_proto_time (stat_msg);
    uint64_t new_metric_time_s = fty_proto_time(bmsg);
    uint64_t last_metric_time_s =  fty_proto_aux_number (stat_msg, AGENT_CM_LASTTS, 0);
    if(new_metric_time_s <= last_metric_time_s)
      return NULL;

    // it is, return the stat value and "restart" the computation
    if ( ((now_ms - (metric_time_s * 1000)) >= (step * 1000)) ) {
        // duplicate "old" value for the interval, that has just ended
        fty_proto_t *ret = fty_proto_dup (stat_msg);

        // update statistics: restart it, as from now on we are going
        // to compute the statistics for the next interval
        fty_proto_set_time (stat_msg, metric_time_new_s);
        fty_proto_aux_insert (stat_msg, AGENT_CM_COUNT, "1");
        fty_proto_aux_insert (stat_msg, AGENT_CM_SUM, "%s", fty_proto_value (bmsg));
        fty_proto_aux_insert (stat_msg, AGENT_CM_LASTTS, "%" PRIu64, new_metric_time_s);

        fty_proto_set_value (stat_msg, "%s", fty_proto_value (bmsg));
        return ret;
    }

    bool value_accepted = false;
    // if we're inside the interval, simply do the computation
    if (streq (addr_fun, "min"))
        value_accepted = s_min (bmsg, stat_msg);
    else
    if (streq (addr_fun, "max"))
        value_accepted = s_max (bmsg, stat_msg);
    else
    if (streq (addr_fun, "arithmetic_mean"))
        value_accepted = s_arithmetic_mean (bmsg, stat_msg);
    // fail otherwise
    else
        assert (false);

    // increase the counter
    if (value_accepted) {
        fty_proto_aux_insert (stat_msg, AGENT_CM_COUNT, "%" PRIu64,
            fty_proto_aux_number (stat_msg, AGENT_CM_COUNT, 0) + 1
        );
        fty_proto_aux_insert (stat_msg, AGENT_CM_LASTTS, "%" PRIu64, new_metric_time_s);
    }

    return NULL;
}

//  --------------------------------------------------------------------------
//  Remove from stats all entries related to the asset with asset_name

void
cmstats_delete_asset (cmstats_t *self, const char *asset_name)
{
    assert (self);
    assert (asset_name);

    zlist_t *keys = zlist_new ();
    // no autofree here, this list constains only _references_ to keys,
    // which are owned and cleanded up by self->stats on zhashx_delete

    for (fty_proto_t *stat_msg = (fty_proto_t*) zhashx_first (self->stats);
                       stat_msg != NULL;
                       stat_msg = (fty_proto_t*) zhashx_next (self->stats))
    {
        const char* key = (const char*) zhashx_cursor (self->stats);
        if (streq (fty_proto_name (stat_msg), asset_name))
            zlist_append (keys, (void*) key);
    }

    for (const char* key = (const char*) zlist_first (keys);
                     key != NULL;
                     key = (const char*) zlist_next (keys))
    {
        zhashx_delete (self->stats, key);
    }
    zlist_destroy (&keys);
}

//  --------------------------------------------------------------------------
//  Polling handler - publish && reset the computed values

void
cmstats_poll (cmstats_t *self)
{
    assert (self);

    // What is it time now? [ms]
    uint64_t now_ms = (uint64_t) zclock_time ();

    for (fty_proto_t *stat_msg = (fty_proto_t*) zhashx_first (self->stats);
                       stat_msg != NULL;
                       stat_msg = (fty_proto_t*) zhashx_next (self->stats))
    {
        // take a key, actually it is the future subject of the message
        const char* key = (const char*) zhashx_cursor (self->stats);

        // What is an assigned time for the metric ( in our case it is a left margin in the interval)
        uint64_t metric_time_s = fty_proto_time (stat_msg);
        uint64_t step = fty_proto_aux_number (stat_msg, AGENT_CM_STEP, 0);
        // What SHOULD be an assigned time for the NEW stat metric (in our case it is a left margin in the NEW interval)
        uint64_t metric_time_new_s = (now_ms - (now_ms % (step * 1000))) / 1000;

        log_debug ("cmstats_poll: key=%s\n\tnow_ms=%" PRIu64 ", metric_time_new_s=%" PRIu64 ", metric_time_s=%" PRIu64 ", (now_ms - (metric_time_s * 1000))=%" PRIu64 "s, step*1000=%" PRIu32 "ms",
            key,
            now_ms,
            metric_time_new_s,
            metric_time_s,
            (now_ms - metric_time_s * 1000),
            step * 1000);

        // Should this metic be published and computation restarted?
        if ((now_ms - (metric_time_s * 1000)) >= (step * 1000)) {
            // Yes, it should!
            fty_proto_t *ret = fty_proto_dup (stat_msg);
            log_debug ("cmstats:\tPublishing message wiht subject=%s", key);
            fty_proto_print (ret);

            if(fty_proto_aux_number(ret, AGENT_CM_COUNT, 0) == 0) {
              log_info ("No metrics for this step, do not publish");
            } else {
              int r = fty::shm::write_metric(ret);
              if ( r == -1 ) {
                  log_error ("cmstats:\tCannot publish statistics");
              }
            }
            fty_proto_destroy(&ret);

            fty_proto_set_time (stat_msg, metric_time_new_s);
            fty_proto_aux_insert (stat_msg, AGENT_CM_COUNT, "0"); // As we do not receive any message, start from ZERO
            fty_proto_aux_insert (stat_msg, AGENT_CM_SUM, "0");  // As we do not receive any message, start from ZERO

            fty_proto_set_value (stat_msg, "0");  // As we do not receive any message, start from ZERO

        }
    }
}

//  --------------------------------------------------------------------------
//  Save the cmstats to filename, return -1 if fail

int
cmstats_save (cmstats_t *self, const char *filename)
{
    assert (self);

    zconfig_t *root = zconfig_new ("cmstats", NULL);
    int i = 1;
    for (fty_proto_t *bmsg = (fty_proto_t*) zhashx_first (self->stats);
                       bmsg != NULL;
                       bmsg = (fty_proto_t*) zhashx_next (self->stats))
    {
        // ZCONFIG doesn't allow spaces in keys! -> metric topic cannot be key
        // because it has an asset name inside!
        char *asset_key = NULL;
        int r = asprintf (&asset_key, "%d", i);
        assert (r != -1);   // make gcc @ rhel happy
        i++;
        const char* metric_topic = (const char*) zhashx_cursor (self->stats);

        zconfig_t *item = zconfig_new (asset_key, root);
        zconfig_put (item, "metric_topic", metric_topic);
        zconfig_put (item, "type", fty_proto_type (bmsg));
        zconfig_put (item, "element_src", fty_proto_name (bmsg));
        zconfig_put (item, "value", fty_proto_value (bmsg));
        zconfig_put (item, "unit", fty_proto_unit (bmsg));
        zconfig_putf (item, "ttl", "%" PRIu32, fty_proto_ttl (bmsg));

        zhash_t *aux = fty_proto_aux (bmsg);
        for (const char *aux_value = (const char*) zhash_first (aux);
                         aux_value != NULL;
                         aux_value = (const char*) zhash_next (aux))
        {
            const char *aux_key = (const char*) zhash_cursor (aux);
            char *item_key;
            int r = asprintf (&item_key, "aux.%s", aux_key);
            assert (r != -1);   // make gcc @ rhel happy
            zconfig_put (item, item_key, aux_value);
            zstr_free (&item_key);
        }
        zstr_free (&asset_key);
    }

    int r = zconfig_save (root, filename);
    zconfig_destroy (&root);
    return r;
}

//  --------------------------------------------------------------------------
//  Load the cmstats from filename

cmstats_t *
cmstats_load (const char *filename)
{
    zconfig_t *root = zconfig_load (filename);

    if (!root)
        return NULL;

    cmstats_t *self = cmstats_new ();
    if (!self) {
        zconfig_destroy (&root);
        return NULL;
    }
    zconfig_t *key_config = zconfig_child (root);
    for (; key_config != NULL; key_config = zconfig_next (key_config))
    {
        // 1. create bmsg
        const char *metric_topic = zconfig_get (key_config, "metric_topic", "");
        fty_proto_t *bmsg = fty_proto_new (FTY_PROTO_METRIC);
        fty_proto_set_type (bmsg, "%s", zconfig_get (key_config, "type", ""));
        fty_proto_set_name (bmsg, "%s", zconfig_get (key_config, "element_src", ""));
        fty_proto_set_value (bmsg, "%s", zconfig_get (key_config, "value", ""));
        fty_proto_set_unit (bmsg, "%s", zconfig_get (key_config, "unit", ""));
        fty_proto_set_ttl (bmsg, atoi (zconfig_get (key_config, "ttl", "0")));

        double value = atof (fty_proto_value (bmsg));
        if (std::isnan (value)) {
            log_warning ("cmstats_load:\tisnan (%s) for %s@%s, ignoring",
                    fty_proto_value (bmsg),
                    fty_proto_type (bmsg),
                    fty_proto_name (bmsg)
                    );
            fty_proto_destroy (&bmsg);
            continue;
        }

        // 2. put aux things
        zconfig_t *bmsg_config = zconfig_child (key_config);
        for (; bmsg_config != NULL; bmsg_config = zconfig_next (bmsg_config))
        {
            const char *bmsg_key = zconfig_name (bmsg_config);
            if (strncmp (bmsg_key, "aux.", 4) != 0)
                continue;

            fty_proto_aux_insert (bmsg, (bmsg_key+4), "%s", zconfig_value (bmsg_config));
        }

        value = atof (fty_proto_aux_string (bmsg, AGENT_CM_SUM, "0"));
        if (std::isnan (value)) {
            fty_proto_aux_insert (bmsg, AGENT_CM_SUM, "0");
        }

        zhashx_update (self->stats, metric_topic, bmsg);
        fty_proto_destroy (&bmsg);
    }

    zconfig_destroy (&root);
    return self;
}

//  --------------------------------------------------------------------------
//  Self test of this class

void
cmstats_test (bool verbose)
{
    printf (" * cmstats: ");
    printf ("\n");

    //  @selftest
    static const char *file = "src/cmstats.zpl";
    unlink (file);

    log_info ("Test 1: Simple test on empty structure");
    cmstats_t *self = cmstats_new ();
    assert (self);
    cmstats_print (self);
    cmstats_delete_asset (self, "SOMESTRING");
    // TODO uncomment, when tests for this function would be supported
    // cmstats_poll (self, client);
    cmstats_save (self, "itshouldbeemptyfile");
    cmstats_destroy (&self);
    log_info ("Test2: some test");
    self = cmstats_new ();
    //XXX: the test is sensitive on timing!!!
    //     so it must start at the beggining of the second
    //     other option is to not test in second precision,
    //     which will increase the time of make check far beyond
    //     what developers would accept ;-)
    {
        int64_t now_ms = zclock_time ();
        int64_t sl = 10000 - (now_ms % 10000);
        zclock_sleep (sl);

        log_debug ("now_ms=%" PRIi64 ", sl=%" PRIi64 ", now=%" PRIi64,
                   now_ms,
                   sl,
                   zclock_time ());
    }
    zclock_sleep (1000);

    // 1. min test
    //  1.1 first metric in
    zmsg_t *msg = fty_proto_encode_metric (
            NULL,
            time (NULL),
            10,
            "TYPE",
            "ELEMENT_SRC",
            "100.989999",
            "UNIT");
    fty_proto_t *bmsg = fty_proto_decode (&msg);
    fty_proto_t *stats = NULL;
    fty_proto_print (bmsg);

    stats = cmstats_put (self, "min", "10s", 10, bmsg);
    assert (!stats);
    stats = cmstats_put (self, "max", "10s", 10, bmsg);
    assert (!stats);
    stats = cmstats_put (self, "arithmetic_mean", "10s", 10, bmsg);
    assert (!stats);
    fty_proto_destroy (&bmsg);

    zclock_sleep(1000);
    //  1.2 second metric (inside interval) in
    msg = fty_proto_encode_metric (
            NULL,
            time (NULL),
            10,
            "TYPE",
            "ELEMENT_SRC",
            "42.11",
            "UNIT");
    bmsg = fty_proto_decode (&msg);
    fty_proto_print (bmsg);

    zclock_sleep (5000);
    stats = cmstats_put (self, "min", "10s", 10, bmsg);
    assert (!stats);
    stats = cmstats_put (self, "max", "10s", 10, bmsg);
    assert (!stats);
    stats = cmstats_put (self, "arithmetic_mean", "10s", 10, bmsg);
    assert (!stats);
    fty_proto_destroy (&bmsg);

    zclock_sleep (6100);

    //  1.3 third metric (outside interval) in
    msg = fty_proto_encode_metric (
            NULL,
            time (NULL),
            10,
            "TYPE",
            "ELEMENT_SRC",
            "42.889999999999",
            "UNIT");
    bmsg = fty_proto_decode (&msg);
    fty_proto_print (bmsg);

    //  1.4 check the minimal value
    stats = cmstats_put (self, "min", "10s", 10, bmsg);
    assert (stats);

    fty_proto_print (stats);
    log_trace("value : %s", fty_proto_value (stats));
    assert (streq (fty_proto_value (stats), "42.11"));
    assert (streq (fty_proto_aux_string (stats, AGENT_CM_COUNT, NULL), "2"));
    fty_proto_destroy (&stats);

    //  1.5 check the maximum value
    stats = cmstats_put (self, "max", "10s", 10, bmsg);
    assert (stats);

    fty_proto_print (stats);
    log_trace("value : %s", fty_proto_value (stats));
    assert (streq (fty_proto_value (stats), "100.989999"));
    assert (streq (fty_proto_aux_string (stats, AGENT_CM_COUNT, NULL), "2"));
    fty_proto_destroy (&stats);

    //  1.6 check the arithmetic_mean
    stats = cmstats_put (self, "arithmetic_mean", "10s", 10, bmsg);
    assert (stats);

    fty_proto_print (stats);
    log_info ("avg real: %s", fty_proto_value (stats) );
    log_info ("avg expected: %f", (100.989999+42.11) / 2 );

    char *xxx = NULL;
    int r = asprintf (&xxx, "%.2f", (100.99+42.1) / 2);
    assert (r != -1);   // make gcc @ rhel happy
    assert (streq (fty_proto_value (stats), xxx));
    zstr_free (&xxx);
    assert (streq (fty_proto_aux_string (stats, AGENT_CM_COUNT, NULL), "2"));
    fty_proto_destroy (&bmsg);
    fty_proto_destroy (&stats);

    cmstats_save (self, "src/cmstats.zpl");
    cmstats_destroy (&self);
    self = cmstats_load ("src/cmstats.zpl");

    // TRIVIA: extend the testing of self->stats
    //         hint is - uncomment the print :)
    //cmstats_print (self);
    assert (zhashx_lookup (self->stats, "TYPE_max_10s@ELEMENT_SRC"));

    cmstats_delete_asset (self, "ELEMENT_SRC");
    assert (!zhashx_lookup (self->stats, "TYPE_max_10s@ELEMENT_SRC"));

    cmstats_destroy (&self);
    unlink (file);
    //  @end
    printf ("OK\n");
}
