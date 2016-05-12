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

//  Structure of our class

struct _bios_cm_server_t {
    int filler;     //  Declare class properties here
};

// convert the time with prefix to number in seconds
// "42" -> 42
// "42s" -> 42
// "42m" -> 2520
static int64_t s_string2secs (const char *step)
{
    assert (step);
    assert (strlen (step) > 0);

    int64_t ret = 0;
    uint32_t times = 1;
    
    char suffix = tolower (step [strlen (step) -1]);

    if (!isdigit (suffix)) {
        switch (suffix) {
            case 's' :
                times = 1;
                break;
            case 'm' :
                times = 60;
                break;
            case 'h' :
                times = 60*60;
                break;
            case 'd' :
                times = 24*60*60;
                break;
            default :
                return -1;
        }
    }

    ret = (int64_t) atoi (step);
    if (ret < 0)
        return -1;
    ret *= times;
    if (ret > UINT32_MAX)
        return -1;

    return ret;

}

//  --------------------------------------------------------------------------
//  Create a new bios_cm_server

void
bios_cm_server (zsock_t *pipe, void *args)
{
    bool verbose = false;
    char *name = strdup (args);
    cmstats_t *stats = cmstats_new ();

    zlist_t *steps = zlist_new ();
    zlist_autofree (steps);
    zlist_t *types = zlist_new ();
    zlist_autofree (types);

    mlm_client_t *client = mlm_client_new ();
    zpoller_t *poller = zpoller_new (pipe, mlm_client_msgpipe (client), NULL);

    zsock_signal (pipe, 0);
    while (!zsys_interrupted)
    {
        void *which = zpoller_wait (poller, -1);

        if (!which)
            break;

        if (which == pipe)
        {
            zmsg_t *msg = zmsg_recv (pipe);
            char *command = zmsg_popstr (msg);
            if (verbose)
                zsys_debug ("%s:\tAPI command=%s", name, command);

            if (streq (command, "$TERM")) {
                zstr_free (&command);
                zmsg_destroy (&msg);
                break;
            }
            else
            if (streq (command, "VERBOSE"))
                verbose=true;
            else
            if (streq (command, "PRODUCER")) {
                char* stream = zmsg_popstr (msg);
                int r = mlm_client_set_producer (client, stream);
                if (r == -1)
                    zsys_error ("%s: can't set producer on stream '%s'", name, stream);
                zstr_free (&stream);
            }
            else
            if (streq (command, "CONSUMER")) {
                char* stream = zmsg_popstr (msg);
                char* pattern = zmsg_popstr (msg);
                int rv = mlm_client_set_consumer (client, stream, pattern);
                if (rv == -1)
                    zsys_error ("%s: can't set consumer on stream '%s', '%s'", name, stream, pattern);
                zstr_free (&pattern);
                zstr_free (&stream);
            }
            else
            if (streq (command, "CONNECT"))
            {
                char *endpoint = zmsg_popstr (msg);
                char *client_name = zmsg_popstr (msg);
                if (!endpoint || !client_name)
                    zsys_error ("%s:\tmissing endpoint or name", name);
                else
                {
                    int r = mlm_client_connect (client, endpoint, 5000, client_name);
                    if (r == -1)
                        zsys_error ("%s:\tConnection to endpoint '%' failed", name);

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
                    if (s_string2secs (foo) != -1) {
                        zlist_append (steps, foo);
                        if (verbose)
                            zsys_debug ("%s:\tadd step='%s'", name, foo);
                    }
                    else {
                        zsys_info ("%s:\tignoring unrecognized step='%s'", name, foo);
                    }
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
                    zlist_append (types, foo);
                    zstr_free (&foo);
                }
            }
            else
                zsys_warning ("%s:\tunkown API command=%s, ignoring", name, command);

            zstr_free (&command);
            zmsg_destroy (&msg);
            continue;
        }

        zmsg_t *msg = mlm_client_recv (client);
        bios_proto_t *bmsg = bios_proto_decode (&msg);

        //TODO: need to know the list of types and or devices to compute
        if (!streq (bios_proto_type (bmsg), "realpower.default"))
            continue;

        for (const char *step = (const char*) zlist_first (steps);
                         step != NULL;
                         step = (const char*) zlist_next (steps))
        {
            for (const char *type = (const char*) zlist_first (types);
                             type != NULL;
                             type = (const char*) zlist_next (types))
            {
                bios_proto_t *stat_msg = cmstats_put (stats, type, (uint32_t) s_string2secs (step), bmsg);
                if (stat_msg) {
                    char *subject;
                    asprintf (&subject, "%s_%s_%s@%s",
                            bios_proto_type (stat_msg),
                            type,
                            step,
                            bios_proto_element_src (stat_msg));

                    zmsg_t *msg = bios_proto_encode (&stat_msg);
                    mlm_client_send (client, subject, &msg);
                    zstr_free (&subject);
                }
            }
        }

        bios_proto_destroy (&bmsg);

    }

    zlist_destroy (&types);
    zlist_destroy (&steps);
    zpoller_destroy (&poller);
    mlm_client_destroy (&client);
    cmstats_destroy (&stats);
    zstr_free (&name);
}



//  --------------------------------------------------------------------------
//  Self test of this class
void
bios_cm_server_test (bool verbose)
{
    printf (" * bios_cm_server: ");

    //  @selftest

    assert (s_string2secs ("42") == 42);
    assert (s_string2secs ("42s") == 42);
    assert (s_string2secs ("42m") == 42*60);
    assert (s_string2secs ("42h") == 42*60*60);
    assert (s_string2secs ("42d") == 42*24*60*60);
    assert (s_string2secs ("42X") == -1);
    assert (s_string2secs ("-42") == -1);
 
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
    zclock_sleep (1200);

    msg = bios_proto_encode_metric (
            NULL,
            "realpower.default",
            "DEV1",
            "1024",
            "UNIT",
            10);
    mlm_client_send (producer, "realpower.default@DEV1", &msg);
    msg = bios_proto_encode_metric (
            NULL,
            "realpower.default",
            "DEV1",
            "42",
            "UNIT",
            10);
    mlm_client_send (producer, "realpower.default@DEV1", &msg);
    
    // now we should have first 1s min/max values published
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
    // send some 1s min/max to differentiate it later
    zclock_sleep (2500);
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

    // consume sent min/max - the unit test for 1s have passed, so simply destroy the values
    // TRIVIA: values ought to be 42 and 1024
    for (int i = 0; i != 2; i++)
    {
        msg = mlm_client_recv (consumer);
        zmsg_destroy (&msg);
    }

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
            assert (streq (bios_proto_value (bmsg), "142"));
        }
        else
        if (streq (type, "min") && streq (step, "5")) {
            assert (streq (mlm_client_subject (consumer), "realpower.default_min_5s@DEV1"));
            assert (streq (bios_proto_value (bmsg), "42"));
        }
        else
        if (streq (type, "max") && streq (step, "1")) {
            assert (streq (mlm_client_subject (consumer), "realpower.default_max_1s@DEV1"));
            assert (streq (bios_proto_value (bmsg), "242"));
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
    mlm_client_destroy (&consumer);
    mlm_client_destroy (&producer);
    zactor_destroy (&server);
    //  @end
    printf ("OK\n");
}
