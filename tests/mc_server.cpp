#include "src/cmstats.h"
#include "src/cmsteps.h"
#include "src/fty_mc_server.h"
#include <catch2/catch.hpp>
#include <fty_shm.h>
#include <malamute.h>

static void wait_time(int64_t start_ms, int time_s, bool verbose = false) {
    int64_t now_ms = zclock_time();
    int64_t delay = start_ms + (time_s * 1000) - now_ms;
    if (verbose) printf("-----> Wait %ds. completion (%dms. delay)\n", time_s, int(delay));
    if (delay > 0) {
        zclock_sleep(int(delay));
    }
}

//"min", "max", "arithmetic_mean"
TEST_CASE("mc_server_min_max_mean", "[mc_server][min][max][mean]")
{
    const bool verbose = true;
    const char* endpoint = "inproc://cm-server-test";

    CHECK(fty_shm_set_test_dir(".") == 0);

    unlink("state.zpl");

    fty_shm_set_default_polling_interval(2);

    // create broker
    zactor_t* server = zactor_new(mlm_server, const_cast<char*>("Malamute"));
    REQUIRE(server);
    zstr_sendx(server, "BIND", endpoint, nullptr);

    zactor_t* mc_server = zactor_new(fty_mc_server, const_cast<char*>("fty-mc-server-test"));
    REQUIRE(mc_server);
    zstr_sendx(mc_server, "TYPES", "min", "max", "arithmetic_mean", nullptr);
    zstr_sendx(mc_server, "STEPS", "10s", "50s", nullptr);
    zstr_sendx(mc_server, "DIR", ".", nullptr);
    zstr_sendx(mc_server, "CONNECT", endpoint, nullptr);
    zstr_sendx(mc_server, "CREATE_PULL", nullptr);

    zclock_sleep(500);

    // XXX: the test is sensitive on timing!!!
    //     so it must start at the beggining of the fifth second in minute (00, 05, 10, ... 55)
    //     other option is to not test in second precision,
    //     which will increase the time of make check far beyond
    //     what developers would accept ;-)
    //     (in order to use shm, all timer for this test as set to x*10)
    {
        int64_t now_ms = zclock_time();
        int64_t delay  = 50000 - (now_ms % 50000);
        if (verbose) printf("-----> start delayed for %dms.\n", int(delay));
        zclock_sleep(int(delay));
    }

    if (verbose) printf("-----> start\n");
    int64_t start_ms = zclock_time();

    // 100
    CHECK(fty::shm::write_metric("DEV1", "realpower.default", "100", "UNIT", 20) == 0);
    zclock_sleep(2000);

    // empty element_src
    fty::shm::write_metric("", "realpower.default", "20", "UNIT", 2);
    zclock_sleep(2000);

    // NaN value
    fty::shm::write_metric("DEV1", "realpower.default", "nan", "UNIT", 2);
    zclock_sleep(2000);

    // 50
    CHECK(fty::shm::write_metric("DEV1", "realpower.default", "50", "UNIT", 2) == 0);
    zclock_sleep(2000);

    // T+11s
    //zclock_sleep(int(50000 - (zclock_time() - TEST_START_MS) - 39000));
    wait_time(start_ms, 11, verbose);

    // now we should have first 1s min/max/avg values published - from polling
    {
        fty_proto_t* bmsg = nullptr;
        fty::shm::read_metric("DEV1", "realpower.default_min_10s", &bmsg);
        const char* type = fty_proto_aux_string(bmsg, AGENT_CM_TYPE, "");
        CHECK(streq(type, "min"));
        CHECK(streq(fty_proto_value(bmsg), "50.00"));
        fty_proto_destroy(&bmsg);
    }
    {
        fty_proto_t* bmsg = nullptr;
        fty::shm::read_metric("DEV1", "realpower.default_max_10s", &bmsg);
        const char* type = fty_proto_aux_string(bmsg, AGENT_CM_TYPE, "");
        CHECK(streq(type, "max"));
        CHECK(streq(fty_proto_value(bmsg), "100"));
        fty_proto_destroy(&bmsg);
    }
    {
        fty_proto_t* bmsg = nullptr;
        fty::shm::read_metric("DEV1", "realpower.default_arithmetic_mean_10s", &bmsg);
        const char* type = fty_proto_aux_string(bmsg, AGENT_CM_TYPE, "");
        CHECK(streq(type, "arithmetic_mean"));
        CHECK(streq(fty_proto_value(bmsg), "75.00")); // (50 + 100) / 2
        fty_proto_destroy(&bmsg);
    }

    // T+31s
    wait_time(start_ms, 31, verbose);

    // send some 10s min/max to differentiate the 10s and 50s min/max later on

    fty::shm::write_metric("DEV1", "realpower.default", "42", "UNIT", 2);
    zclock_sleep(2000);

    fty::shm::write_metric("DEV1", "realpower.default", "242", "UNIT", 2);
    zclock_sleep(2000);

    // T+51s
    wait_time(start_ms, 51, verbose);

    // now we have 2 times 10s and 50s min/max as well
    {
        fty_proto_t* bmsg = nullptr;
        fty::shm::read_metric("DEV1", "realpower.default_min_50s", &bmsg);
        const char* type = fty_proto_aux_string(bmsg, AGENT_CM_TYPE, "");
        CHECK(streq(type, "min"));
        CHECK(streq(fty_proto_value(bmsg), "42.00"));
        fty_proto_destroy(&bmsg);
    }
    {
        fty_proto_t* bmsg = nullptr;
        fty::shm::read_metric("DEV1", "realpower.default_max_50s", &bmsg);
        const char* type = fty_proto_aux_string(bmsg, AGENT_CM_TYPE, "");
        CHECK(streq(type, "max"));
        CHECK(streq(fty_proto_value(bmsg), "242.00"));
        fty_proto_destroy(&bmsg);
    }
    {
        fty_proto_t* bmsg = nullptr;
        fty::shm::read_metric("DEV1", "realpower.default_arithmetic_mean_50s", &bmsg);
        const char* type = fty_proto_aux_string(bmsg, AGENT_CM_TYPE, "");
        CHECK(streq(type, "arithmetic_mean"));
        CHECK(streq(fty_proto_value(bmsg), "108.50")); // (100 + 50 + 42 + 242) / 4
        fty_proto_destroy(&bmsg);
    }

    if (verbose) printf("---> Cleanup...\n");
    zactor_destroy(&mc_server);
    zactor_destroy(&server);

    CHECK(zfile_exists("state.zpl"));
    unlink("state.zpl");
    fty_shm_delete_test_dir();
}

//"consumption"
TEST_CASE("mc_server_consumption", "[mc_server][consumption]")
{
    const bool verbose = true;
    const char* endpoint = "inproc://cm-server-test"; //mlm

    // The test will last 30 sec. During this period, the power is changing twice: first after
    // 15 sec and a second time after 15 sec again. We should receive the consumption each 10s
    // and at the end, we should have the consumption of the 30 sec period in addition.

    CHECK(fty_shm_set_test_dir(".") == 0);

    unlink("state.zpl");

    fty_shm_set_default_polling_interval(2);

    // create broker
    zactor_t* server = zactor_new(mlm_server, const_cast<char*>("Malamute"));
    REQUIRE(server);
    zstr_sendx(server, "BIND", endpoint, nullptr);

    zactor_t* mc_server = zactor_new(fty_mc_server, const_cast<char*>("fty-mc-server-test"));
    REQUIRE(mc_server);
    zstr_sendx(mc_server, "TYPES", "consumption", nullptr);
    zstr_sendx(mc_server, "STEPS", "10s", "30s", nullptr);
    zstr_sendx(mc_server, "DIR", ".", nullptr);
    zstr_sendx(mc_server, "CONNECT", endpoint, nullptr);
    zstr_sendx(mc_server, "CREATE_PULL", nullptr);

    zclock_sleep(500);

    // XXX: the test is sensitive on timing!!!
    //     so it must start at the beggining of the 30th second in minute (00, 30, ...)
    //     other option is to not test in second precision,
    //     which will increase the time of make check far beyond
    //     what developers would accept ;-)
    {
        int64_t now_ms = zclock_time();
        int64_t delay = 30000 - (now_ms % 30000);
        if (verbose) printf("-----> start delayed for %dms.\n", int(delay));
        zclock_sleep(int(delay));
    }

    char consumption[64];

    // T+0s
    int64_t start_ms = zclock_time();
    if (verbose) printf("-----> start\n");

    {
        if (verbose) printf("---------> Send 0\n");
        CHECK(fty::shm::write_metric("DEV1", "realpower.default", "100", "UNIT", 60) == 0);
        zstr_send(mc_server, "UNITTEST_SYNC_METRICS");
    }

    // T+12s
    wait_time(start_ms, 12, verbose);

    // now we should have the first 10s consumption value published
    {
        fty_proto_t* bmsg = nullptr;
        fty::shm::read_metric("DEV1", "realpower.default_consumption_10s", &bmsg);
        const char* count = fty_proto_aux_string(bmsg, AGENT_CM_COUNT, "0");
        const char* type = fty_proto_aux_string(bmsg, AGENT_CM_TYPE, "");
        CHECK(streq(type, "consumption"));
        snprintf(consumption, sizeof(consumption), "%.1f", 100.0 * 10);
        if (verbose) printf("--->10(1): %s <> %s (count: %s)\n", fty_proto_value(bmsg), consumption, count);
        CHECK(streq(fty_proto_value(bmsg), consumption));
        fty_proto_destroy(&bmsg);
    }

    // T+15s
    wait_time(start_ms, 15, verbose);

    {
        if (verbose) printf("---------> Send 1\n");
        CHECK(fty::shm::write_metric("DEV1", "realpower.default", "150", "UNIT", 60) == 0);
        zstr_send(mc_server, "UNITTEST_SYNC_METRICS");
    }

    // T+22s
    wait_time(start_ms, 22, verbose);

    // now we should have the second 10s consumption value published
    {
        fty_proto_t* bmsg = nullptr;
        fty::shm::read_metric("DEV1", "realpower.default_consumption_10s", &bmsg);
        const char* count = fty_proto_aux_string(bmsg, AGENT_CM_COUNT, "0");
        const char* type = fty_proto_aux_string(bmsg, AGENT_CM_TYPE, "");
        CHECK(streq(type, "consumption"));
        snprintf(consumption, sizeof(consumption), "%.1f", 100.0 * 5 + 150.0 * 5);
        if (verbose) printf("--->10(2): %s <> %s (count: %s)\n", fty_proto_value(bmsg), consumption, count);
        CHECK(streq(fty_proto_value(bmsg), consumption));
        fty_proto_destroy(&bmsg);
    }

    // T+25s
    wait_time(start_ms, 25, verbose);

    {
        if (verbose) printf("---------> Send 2\n");
        CHECK(fty::shm::write_metric("DEV1", "realpower.default", "200", "UNIT", 60) == 0);
        zstr_send(mc_server, "UNITTEST_SYNC_METRICS");
    }

    // T+32s
    wait_time(start_ms, 32, verbose);

    // now we should have the third 10s consumption value published
    {
        fty_proto_t* bmsg = nullptr;
        fty::shm::read_metric("DEV1", "realpower.default_consumption_10s", &bmsg);
        const char* count = fty_proto_aux_string(bmsg, AGENT_CM_COUNT, "0");
        const char* type = fty_proto_aux_string(bmsg, AGENT_CM_TYPE, "");
        CHECK(streq(type, "consumption"));
        snprintf(consumption, sizeof(consumption), "%.1f", 150.0 * 5 + 200.0 * 5);
        if (verbose) printf("--->10(3): %s <> %s (count: %s)\n", fty_proto_value(bmsg), consumption, count);
        CHECK(streq(fty_proto_value(bmsg), consumption));
        fty_proto_destroy(&bmsg);
    }
    // and we should have the first 30s consumption value published
    {
        fty_proto_t* bmsg = nullptr;
        fty::shm::read_metric("DEV1", "realpower.default_consumption_30s", &bmsg);
        const char* count = fty_proto_aux_string(bmsg, AGENT_CM_COUNT, "0");
        const char* type = fty_proto_aux_string(bmsg, AGENT_CM_TYPE, "");
        CHECK(streq(type, "consumption"));
        snprintf(consumption, sizeof(consumption), "%.1f", ceil((100.0 * 15 + 150.0 * 10 + 200.0 * 5) * 10) / 10);
        if (verbose) printf("--->30: %s <> %s (count: %s)\n", fty_proto_value(bmsg), consumption, count);
        CHECK(streq(fty_proto_value(bmsg), consumption));
        fty_proto_destroy(&bmsg);
    }

    if (verbose) printf("---> Cleanup...\n");
    zactor_destroy(&mc_server);
    zactor_destroy(&server);

    CHECK(zfile_exists("state.zpl"));
    unlink("state.zpl");
    fty_shm_delete_test_dir();
}
