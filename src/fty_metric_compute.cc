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
#define AGENT_CONF "/etc/fty-metric-compute/fty-metric-compute.cfg"
static const char* DEFAULT_ENDPOINT = "ipc://@/malamute";

int main(int argc, char* argv[])
{
    bool verbose = false;
    int  argn;

    const char* endpoint   = DEFAULT_ENDPOINT;
    char*       config     = nullptr;
    const char* log_config = "";

    ftylog_setInstance(ACTOR_NAME, log_config);

    for (argn = 1; argn < argc; argn++) {
        if (streq(argv[argn], "--help") || streq(argv[argn], "-h")) {
            puts("fty-metric-compute [options] ...");
            puts("  --verbose / -v         verbose output");
            puts("  --endpoint / -e        malamute endpoint (default ipc://@/malamute)");
            puts("  --help / -h            this information");
            puts("  --config / -c          config file for logging");
            return 0;
        } else if (streq(argv[argn], "--verbose") || streq(argv[argn], "-v"))
            verbose = true;
        else if (streq(argv[argn], "--endpoint") || streq(argv[argn], "-e")) {
            argn += 1;
            if (argc > argn)
                endpoint = reinterpret_cast<const char*>(argv[argn]);
            else {
                log_error("-e/--endpoint expects an argument");
                return 1;
            }
        } else if (streq(argv[argn], "--config") || streq(argv[argn], "-c")) {
            argn += 1;
            if (argc > argn)
                config = argv[argn];
            else {
                log_error("-c/--config expects argument");
                return 1;
            }
        } else {
            printf("Unknown option: %s\n", argv[argn]);
            return 1;
        }
    }

    if (!config) {
        zconfig_t* cfg = zconfig_load(AGENT_CONF);
        if (cfg) {
            log_config = zconfig_get(cfg, "log/config", "/etc/fty/ftylog.cfg");
            ftylog_setConfigFile(ftylog_getInstance(), log_config);
        }
    }

    if (verbose) {
        ftylog_setVeboseMode(ftylog_getInstance());
    }
    log_info("%s - started connected to %s", ACTOR_NAME, endpoint);

    zactor_t* cm_server = zactor_new(fty_mc_server, const_cast<char*>(ACTOR_NAME));
    zstr_sendx(cm_server, "TYPES", "min", "max", "arithmetic_mean", "consumption", nullptr);
    zstr_sendx(cm_server, "STEPS", "15m", "30m", "1h", "8h", "24h", "7d", "30d", nullptr);
    // TODO: Make this configurable, runtime and build-time default
    zstr_sendx(cm_server, "DIR", "/var/lib/fty/fty-metric-compute", nullptr);
    zstr_sendx(cm_server, "CONNECT", endpoint, nullptr);
    // zstr_sendx (cm_server, "PRODUCER", FTY_PROTO_STREAM_METRICS, nullptr);
    zstr_sendx(cm_server, "CONSUMER", FTY_PROTO_STREAM_ASSETS, ".*", nullptr);
    zstr_sendx(cm_server, "CREATE_PULL", nullptr);
    // zstr_sendx (cm_server, "CONSUMER", FTY_PROTO_STREAM_METRICS,
    // "(^realpower.default.*|.*temperature.*|.*humidity.*)", nullptr);

    // src/malamute.c, under MPL license
    while (true) {
        char* message = zstr_recv(cm_server);
        if (message) {
            puts(message);
            zstr_free(&message);
        } else {
            puts("interrupted");
            break;
        }
    }

    zactor_destroy(&cm_server);

    log_info("END: fty_agent_cm is stopped");
    return 0;
}
