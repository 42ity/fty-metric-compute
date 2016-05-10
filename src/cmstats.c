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

struct _cmstats_t {
    zhashx_t *stats;
};

static void
s_destructor (void **self_p)
{
    if (*self_p) {
        bios_proto_destroy ((bios_proto_t**) self_p);
    }
}

static void*
s_duplicator (const void *self)
{
    return (void*) bios_proto_dup ((bios_proto_t*) self);
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

static const char*
s_step2a (uint32_t step)
{
    if (step == 1)
        return "1s";
    if (step == 15*60)
        return "15m";
    return NULL;
}

//  --------------------------------------------------------------------------
// Collect the min value for given step - if the interval is over and new metric
// is already inside the interval, NULL is returned
// Otherwise new bios_proto_t metric is returned. Caller is responsible for
// destroying the value.
//
// TODO:
//  1. rewrite s_step2a somehow!!
//  2. rewrite the actual min implementation as a function pointer
AGENT_CM_EXPORT bios_proto_t *
cmstats_min (cmstats_t *self, const char* type, uint32_t step, bios_proto_t *bmsg)
{
    assert (self);
    assert (type);
    assert (streq (type, "min"));
    assert (bmsg);

    int64_t now = zclock_mono ();

    char *key;
    asprintf (&key, "%s_%s_%s@%s",
            bios_proto_type (bmsg),
            type,
            s_step2a (step),
            bios_proto_element_src (bmsg));

    bios_proto_t *stat_msg = (bios_proto_t*) zhashx_lookup (self->stats, key);

    // handle the first insert
    if (!stat_msg) {
        bios_proto_aux_insert (bmsg, AGENT_CM_TIME, "%"PRIu64, now);
        bios_proto_aux_insert (bmsg, AGENT_CM_COUNT, "1");
        //bios_proto_set_ttl (bmsg, 2 * step);
        zhashx_insert (self->stats, key, bmsg);
        zstr_free (&key);
        return NULL;
    }
    zstr_free (&key);

    // there is already some value
    // so check if it's not already older than we need
    uint64_t stat_now = bios_proto_aux_number (stat_msg, AGENT_CM_TIME, 0);

    // it is, return the stat value and "restart" the computation
    if (now - stat_now >= step * 1000) {
        bios_proto_t *ret = bios_proto_dup (stat_msg);

        bios_proto_aux_insert (stat_msg, AGENT_CM_TIME, "%"PRIu64, now);
        bios_proto_aux_insert (stat_msg, AGENT_CM_COUNT, "1");
        bios_proto_set_value (stat_msg, bios_proto_value (bmsg));

        return ret;
    }

    // if we're inside the interval, simply do the computation
    uint64_t stat_value = atol (bios_proto_value (stat_msg));
    uint64_t bmsg_value = atol (bios_proto_value (bmsg));

    if (bmsg_value < stat_value) {
        bios_proto_set_value (stat_msg, "%"PRIu64, bmsg_value);
    }
    // increase the counter
    bios_proto_aux_insert (stat_msg, AGENT_CM_COUNT, "%"PRIu64, 
        bios_proto_aux_number (stat_msg, AGENT_CM_COUNT, 0) + 1
    );

    return NULL;
}

//  --------------------------------------------------------------------------
//  Self test of this class

void
cmstats_test (bool verbose)
{
    printf (" * cmstats: ");

    //  @selftest
    //  Simple create/destroy test
    cmstats_t *self = cmstats_new ();
    assert (self);

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

    bios_proto_t *stats = cmstats_min (self, "min", 1, bmsg);
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

    stats = cmstats_min (self, "min", 1, bmsg);
    assert (!stats);
    bios_proto_destroy (&bmsg);
    
    zclock_sleep (510);

    //  1.3 third metric (outside interval) in
    msg = bios_proto_encode_metric (
            NULL,
            "TYPE",
            "ELEMENT_SRC",
            "42",
            "UNIT",
            10);
    bmsg = bios_proto_decode (&msg);

    stats = cmstats_min (self, "min", 1, bmsg);
    assert (stats);
    bios_proto_print (stats);
    //  1.4 check the minimal value
    assert (streq (bios_proto_value (stats), "42"));
    assert (streq (bios_proto_aux_string (stats, AGENT_CM_COUNT, NULL), "2"));
    bios_proto_destroy (&stats);

    cmstats_destroy (&self);
    //  @end
    printf ("OK\n");
}
