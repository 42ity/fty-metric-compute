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


//  --------------------------------------------------------------------------
//  Create a new bios_cm_server

void
bios_cm_server (zsock_t *pipe, void *args)
{
    cmstats_t *stats = cmstats_new ();

    // TODO: move to config file
    static const size_t SIZE_LEN = 7;
    static const char* STEPS[] = {"15m", "30m", "1h", "8h", "24h", "7d", "30d"};
    static const uint64_t NSTEPS[] = {15*60, 30*60, 60*60, 8*3600, 24*3600, 7*24*3600, 30*24*3600};
    static const size_t TYPES_LEN = 3;
    static const char* TYPES[] = {"min", "max", "avg"};

    mlm_client_t *client = mlm_client_new ();
    //TODO: move to actor command
    mlm_client_set_consumer (client, BIOS_PROTO_STREAM_METRICS, ".*");
    mlm_client_set_producer (client, BIOS_PROTO_STREAM_METRICS);

    zpoller_t *poller = zpoller_new (pipe, mlm_client_msgpipe (client), NULL);

    while (!zsys_interrupted)
    {
        void *which = zpoller_wait (poller, 0);

        if (!which || which == pipe)
            break;

        zmsg_t *msg = mlm_client_recv (client);
        bios_proto_t *bmsg = bios_proto_decode (&msg);

        //TODO: need to know the list of types and or devices to compute
        if (!streq (bios_proto_type (bmsg), "realpower.default"))
            continue;

        for (size_t i = 0; i != SIZE_LEN; i++)
        {
            for (size_t j = 0; j != TYPES_LEN; j++)
            {
                bios_proto_t *stat_msg = cmstats_put (stats, TYPES [j], NSTEPS [j], bmsg);
                if (stat_msg) {
                    char *subject;
                    asprintf (&subject, "%s_%s_%s@%s",
                            bios_proto_type (stat_msg),
                            TYPES [j],
                            STEPS [i],
                            bios_proto_element_src (stat_msg));

                    zmsg_t *msg = bios_proto_encode (&stat_msg);
                    mlm_client_send (client, subject, &msg);
                }
            }
        }
        bios_proto_destroy (&bmsg);

    }

    zpoller_destroy (&poller);
    mlm_client_destroy (&client);
    cmstats_destroy (&stats);
}



//  --------------------------------------------------------------------------
//  Self test of this class

void
bios_cm_server_test (bool verbose)
{
    printf (" * bios_cm_server: ");

    //  @selftest
    //  TODO: testing
    //  @end
    printf ("OK\n");
}
