/*  =========================================================================
    bios_cm_server - Computation server implementation

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
    bios_cm_server - Computation server implementation
@discuss
@end
*/

#include "agent_cm_classes.h"

// TODO: move to class sometime
// It is a "CM" entity
typedef struct _cm_t {
    bool verbose;           // is server verbose or not
    char *name;             // server name
    cmstats_t *stats;       // computed statictics for all types and steps
    cmsteps_t *steps;       // info about supported steps
    zlist_t *types;         // info about supported statistic types (min, max, avg)
    mlm_client_t *client;   // malamute client
    char *filename;         // state file name
} cm_t;

/*
 * \brief Create new empty not verbose "CM" entity
 */
cm_t*
cm_new (const char* name)
{
    assert (name);
    cm_t *self = (cm_t*) zmalloc (sizeof (cm_t));
    assert (self);

    self->verbose = false;

    self->name = strdup (name);
    assert (self->name);

    self->stats = cmstats_new ();
    assert (self->stats);

    self->steps = cmsteps_new ();
    assert (self->steps);

    self->types = zlist_new ();
    assert (self->types);
    zlist_autofree (self->types);

    self->client = mlm_client_new ();
    assert (self->client);

    return self;
}

/*
 * \brief Destroy the "CM" entity
 */
void
cm_destroy (cm_t **self_p)
{
    if (*self_p)
    {
        cm_t *self = *self_p;

        // free structure items
        mlm_client_destroy (&self->client);
        zlist_destroy (&self->types);
        cmsteps_destroy (&self->steps);
        cmstats_destroy (&self->stats);
        zstr_free (&self->name);
        zstr_free (&self->filename);

        // free structure itself
        free (self);
        *self_p = NULL;
    }
}

//  --------------------------------------------------------------------------
//  bios_cm_server actor

void
bios_cm_server (zsock_t *pipe, void *args)
{

    cm_t *self = cm_new ((const char*) args);

    zpoller_t *poller = zpoller_new (pipe, mlm_client_msgpipe (self->client), NULL);

    // do not forget to send a signal to actor :)
    zsock_signal (pipe, 0);

    // Time in [ms] when last cmstats_poll was called
    // -1 means it was never called yet
    int64_t last_poll_ms = -1;
    while (!zsys_interrupted)
    {
        // What time left before publishing?
        // If steps where not defined ( cmsteps_gcd == 0 ) then nothing to publish,
        // so, we can wait forever (-1) for first message to come
        // in [ms]
        int interval_ms = -1;
        if (cmsteps_gcd (self->steps) != 0) {
            // So, some steps where defined

            // What is the "now" time in [s]
            int64_t now_s = zclock_time () / 1000;

            // Compute the left border of the interval:
            // length_of_the_minimal_interval - part_of_interval_already_passed
            interval_ms = (cmsteps_gcd (self->steps) - (now_s % cmsteps_gcd (self->steps))) * 1000;
            if (self->verbose)
                zsys_debug ("%s:\tnow=%"PRIu64 "s, cmsteps_gcd=%"PRIu32 "s, interval=%dms",
                        self->name,
                        now_s,
                        cmsteps_gcd (self->steps),
                        interval_ms
                        );
        }

        // wait for interval left
        void *which = zpoller_wait (poller, interval_ms);

        if (!which && zpoller_terminated (poller))
            break;

        // poll when zpoller expired
        // the second condition necessary
        //
        //  X2 is an expected moment when some metrics should be published
        //  in t=X1 message comes, its processing takes time and cycle will begin from the beginning
        //  at moment X4, but in X3 some message had already come -> so zpoller_expired(poller) == false 
        //
        // -NOW----X1------X2---X3--X4-----
        if ((!which && zpoller_expired (poller))
        ||  (last_poll_ms > 0 &&  ( (zclock_time () - last_poll_ms) > cmsteps_gcd (self->steps) * 1000 )) ) {

            if (self->verbose) {
                if (zpoller_expired (poller))
                    zsys_debug ("%s:\tzpoller_expired, calling cmstats_poll", self->name);
                else
                    zsys_debug ("%s:\ttime (not zpoller) expired, calling cmstats_poll", self->name);
            }
            // Publish metrics and reset the computation where needed 
            cmstats_poll (self->stats, self->client, self->verbose);
            // State is saved every time, when something is published
            // Something is published every "steps_gcd" interval
            // In the most of the cases (steps_gcd = "minimal_interval")
            if (self->filename) {
                int r = cmstats_save (self->stats, self->filename);
                if (r == -1)
                    zsys_error ("%s:\tFailed to save %s: %s", self->name, self->filename, strerror (errno));
                else
                    if (self->verbose)
                        zsys_info ("%s:\t'%s' saved succesfully", self->name, self->filename);
            }
            // Record the time, when something was published last time
            last_poll_ms = zclock_time ();
            // if poller expired, we can continue in order to wait for new message
            if ( !which && zpoller_expired (poller) )
                continue;
            // else if poller not expired, we need to process the message!!
        }

        if (which == pipe)
        {
            zmsg_t *msg = zmsg_recv (pipe);
            char *command = zmsg_popstr (msg);
            if (self->verbose)
                zsys_debug ("%s:\tAPI command=%s", self->name, command);

            if (streq (command, "$TERM")) {
                zstr_free (&command);
                zmsg_destroy (&msg);
                break;
            }
            else
            if (streq (command, "VERBOSE")) {
                self->verbose = true;
                zsys_debug ("%s:\tVERBOSE", self->name);
            }
            else
            if (streq (command, "DIR")) {
                char* dir = zmsg_popstr (msg);
                zfile_t *f = zfile_new (dir, "state.zpl");
                self->filename = strdup (zfile_filename (f, NULL));

                if (zfile_exists (self->filename)) {
                    cmstats_t *foo = cmstats_load (self->filename);
                    if (!foo)
                        zsys_error ("%s:\tFailed to load '%s'", self->name, self->filename);
                    else {
                        if (self->verbose)
                            zsys_info ("%s:\tLoaded '%s'", self->name, self->filename);
                        cmstats_destroy (&self->stats);
                        self->stats = foo;
                    }
                } else {
                    zsys_info ("%s:\tState file '%s' doesn't exists", self->name, self->filename);
                }

                zfile_destroy (&f);
                zstr_free (&dir);
            }
            else
            if (streq (command, "PRODUCER")) {
                char* stream = zmsg_popstr (msg);
                int r = mlm_client_set_producer (self->client, stream);
                if (r == -1)
                    zsys_error ("%s:\tCan't set producer on stream '%s'", self->name, stream);
                zstr_free (&stream);
            }
            else
            if (streq (command, "CONSUMER")) {
                char* stream = zmsg_popstr (msg);
                char* pattern = zmsg_popstr (msg);
                int rv = mlm_client_set_consumer (self->client, stream, pattern);
                if (rv == -1)
                    zsys_error ("%s:\tCan't set consumer on stream '%s', '%s'", self->name, stream, pattern);
                zstr_free (&pattern);
                zstr_free (&stream);
            }
            else
            if (streq (command, "CONNECT"))
            {
                char *endpoint = zmsg_popstr (msg);
                char *client_name = zmsg_popstr (msg);
                if (!endpoint || !client_name)
                    zsys_error ("%s:\tMissing endpoint or name", self->name);
                else
                {
                    int r = mlm_client_connect (self->client, endpoint, 5000, client_name);
                    if (r == -1)
                        zsys_error ("%s:\tConnection to endpoint '%s' failed", self->name);

                }
                zstr_free (&client_name);
                zstr_free (&endpoint);
            }
            else
            if (streq (command, "STEPS"))
            {
                for (;;)
                {
                    char *foo = zmsg_popstr (msg);
                    if (!foo)
                        break;
                    int r = cmsteps_put (self->steps, foo);
                    if (r == -1)
                        zsys_info ("%s:\tIgnoring unrecognized step='%s'", self->name, foo);
                    zstr_free (&foo);
                }
            }
            else
            if (streq (command, "TYPES"))
            {
                for (;;)
                {
                    char *foo = zmsg_popstr (msg);
                    // TODO: may be we need some check here for supported types
                    if (!foo)
                        break;
                    zlist_append (self->types, foo);
                    zstr_free (&foo);
                }
            }
            else
                zsys_warning ("%s:\tUnkown API command=%s, ignoring", self->name, command);

            zstr_free (&command);
            zmsg_destroy (&msg);
            continue;
        } // end of comand pipe processing

        zmsg_t *msg = mlm_client_recv (self->client);
        if ( !msg ) {
            zsys_error ("%s:\tmlm_client_recv() == NULL", self->name);
            continue;
        }
        bios_proto_t *bmsg = bios_proto_decode (&msg);

        // If we received an asset message 
        // * "delete" or "retire"  -> drop all computations on that asset 
        // *  other                -> ignore it, as it doesn't impact this agent
        if ( bios_proto_id (bmsg) == BIOS_PROTO_ASSET ) {
            const char *op = bios_proto_operation (bmsg);
            if (streq (op, "delete")
            ||  streq (op, "retire"))
                cmstats_delete_asset (self->stats, bios_proto_name (bmsg));

            bios_proto_destroy (&bmsg);
            continue;
        }

        // If we received a metric message
        // update statistics for all steps and types
        // All statistics are computed for "left side of the interval"
        if ( bios_proto_id (bmsg) == BIOS_PROTO_METRIC ) {
            for (uint32_t *step_p = cmsteps_first (self->steps);
                    step_p != NULL;
                    step_p = cmsteps_next (self->steps))
            {
                for (const char *type = (const char*) zlist_first (self->types);
                        type != NULL;
                        type = (const char*) zlist_next (self->types))
                {
                    const char *step = (const char*) cmsteps_cursor (self->steps);
                    bios_proto_t *stat_msg = cmstats_put (self->stats, type, step, *step_p, bmsg);
                    if (stat_msg) {
                        char *subject = zsys_sprintf ("%s@%s",
                                bios_proto_type (stat_msg),
                                bios_proto_element_src (stat_msg));
                        assert (subject);

                        zmsg_t *msg = bios_proto_encode (&stat_msg);
                        int r = mlm_client_send (self->client, subject, &msg);
                        if ( r == -1 ) {
                            zsys_error ("%s:\tCannot publish statistics", self->name);
                        }
                        zstr_free (&subject);
                    }
                }
            }
            bios_proto_destroy (&bmsg);
            continue;
        }

        // We received some unexpected message
        zsys_warning ("%s:\tUnexpected message from sender=%s, subject=%s",
                self->name, mlm_client_sender(self->client), mlm_client_subject(self->client));

        bios_proto_destroy (&bmsg);
    }
    // end of main loop, so we are going to die soon

    if (self->filename) {
        int r = cmstats_save (self->stats, self->filename);
        if (r == -1)
            zsys_error ("%s:\tFailed to save '%s': %s", self->name, self->filename, strerror (errno));
        else
            if (self->verbose)
                zsys_info ("%s:\tSaved succesfully '%s'", self->name, self->filename);
    }

    cm_destroy (&self);
    zpoller_destroy (&poller);
}



//  --------------------------------------------------------------------------
//  Self test of this class
void
bios_cm_server_test (bool verbose)
{
    printf (" * bios_cm_server:");
    if (verbose)
        printf ("\n");

    //  @selftest
    unlink ("src/state.zpl");

    static const char *endpoint = "inproc://cm-server-test";

    // create broker
    zactor_t *server = zactor_new (mlm_server, "Malamute");
    if (verbose)
        zstr_sendx (server, "VERBOSE", NULL);
    zstr_sendx (server, "BIND", endpoint, NULL);

    mlm_client_t *producer = mlm_client_new ();
    mlm_client_connect (producer, endpoint, 5000, "publisher");
    mlm_client_set_producer (producer, BIOS_PROTO_STREAM_METRICS);

    // 1s consumer
    mlm_client_t *consumer_1s = mlm_client_new ();
    mlm_client_connect (consumer_1s, endpoint, 5000, "consumer_1s");
    mlm_client_set_consumer (consumer_1s, BIOS_PROTO_STREAM_METRICS, ".*(min|max|arithmetic_mean)_1s.*");

    // 5s consumer
    mlm_client_t *consumer_5s = mlm_client_new ();
    mlm_client_connect (consumer_5s, endpoint, 5000, "consumer_5s");
    mlm_client_set_consumer (consumer_5s, BIOS_PROTO_STREAM_METRICS, ".*(min|max|arithmetic_mean)_5s.*");

    zactor_t *cm_server = zactor_new (bios_cm_server, "bios-cm-server");
    if (verbose)
        zstr_sendx (cm_server, "VERBOSE", NULL);
    zstr_sendx (cm_server, "TYPES", "min", "max", "arithmetic_mean", NULL);
    zstr_sendx (cm_server, "STEPS", "1s", "5s", NULL);
    zstr_sendx (cm_server, "DIR", "src", NULL);
    zstr_sendx (cm_server, "CONNECT", endpoint, "bios-cm-server", NULL);
    zstr_sendx (cm_server, "PRODUCER", BIOS_PROTO_STREAM_METRICS, NULL);
    zstr_sendx (cm_server, "CONSUMER", BIOS_PROTO_STREAM_METRICS, ".*", NULL);
    zclock_sleep (500);
    
    //XXX: the test is sensitive on timing!!!
    //     so it must start at the beggining of the fifth second in minute (00, 05, 10, ... 55)
    //     other option is to not test in second precision,
    //     which will increase the time of make check far beyond
    //     what developers would accept ;-)
    {
        int64_t now_ms = zclock_time ();
        int64_t sl = 5000 - (now_ms % 5000);
        zclock_sleep (sl);
        if (verbose)
            zsys_debug ("now_ms=%"PRIi64 ", sl=%"PRIi64 ", now=%"PRIi64,
                now_ms,
                sl,
                zclock_time ());
    }

    int64_t TEST_START_MS = zclock_time ();
    if (verbose)
        zsys_debug ("TEST_START_MS=%"PRIi64, TEST_START_MS);

    zmsg_t *msg = bios_proto_encode_metric (
            NULL,
            "realpower.default",
            "DEV1",
            "100",
            "UNIT",
            10);
    mlm_client_send (producer, "realpower.default@DEV1", &msg);

    msg = bios_proto_encode_metric (
            NULL,
            "realpower.default",
            "DEV1",
            "50",
            "UNIT",
            10);
    mlm_client_send (producer, "realpower.default@DEV1", &msg);

    // T+1100ms
    zclock_sleep (5000 - (zclock_time () - TEST_START_MS) - 3900);

    // now we should have first 1s min/max/avg values published - from polling
    for (int i = 0; i != 3; i++) {
        bios_proto_t *bmsg = NULL;
        msg = mlm_client_recv (consumer_1s);
        bmsg = bios_proto_decode (&msg);

        if (verbose) {
            zsys_debug ("subject=%s", mlm_client_subject (consumer_1s));
            bios_proto_print (bmsg);
        }

        const char *type = bios_proto_aux_string (bmsg, AGENT_CM_TYPE, "");
        if (streq (type, "min")) {
            assert (streq (mlm_client_subject (consumer_1s), "realpower.default_min_1s@DEV1"));
            assert (streq (bios_proto_value (bmsg), "50.00"));
        }
        else
        if (streq (type, "max")) {
            assert (streq (mlm_client_subject (consumer_1s), "realpower.default_max_1s@DEV1"));
            assert (streq (bios_proto_value (bmsg), "100"));
        }
        else
        if (streq (type, "arithmetic_mean")) {
            assert (streq (mlm_client_subject (consumer_1s), "realpower.default_arithmetic_mean_1s@DEV1"));
            assert (streq (bios_proto_value (bmsg), "75.00"));
        }
        else
            assert (false);

        bios_proto_destroy (&bmsg);
    }

    // goto T+3100ms
    zclock_sleep (5000 - (zclock_time () - TEST_START_MS) - 1900);
    // send some 1s min/max to differentiate the 1s and 5s min/max later on
    msg = bios_proto_encode_metric (
            NULL,
            "realpower.default",
            "DEV1",
            "42",
            "UNIT",
            10);
    mlm_client_send (producer, "realpower.default@DEV1", &msg);
    msg = bios_proto_encode_metric (
            NULL,
            "realpower.default",
            "DEV1",
            "242",
            "UNIT",
            10);
    mlm_client_send (producer, "realpower.default@DEV1", &msg);

    // goto T+4600
    zclock_sleep (5000 - (zclock_time () - TEST_START_MS) - 400);
    // consume sent min/max/avg - the unit test for 1s have
    // there are 3 mins, 3 max and 3 arithmetic_mean published so far
    for (int i = 0; i != 9; i++)
    {
        msg = mlm_client_recv (consumer_1s);
        bios_proto_t *bmsg = bios_proto_decode (&msg);
        if (verbose) {
            zsys_debug ("subject=%s", mlm_client_subject (consumer_1s));
            bios_proto_print (bmsg);
        }
        /* It is not reliable under memcheck, because of timeing 
        static const char* values[] = {"0", "42.000000", "242.000000", "142.000000"};
        bool test = false;
        for (int j =0; j < sizeof (values); j++)
        {
            test = streq (values [j], bios_proto_value (bmsg));
            if (test) {
                break;
            }
        }
        // ATTENTION: test == false , then make chekc will write "Segmentation fault"
        // instead of "Assertion failed"
        assert (test == true);
        */
        bios_proto_destroy (&bmsg);
    }

    // T+5100s
    zclock_sleep (5000 - (zclock_time () - TEST_START_MS) + 100);

    // now we have 2 times 1s and 5s min/max as well
    for (int i = 0; i != 3; i++) {
        bios_proto_t *bmsg = NULL;
        msg = mlm_client_recv (consumer_5s);
        bmsg = bios_proto_decode (&msg);

        if (verbose) {
            zsys_debug ("zclock_time=%"PRIi64 "ms", zclock_time ());
            zsys_debug ("subject=%s", mlm_client_subject (consumer_5s));
            bios_proto_print (bmsg);
        }

        const char *type = bios_proto_aux_string (bmsg, AGENT_CM_TYPE, "");

        if (streq (type, "min")) {
            assert (streq (mlm_client_subject (consumer_5s), "realpower.default_min_5s@DEV1"));
            assert (streq (bios_proto_value (bmsg), "42.00"));
        }
        else
        if (streq (type, "max")) {
            assert (streq (mlm_client_subject (consumer_5s), "realpower.default_max_5s@DEV1"));
            assert (streq (bios_proto_value (bmsg), "242.00"));
        }
        else
        if (streq (type, "arithmetic_mean")) {
            assert (streq (mlm_client_subject (consumer_5s), "realpower.default_arithmetic_mean_5s@DEV1"));
            // (100 + 50 + 42 + 242) / 5
            assert (streq (bios_proto_value (bmsg), "108.50"));
        }
        else
            assert (false);

        bios_proto_destroy (&bmsg);
    }
    zactor_destroy (&cm_server);
    zclock_sleep (500);

    // to prevent false positives in memcheck - there should not be any messages in a broker
    // on the end of the run
    zpoller_t *poller = zpoller_new (mlm_client_msgpipe (consumer_5s), mlm_client_msgpipe (consumer_1s), NULL);
    while (!zsys_interrupted) {
        void *which = zpoller_wait (poller, 500);

        if (!which)
            break;
        else
        if (which == mlm_client_msgpipe (consumer_1s))
            msg = mlm_client_recv (consumer_1s);
        else
        if (which == mlm_client_msgpipe (consumer_5s))
            msg = mlm_client_recv (consumer_5s);

        zmsg_destroy (&msg);
    }
    zpoller_destroy (&poller);

    zactor_destroy (&cm_server);
    mlm_client_destroy (&consumer_5s);
    mlm_client_destroy (&consumer_1s);
    mlm_client_destroy (&producer);
    zactor_destroy (&server);

    assert (zfile_exists ("src/state.zpl"));
    unlink ("src/state.zpl");
    //  @end
    printf ("OK\n");
}
