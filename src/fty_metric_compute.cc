/*  =========================================================================
    fty-metric-compute - description

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

#include "fty_mc_server.h"
#include <fty_log.h>
#include <fty_proto.h>

#define ACTOR_NAME "fty-metric-compute"

static void usage(const char* pname)
{
    printf("%s [options] ...\n", pname);
    printf("  -v|--verbose    verbose output\n");
    printf("  -e|--endpoint   malamute endpoint (default ipc://@/malamute)\n");
    printf("  -h|--help       show this information\n");
}

int main(int argc, char* argv[])
{
    //defaults
    const char* endpoint = "ipc://@/malamute";
    const char* dir_persist = "/var/lib/fty/fty-metric-compute";
    bool verbose = false;

    for (int argn = 1; argn < argc; argn++) {
        const char* arg = argv[argn];
        const char* param = ((argn + 1) < argc) ? argv[argn + 1] : NULL;

        if (streq(arg, "--help") || streq(arg, "-h")) {
            usage(argv[0]);
            return EXIT_SUCCESS;
        }

        if (streq(arg, "--verbose") || streq(arg, "-v")) {
            verbose = true;
        }
        else if (streq(arg, "--endpoint") || streq(arg, "-e")) {
            if (param) {
                endpoint = param;
                argn++;
            }
            else {
                fprintf(stderr, "%s option expects an argument\n", arg);
                return EXIT_FAILURE;
            }
        }
        else {
            fprintf(stderr, "%s option is unknown\n", arg);
            usage(argv[0]);
            return EXIT_FAILURE;
        }
    }

    ftylog_setInstance(ACTOR_NAME, FTY_COMMON_LOGGING_DEFAULT_CFG);

    if (verbose) {
        ftylog_setVerboseMode(ftylog_getInstance());
    }

    log_info("%s: starting...", ACTOR_NAME);

    zactor_t* mc_server = zactor_new(fty_mc_server, const_cast<char*>(ACTOR_NAME));
    if (!mc_server) {
        log_fatal("%s: mc_server new failed", ACTOR_NAME);
        return EXIT_FAILURE;
    }

    // Should make this configurable, runtime and build-time default
    // TYPES: types of computation from any handled metrics (consumption is specific to power)
    // STEPS: periods of time for the computations

    zstr_sendx(mc_server, "TYPES", "min", "max", "arithmetic_mean", "consumption", nullptr);
    zstr_sendx(mc_server, "STEPS", "15m", "30m", "1h", "8h", "24h", "7d", "30d", nullptr);
    zstr_sendx(mc_server, "DIR", dir_persist, nullptr);
    zstr_sendx(mc_server, "CONNECT", endpoint, nullptr);
    zstr_sendx(mc_server, "CONSUMER", FTY_PROTO_STREAM_ASSETS, ".*", nullptr);
    zstr_sendx(mc_server, "CREATE_PULL", nullptr);

    log_info("%s: started", ACTOR_NAME);

    // Main loop, accept any message back from server
    // copy from src/malamute.c under MPL license
    while (!zsys_interrupted) {
        char* msg = zstr_recv(mc_server);
        if (!msg)
            break;
        log_trace("%s: recv msg '%s'", ACTOR_NAME, msg);
        zstr_free(&msg);
    }

    zactor_destroy(&mc_server);

    log_info("%s: ended", ACTOR_NAME);

    return EXIT_SUCCESS;
}
