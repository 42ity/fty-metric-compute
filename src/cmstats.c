/*  =========================================================================
    cmstats - Computing the stats on metrics

    Copyright (C) 2016 Eaton                                               
                                                                           
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

#include "agent_cm_classes.h"

//  Structure of our class

typedef void (compute_fn) (const bios_proto_t *bmsg, bios_proto_t *stat_msg);

struct _cmstats_t {
    zhashx_t *stats;
};

static void
s_destructor (void **self_p)
{
    bios_proto_destroy ((bios_proto_t**) self_p);
}

static void*
s_duplicator (const void *self)
{
    return (void*) bios_proto_dup ((bios_proto_t*) self);
}


// find minimum value
static void
s_min (const bios_proto_t *bmsg, bios_proto_t *stat_msg)
{
    uint64_t bmsg_value = atol (bios_proto_value ((bios_proto_t*) bmsg));
    uint64_t stat_value = atol (bios_proto_value (stat_msg));

    if ((bios_proto_aux_number (stat_msg, AGENT_CM_COUNT, 1) == 1)
    || (bmsg_value < stat_value)) {
        bios_proto_set_value (stat_msg, "%"PRIu64, bmsg_value);
    }
}

static void
s_max (const bios_proto_t *bmsg, bios_proto_t *stat_msg)
{
    uint64_t bmsg_value = atol (bios_proto_value ((bios_proto_t*) bmsg));
    uint64_t stat_value = atol (bios_proto_value (stat_msg));

    if (bmsg_value > stat_value) {
        bios_proto_set_value (stat_msg, "%"PRIu64, bmsg_value);
    }
}

static void
s_arithmetic_mean (const bios_proto_t *bmsg, bios_proto_t *stat_msg)
{
    uint64_t value = atol (bios_proto_value ((bios_proto_t*) bmsg));
    uint64_t count = bios_proto_aux_number (stat_msg, AGENT_CM_COUNT, 0);
    uint64_t sum = bios_proto_aux_number (stat_msg, AGENT_CM_SUM, 0);

    // 0 means that we have first value
    if (sum == 0) {
        sum = atof (bios_proto_value (stat_msg));
    }

    sum += value;

    bios_proto_aux_insert (stat_msg, AGENT_CM_SUM, "%"PRIu64, sum);
    bios_proto_set_value (stat_msg, "%f", ((double)sum) / (count + 1));
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
AGENT_CM_EXPORT void
cmstats_print (cmstats_t *self)
{
    assert (self);
    for (void* it = zhashx_first (self->stats);
               it != NULL;
               it = zhashx_next (self->stats))
    {
        zsys_debug ("%s =>", (char*) zhashx_cursor (self->stats));
        bios_proto_print ((bios_proto_t*) it);
    }
}

//  --------------------------------------------------------------------------
// Compute the $type value for given step - if the interval is over and new metric
// is already inside the interval, NULL is returned
// Otherwise new bios_proto_t metric is returned. Caller is responsible for
// destroying the value.
//
// Type is supposed to be
// * min - to find a minimum value inside given interval
// * max - for find a maximum value
// * arithmetic_mean - to compute arithmetic mean
//
bios_proto_t *
cmstats_put (cmstats_t *self, const char* type, const char *sstep, uint32_t step, bios_proto_t *bmsg)
{
    assert (self);
    assert (type);
    assert (bmsg);

    uint64_t now_ms = (uint64_t) zclock_time ();
    uint64_t now_s = now_ms / 1000;
    // round the now to earliest time start
    // ie for 12:16:29 / step 15*60 return 12:15:00
    //    for 12:16:29 / step 60*60 return 12:00:00
    //    ... etc
    // works well for any value of step
    now_s = now_s - (now_s % step);

    char *key;
    int r = asprintf (&key, "%s_%s_%s@%s",
            bios_proto_type (bmsg),
            type,
            sstep,
            bios_proto_element_src (bmsg));
    assert (r != -1);   // make gcc @ rhel happy

    bios_proto_t *stat_msg = (bios_proto_t*) zhashx_lookup (self->stats, key);

    // handle the first insert
    if (!stat_msg) {

        stat_msg = bios_proto_dup (bmsg);
        bios_proto_set_type (stat_msg, "%s_%s_%s",
            bios_proto_type (bmsg),
            type,
            sstep);

        bios_proto_aux_insert (stat_msg, AGENT_CM_TIME, "%"PRIu64, now_s);
        bios_proto_aux_insert (stat_msg, AGENT_CM_COUNT, "1");
        bios_proto_aux_insert (stat_msg, AGENT_CM_SUM, "0");
        bios_proto_aux_insert (stat_msg, AGENT_CM_TYPE, "%s", type);
        bios_proto_aux_insert (stat_msg, AGENT_CM_STEP, "%"PRIu32, step);
        bios_proto_set_ttl (stat_msg, 2 * step);
        zhashx_insert (self->stats, key, stat_msg);
        zstr_free (&key);
        bios_proto_destroy (&stat_msg);
        return NULL;
    }
    zstr_free (&key);

    // there is already some value
    // so check if it's not already older than we need
    uint64_t stat_now_s = bios_proto_aux_number (stat_msg, AGENT_CM_TIME, 0);

    // it is, return the stat value and "restart" the computation
    if ((now_ms - (stat_now_s * 1000)) >= (step * 1000)) {
        bios_proto_t *ret = bios_proto_dup (stat_msg);

        bios_proto_aux_insert (stat_msg, AGENT_CM_TIME, "%"PRIu64, now_s);
        bios_proto_aux_insert (stat_msg, AGENT_CM_COUNT, "1");
        bios_proto_aux_insert (stat_msg, AGENT_CM_SUM, "0");

        bios_proto_set_value (stat_msg, bios_proto_value (bmsg));

        zsys_debug ("return ret <%p>", (void*) ret);
        return ret;
    }

    // if we're inside the interval, simply do the computation
    if (streq (type, "min"))
        s_min (bmsg, stat_msg);
    else
    if (streq (type, "max"))
        s_max (bmsg, stat_msg);
    else
    if (streq (type, "arithmetic_mean"))
        s_arithmetic_mean (bmsg, stat_msg);
    // fail otherwise
    else
        assert (false);

    // increase the counter
    bios_proto_aux_insert (stat_msg, AGENT_CM_COUNT, "%"PRIu64, 
        bios_proto_aux_number (stat_msg, AGENT_CM_COUNT, 0) + 1
    );

    return NULL;
}

//  --------------------------------------------------------------------------
//  Remove all the entries related to device dev from stats
void
cmstats_delete_dev (cmstats_t *self, const char *dev)
{
    assert (self);
    assert (dev);

    zlist_t *keys = zlist_new ();
    // no autofree here, this list constains only _references_ to keys,
    // which are owned and cleanded up by self->stats on zhashx_delete

    for (bios_proto_t *stat_msg = (bios_proto_t*) zhashx_first (self->stats);
                       stat_msg != NULL;
                       stat_msg = (bios_proto_t*) zhashx_next (self->stats))
    {
        const char* key = (const char*) zhashx_cursor (self->stats);
        if (streq (bios_proto_element_src (stat_msg), dev))
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
cmstats_poll (cmstats_t *self, mlm_client_t *client, bool verbose)
{
    assert (self);
    assert (client);

    uint64_t now_ms = (uint64_t) zclock_time ();
    uint64_t now_s = now_ms / 1000;

    for (bios_proto_t *stat_msg = (bios_proto_t*) zhashx_first (self->stats);
                       stat_msg != NULL;
                       stat_msg = (bios_proto_t*) zhashx_next (self->stats))
    {
        const char* key = (const char*) zhashx_cursor (self->stats);

        uint64_t stat_now_s = bios_proto_aux_number (stat_msg, AGENT_CM_TIME, 0);
        uint64_t step = bios_proto_aux_number (stat_msg, AGENT_CM_STEP, 0);

        if (verbose)
            zsys_debug ("cmstats_poll: key=%s\n\tnow_ms=%"PRIu64 ", now_s=%"PRIu64 ", stat_now_s=%"PRIu64 ", (now_ms - (stat_now_s * 1000))=%" PRIu64 ", step*1000=%"PRIu32,
            key,
            now_ms,
            now_s,
            stat_now_s,
            (now_ms - stat_now_s * 1000),
            step * 1000);

        // it is, return the stat value and "restart" the computation
        if ((now_ms - (stat_now_s * 1000)) >= (step * 1000)) {
            bios_proto_t *ret = bios_proto_dup (stat_msg);

            bios_proto_aux_insert (stat_msg, AGENT_CM_TIME, "%"PRIu64, now_s);
            bios_proto_aux_insert (stat_msg, AGENT_CM_COUNT, "1");
            bios_proto_aux_insert (stat_msg, AGENT_CM_SUM, "0");

            bios_proto_set_value (stat_msg, "0");

            if (verbose) {
                zsys_debug ("cmstats:\tpublish message, subject=%s", key);
                bios_proto_print (ret);
            }
            zmsg_t *msg = bios_proto_encode (&ret);
            mlm_client_send (client, key, &msg);
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
    for (bios_proto_t *bmsg = (bios_proto_t*) zhashx_first (self->stats);
                       bmsg != NULL;
                       bmsg = (bios_proto_t*) zhashx_next (self->stats))
    {
        const char* key = (const char*) zhashx_cursor (self->stats);

        zconfig_t *item = zconfig_new (key, root);
        zconfig_put (item, "type", bios_proto_type (bmsg));
        zconfig_put (item, "element_src", bios_proto_element_src (bmsg));
        zconfig_put (item, "value", bios_proto_value (bmsg));
        zconfig_put (item, "unit", bios_proto_unit (bmsg));
        zconfig_putf (item, "ttl", "%"PRIu32, bios_proto_ttl (bmsg));

        zhash_t *aux = bios_proto_aux (bmsg);
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
    zconfig_t *key_config = zconfig_child (root);
    for (; key_config != NULL; key_config = zconfig_next (key_config))
    {
        const char *key = zconfig_name (key_config);

        // 1. create bmsg
        bios_proto_t *bmsg = bios_proto_new (BIOS_PROTO_METRIC);
        bios_proto_set_type (bmsg, zconfig_get (key_config, "type", ""));
        bios_proto_set_element_src (bmsg, zconfig_get (key_config, "element_src", ""));
        bios_proto_set_value (bmsg, zconfig_get (key_config, "value", ""));
        bios_proto_set_unit (bmsg, zconfig_get (key_config, "unit", ""));
        bios_proto_set_ttl (bmsg, atoi (zconfig_get (key_config, "unit", "0")));

        // 2. put aux things
        zconfig_t *bmsg_config = zconfig_child (key_config);
        for (; bmsg_config != NULL; bmsg_config = zconfig_next (bmsg_config))
        {
            const char *bmsg_key = zconfig_name (bmsg_config);
            if (strncmp (bmsg_key, "aux.", 4) != 0)
                continue;

            bios_proto_aux_insert (bmsg, (bmsg_key+4), zconfig_value (bmsg_config));
        }

        zhashx_update (self->stats, key, bmsg);
        bios_proto_destroy (&bmsg);
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
    if (verbose)
        printf ("\n");

    //  @selftest
    //  Simple create/destroy test

    static const char *file = "src/cmstats.zpl";
    unlink (file);

    cmstats_t *self = cmstats_new ();
    assert (self);


    //XXX: the test is sensitive on timing!!!
    //     so it must start at the beggining of the second
    //     other option is to not test in second precision,
    //     which will increase the time of make check far beyond
    //     what developers would accept ;-)
    {
        int64_t now_ms = zclock_time ();
        int64_t sl = 1000 - (now_ms % 1000);
        zclock_sleep (sl);
        if (verbose)
            zsys_debug ("now_ms=%"PRIi64 ", sl=%"PRIi64 ", now=%"PRIi64,
                now_ms,
                sl,
                zclock_time ());
    }

    // 1. min test
    //  1.1 first metric in
    zmsg_t *msg = bios_proto_encode_metric (
            NULL,
            "TYPE",
            "ELEMENT_SRC",
            "100",
            "UNIT",
            10);
    bios_proto_t *bmsg = bios_proto_decode (&msg);
    bios_proto_t *stats = NULL;

    stats = cmstats_put (self, "min", "1", 1, bmsg);
    assert (!stats);
    stats = cmstats_put (self, "max", "1", 1, bmsg);
    assert (!stats);
    stats = cmstats_put (self, "arithmetic_mean", "1", 1, bmsg);
    assert (!stats);
    bios_proto_destroy (&bmsg);
    zclock_sleep (500);

    //  1.2 second metric (inside interval) in
    msg = bios_proto_encode_metric (
            NULL,
            "TYPE",
            "ELEMENT_SRC",
            "42",
            "UNIT",
            10);
    bmsg = bios_proto_decode (&msg);

    stats = cmstats_put (self, "min", "1", 1, bmsg);
    assert (!stats);
    stats = cmstats_put (self, "max", "1", 1, bmsg);
    assert (!stats);
    stats = cmstats_put (self, "arithmetic_mean", "1", 1, bmsg);
    assert (!stats);
    bios_proto_destroy (&bmsg);
    
    zclock_sleep (610);

    //  1.3 third metric (outside interval) in
    msg = bios_proto_encode_metric (
            NULL,
            "TYPE",
            "ELEMENT_SRC",
            "42",
            "UNIT",
            10);
    bmsg = bios_proto_decode (&msg);

    //  1.4 check the minimal value
    stats = cmstats_put (self, "min", "1", 1, bmsg);
    assert (stats);
    if (verbose)
        bios_proto_print (stats);
    assert (streq (bios_proto_value (stats), "42"));
    assert (streq (bios_proto_aux_string (stats, AGENT_CM_COUNT, NULL), "2"));
    bios_proto_destroy (&stats);

    //  1.5 check the maximum value
    stats = cmstats_put (self, "max", "1", 1, bmsg);
    assert (stats);
    if (verbose)
        bios_proto_print (stats);
    assert (streq (bios_proto_value (stats), "100"));
    assert (streq (bios_proto_aux_string (stats, AGENT_CM_COUNT, NULL), "2"));
    bios_proto_destroy (&stats);

    //  1.6 check the maximum value
    stats = cmstats_put (self, "arithmetic_mean", "1", 1, bmsg);
    assert (stats);
    if (verbose)
        bios_proto_print (stats);
    assert (atof (bios_proto_value (stats)) == (100.0+42) / 2);
    assert (streq (bios_proto_aux_string (stats, AGENT_CM_COUNT, NULL), "2"));
    bios_proto_destroy (&bmsg);
    bios_proto_destroy (&stats);

    cmstats_save (self, "src/cmstats.zpl");
    cmstats_destroy (&self);
    self = cmstats_load ("src/cmstats.zpl");

    // TRIVIA: extend the testing of self->stats
    //         hint is - uncomment the print :)
    //cmstats_print (self);
    assert (zhashx_lookup (self->stats, "TYPE_max_1@ELEMENT_SRC"));

    cmstats_delete_dev (self, "ELEMENT_SRC");
    assert (!zhashx_lookup (self->stats, "TYPE_max_1@ELEMENT_SRC"));

    cmstats_destroy (&self);
    unlink (file);
    //  @end
    printf ("OK\n");
}
