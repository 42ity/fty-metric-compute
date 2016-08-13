/*  =========================================================================
    bios_agent_cm - description

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
    bios_agent_cm -
@discuss
@end
*/

#include "agent_cm_classes.h"

static const char* DEFAULT_ENDPOINT = "ipc://@/malamute";

int main (int argc, char *argv [])
{
    bool verbose = false;
    int argn;

    const char *endpoint = DEFAULT_ENDPOINT;

    for (argn = 1; argn < argc; argn++) {
        if (streq (argv [argn], "--help")
        ||  streq (argv [argn], "-h")) {
            puts ("bios-agent-cm [options] ...");
            puts ("  --verbose / -v         verbose test output");
            puts ("  --endpoint / -e        malamute endpoint (default ipc://@/malamute)");
            puts ("  --help / -h            this information");
            return 0;
        }
        else
        if (streq (argv [argn], "--verbose")
        ||  streq (argv [argn], "-v"))
            verbose = true;
        else
        if (streq (argv [argn], "--endpoint")
        ||  streq (argv [argn], "-e")) {
            argn += 1;
            if (argc > argn)
                endpoint = (const char*) argv [argn];
            else {
                zsys_error ("-e/--endpoint expects an argument");
            }
        }
        else {
            printf ("Unknown option: %s\n", argv [argn]);
            return 1;
        }
    }

    if (getenv ("BIOS_LOG_LEVEL")
    && streq (getenv ("BIOS_LOG_LEVEL"), "LOG_DEBUG"))
        verbose = true;

    if (verbose)
        zsys_info ("START: bios_agent_cm - starting at endpoint=%s", endpoint);

    zactor_t *cm_server = zactor_new (bios_cm_server, "bios-agent-cm");
    if (verbose)
        zstr_sendx (cm_server, "VERBOSE", NULL);
    zstr_sendx (cm_server, "TYPES", "min", "max", "arithmetic_mean", NULL);
    zstr_sendx (cm_server, "STEPS", "15m", "30m", "1h", "8h", "24h", "7d", "30d", NULL);
    // TODO: Make this configurable, runtime and build-time default
    zstr_sendx (cm_server, "DIR", "/var/lib/bios/bios-agent-cm/", NULL);
    zstr_sendx (cm_server, "CONNECT", endpoint, "bios-cm-server", NULL);
    zstr_sendx (cm_server, "PRODUCER", BIOS_PROTO_STREAM_METRICS, NULL);
    zstr_sendx (cm_server, "CONSUMER", BIOS_PROTO_STREAM_ASSETS, ".*", NULL);
    zstr_sendx (cm_server, "CONSUMER", BIOS_PROTO_STREAM_METRICS, "(^realpower.default.*|.*temperature.*|.*humidity.*)", NULL);

    // src/malamute.c, under MPL license
    while (true) {
        char *message = zstr_recv (cm_server);
        if (message) {
            puts (message);
            zstr_free (&message);
        }
        else {
            puts ("interrupted");
            break;
        }
    }

    zactor_destroy (&cm_server);
    if (verbose)
        zsys_info ("END: bios_agent_cm is stopped");
    return 0;
}
