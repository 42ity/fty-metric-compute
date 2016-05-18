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

// TODO: move to class sometime ???
typedef struct _cm_t {
    bool verbose; // is server verbose or not
    char *name;   // server name
    cmstats_t *stats; // statistics (min, max, averages, ...)
    cmsteps_t *steps; // info about steps
    zlist_t *types; // info about types to compute
    mlm_client_t *client; // malamute client
    char *filename; // state file
} cm_t;

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
    zsock_signal (pipe, 0);
    while (!zsys_interrupted)
    {
        int interval = -1;
        if (cmsteps_gcd (self->steps) != 0) {
            int64_t now = zclock_mono ();
            // find next nearest interval to compute some average
            interval = now - (now % -cmsteps_gcd (self->steps)) + 15;
        }

        void *which = zpoller_wait (poller, interval);

        if (!which && zpoller_terminated (poller))
            break;

        if (!which && zpoller_expired (poller)) {
            if (self->verbose)
                zsys_debug ("%s:\tpolling interval expired, calling cmstats_poll");
            cmstats_poll (self->stats, self->client, zclock_mono (), self->verbose);
	    
            if (self->filename) {
                int r = cmstats_save (self->stats, self->filename);
                if (r == -1)
                    zsys_error ("%s:\t failed to save %s: %s", self->name, self->filename, strerror (errno));
                else
                    if (self->verbose)
                        zsys_info ("%s:\t'%s' saved succesfully", self->name, self->filename);
            }
            continue;
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
            if (streq (command, "VERBOSE"))
                self->verbose=true;
            else
            if (streq (command, "DIR")) {
                char* dir = zmsg_popstr (msg);
                zfile_t *f = zfile_new (dir, "state.zpl");
                self->filename = strdup (zfile_filename (f, NULL));

                if (zfile_exists (self->filename)) {
                    cmstats_t *foo = cmstats_load (self->filename);
                    if (!foo)
                        zsys_error ("%s:\tFailed to load %s", self->name, self->filename);
                    else {
                        if (self->verbose)
                            zsys_info ("%s:\tLoaded %s", self->name, self->filename);
                        cmstats_destroy (&self->stats);
                        self->stats = foo;
                    }
                }

                zfile_destroy (&f);
                zstr_free (&dir);
            }
            else
            if (streq (command, "PRODUCER")) {
                char* stream = zmsg_popstr (msg);
                int r = mlm_client_set_producer (self->client, stream);
                if (r == -1)
                    zsys_error ("%s: can't set producer on stream '%s'", self->name, stream);
                zstr_free (&stream);
            }
            else
            if (streq (command, "CONSUMER")) {
                char* stream = zmsg_popstr (msg);
                char* pattern = zmsg_popstr (msg);
                int rv = mlm_client_set_consumer (self->client, stream, pattern);
                if (rv == -1)
                    zsys_error ("%s: can't set consumer on stream '%s', '%s'", self->name, stream, pattern);
                zstr_free (&pattern);
                zstr_free (&stream);
            }
            else
            if (streq (command, "CONNECT"))
            {
                char *endpoint = zmsg_popstr (msg);
                char *client_name = zmsg_popstr (msg);
                if (!endpoint || !client_name)
                    zsys_error ("%s:\tmissing endpoint or name", self->name);
                else
                {
                    int r = mlm_client_connect (self->client, endpoint, 5000, client_name);
                    if (r == -1)
                        zsys_error ("%s:\tConnection to endpoint '%' failed", self->name);

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
                        zsys_info ("%s:\tignoring unrecognized step='%s'", self->name, foo);
                    zstr_free (&foo);
                }
            }
            else
            if (streq (command, "TYPES"))
            {
                for (;;)
                {
                    char *foo = zmsg_popstr (msg);
                    if (!foo)
                        break;
                    zlist_append (self->types, foo);
                    zstr_free (&foo);
                }
            }
            else
            if (streq (command, "FILENAME"))
            {
                char *filename = zmsg_popstr (msg);
                if (!filename)
                    zsys_error ("%s:\tmissing filename argument", self->name);
                if (self->verbose)
                    zsys_debug ("%s:\tfilename=%s", self->name, filename);
                zstr_free (&self->filename);
                self->filename = strdup (filename);
                zstr_free (&filename);
            }
            else
                zsys_warning ("%s:\tunkown API command=%s, ignoring", self->name, command);

            zstr_free (&command);
            zmsg_destroy (&msg);
            continue;
        }

        zmsg_t *msg = mlm_client_recv (self->client);
        bios_proto_t *bmsg = bios_proto_decode (&msg);

        if (streq (mlm_client_address (self->client), BIOS_PROTO_STREAM_ASSETS)) {
            const char *op = bios_proto_operation (bmsg);
            if (streq (op, "delete")
            ||  streq (op, "retire"))
                cmstats_delete_dev (self->stats, bios_proto_name (bmsg));

            bios_proto_destroy (&bmsg);
            continue;
        }

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
                    mlm_client_send (self->client, subject, &msg);
                    zstr_free (&subject);
                }
            }
        }

        bios_proto_destroy (&bmsg);

    }

    if (self->filename) {
        int r = cmstats_save (self->stats, self->filename);
        if (r == -1)
            zsys_error ("%s:\t failed to save %s: %s", self->name, self->filename, strerror (errno));
        else
            if (self->verbose)
                zsys_info ("%s:\t'%s' saved succesfully", self->name, self->filename);
    }

    cm_destroy (&self);
    zpoller_destroy (&poller);
}



//  --------------------------------------------------------------------------
//  Self test of this class
void
bios_cm_server_test (bool verbose)
{
    printf (" * bios_cm_server: ");

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

    mlm_client_t *consumer = mlm_client_new ();
    mlm_client_connect (consumer, endpoint, 5000, "consumer");
    mlm_client_set_consumer (consumer, BIOS_PROTO_STREAM_METRICS, ".*(min|max).*");

    zactor_t *cm_server = zactor_new (bios_cm_server, "bios-cm-server");
    if (verbose)
        zstr_sendx (cm_server, "VERBOSE", NULL);
    zstr_sendx (cm_server, "TYPES", "min", "max", NULL);
    zstr_sendx (cm_server, "STEPS", "1s", "5s", NULL);
    zstr_sendx (cm_server, "DIR", "src", NULL);
    zstr_sendx (cm_server, "CONNECT", endpoint, "bios-cm-server", NULL);
    zstr_sendx (cm_server, "PRODUCER", BIOS_PROTO_STREAM_METRICS, NULL);
    zstr_sendx (cm_server, "CONSUMER", BIOS_PROTO_STREAM_METRICS, ".*", NULL);
    zclock_sleep (500);

    zmsg_t *msg = bios_proto_encode_metric (
            NULL,
            "realpower.default",
            "DEV1",
            "100",
            "UNIT",
            10);
    mlm_client_send (producer, "realpower.default@DEV1", &msg);
    zclock_sleep (500);

    msg = bios_proto_encode_metric (
            NULL,
            "realpower.default",
            "DEV1",
            "50",
            "UNIT",
            10);
    mlm_client_send (producer, "realpower.default@DEV1", &msg);

    // now we should have first 1s min/max values published - from polling
    for (int i = 0; i != 2; i++) {
        bios_proto_t *bmsg = NULL;
        msg = mlm_client_recv (consumer);
        bmsg = bios_proto_decode (&msg);

        const char *type = bios_proto_aux_string (bmsg, AGENT_CM_TYPE, "");
        if (streq (type, "min")) {
            assert (streq (mlm_client_subject (consumer), "realpower.default_min_1s@DEV1"));
            assert (streq (bios_proto_value (bmsg), "50"));
        }
        else
        if (streq (type, "max")) {
            assert (streq (mlm_client_subject (consumer), "realpower.default_max_1s@DEV1"));
            assert (streq (bios_proto_value (bmsg), "100"));
        }
        else
            assert (false);

        bios_proto_destroy (&bmsg);
    }

    zclock_sleep (2500);
    // consume sent min/max - the unit test for 1s have
    for (int i = 0; i != 2; i++)
    {
        msg = mlm_client_recv (consumer);
        bios_proto_t *bmsg = bios_proto_decode (&msg);
        assert (bios_proto_value (bmsg)[0] == '0');
        bios_proto_destroy (&bmsg);
    }

    // send some 1s min/max to differentiate it later
    msg = bios_proto_encode_metric (
            NULL,
            "realpower.default",
            "DEV1",
            "142",
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

    // trigger the 5s now
    zclock_sleep (3000);
    msg = bios_proto_encode_metric (
            NULL,
            "realpower.default",
            "DEV1",
            "11",
            "UNIT",
            10);
    mlm_client_send (producer, "realpower.default@DEV1", &msg);
    
    zclock_sleep(500);

    // now we have 1s and 5s min/max as well
    for (int i = 0; i != 4; i++) {
        bios_proto_t *bmsg = NULL;
        msg = mlm_client_recv (consumer);
        bmsg = bios_proto_decode (&msg);

        if (verbose) {
            zsys_debug ("subject=%s", mlm_client_subject (consumer));
            bios_proto_print (bmsg);
        }

        const char *type = bios_proto_aux_string (bmsg, AGENT_CM_TYPE, "");
        const char *step = bios_proto_aux_string (bmsg, AGENT_CM_STEP, "");

        if (streq (type, "min") && streq (step, "1")) {
            assert (streq (mlm_client_subject (consumer), "realpower.default_min_1s@DEV1"));

            bool foo = streq (bios_proto_value (bmsg), "142") || bios_proto_value (bmsg) [0];
            assert (foo);

        }
        else
        if (streq (type, "min") && streq (step, "5")) {
            assert (streq (mlm_client_subject (consumer), "realpower.default_min_5s@DEV1"));
            assert (streq (bios_proto_value (bmsg), "42"));
        }
        else
        if (streq (type, "max") && streq (step, "1")) {
            assert (streq (mlm_client_subject (consumer), "realpower.default_max_1s@DEV1"));

            bool foo = streq (bios_proto_value (bmsg), "142") || bios_proto_value (bmsg) [0];
            assert (foo);
        }
        else
        if (streq (type, "max") && streq (step, "5")) {
            assert (streq (mlm_client_subject (consumer), "realpower.default_max_5s@DEV1"));
            assert (streq (bios_proto_value (bmsg), "1024"));
        }
        else
            assert (false);

        bios_proto_destroy (&bmsg);
    }

    zactor_destroy (&cm_server);

    // to prevent false positives in memcheck - there should not be any messages in a broker
    // on the end of the run
    zpoller_t *poller = zpoller_new (mlm_client_msgpipe (consumer), mlm_client_msgpipe (producer), NULL);
    while (!zsys_interrupted) {
        void *which = zpoller_wait (poller, 1000);

        if (!which)
            break;
        
        msg = mlm_client_recv (consumer);
        zmsg_destroy (&msg);
    }
    zpoller_destroy (&poller);

    mlm_client_destroy (&consumer);
    mlm_client_destroy (&producer);
    zactor_destroy (&server);

    assert (zfile_exists ("src/state.zpl"));
    unlink ("src/state.zpl");
    //  @end
    printf ("OK\n");
}
