#include "src/cmsteps.h"
#include "src/fty_mc_server.h"
#include <catch2/catch.hpp>
#include <fty_shm.h>
#include <malamute.h>

TEST_CASE("fty mc server test", "[fty_mc_server]")
{
    CHECK(fty_shm_set_test_dir(".") == 0);

    unlink("state.zpl");

    fty_shm_set_default_polling_interval(2);

    static const char* endpoint = "inproc://cm-server-test";

    // create broker
    zactor_t* server = zactor_new(mlm_server, const_cast<char*>("Malamute"));
    zstr_sendx(server, "BIND", endpoint, nullptr);

    //    mlm_client_t *producer = mlm_client_new ();
    //    mlm_client_connect (producer, endpoint, 5000, "publisher");
    //    mlm_client_set_producer (producer, FTY_PROTO_STREAM_METRICS);

    // 1s consumer
    //    mlm_client_t *consumer_1s = mlm_client_new ();
    //    mlm_client_connect (consumer_1s, endpoint, 5000, "consumer_10s");
    //    mlm_client_set_consumer (consumer_1s, FTY_PROTO_STREAM_METRICS, ".*(min|max|arithmetic_mean)_10s.*");
    //
    //    // 5s consumer
    //    mlm_client_t *consumer_5s = mlm_client_new ();
    //    mlm_client_connect (consumer_5s, endpoint, 5000, "consumer_50s");
    //    mlm_client_set_consumer (consumer_5s, FTY_PROTO_STREAM_METRICS, ".*(min|max|arithmetic_mean)_50s.*");

    zactor_t* cm_server = zactor_new(fty_mc_server, const_cast<char*>("fty-mc-server"));

    zstr_sendx(cm_server, "TYPES", "min", "max", "arithmetic_mean", nullptr);
    zstr_sendx(cm_server, "STEPS", "10s", "50s", nullptr);
    zstr_sendx(cm_server, "DIR", ".", nullptr);
    zstr_sendx(cm_server, "CONNECT", endpoint, nullptr);
    //    zstr_sendx (cm_server, "PRODUCER", FTY_PROTO_STREAM_METRICS, nullptr);
    zstr_sendx(cm_server, "CREATE_PULL", nullptr);
    // zstr_sendx (cm_server, "CONSUMER", FTY_PROTO_STREAM_METRICS, ".*", nullptr);
    zclock_sleep(500);

    // XXX: the test is sensitive on timing!!!
    //     so it must start at the beggining of the fifth second in minute (00, 05, 10, ... 55)
    //     other option is to not test in second precision,
    //     which will increase the time of make check far beyond
    //     what developers would accept ;-)
    //     (in order to use shm, all timer for this test as set to x*10)
    {
        int64_t now_ms = zclock_time();
        int64_t sl     = 50000 - (now_ms % 50000);
        zclock_sleep(int(sl));
    }

    zclock_sleep(1000);
    int64_t TEST_START_MS = zclock_time();
    //    zmsg_t *msg = fty_proto_encode_metric (
    //            nullptr,
    //            time (nullptr),
    //            10,
    //            "realpower.default",(^realpower\\.default|.*temperature|.*humidity)((?!_arithmetic_mean|_max_|_min_).)*
    //            "DEV1",
    //            "100",
    //            "UNIT");
    //    mlm_client_send (producer, "realpower.default@DEV1", &msg);
    CHECK(fty::shm::write_metric("DEV1", "realpower.default", "100", "UNIT", 20) == 0);
    zclock_sleep(2000);
    // empty element_src
    //    msg = fty_proto_encode_metric (
    //            nullptr,
    //            time (nullptr),
    //            10,
    //            "realpower.default",
    //            "",
    //            "20",
    //            "UNIT");
    //    mlm_client_send (producer, "realpower.default@", &msg);
    fty::shm::write_metric("", "realpower.default", "20", "UNIT", 2);
    zclock_sleep(2000);
    //    msg = fty_proto_encode_metric (
    //            nullptr,
    //            time (nullptr),
    //            10,
    //            "realpower.default",
    //            "DEV1",
    //            "nan",
    //            "UNIT");
    //    mlm_client_send (producer, "realpower.default@DEV1", &msg);
    fty::shm::write_metric("DEV1", "realpower.default", "nan", "UNIT", 2);
    zclock_sleep(2000);
    //    msg = fty_proto_encode_metric (
    //            nullptr,
    //            time (nullptr),
    //            10,
    //            "realpower.default",
    //            "DEV1",
    //            "50",
    //            "UNIT");
    //    mlm_client_send (producer, "realpower.default@DEV1", &msg);
    fty::shm::write_metric("DEV1", "realpower.default", "50", "UNIT", 2);
    zclock_sleep(2000);

    // T+11000ms
    zclock_sleep(int(50000 - (zclock_time() - TEST_START_MS) - 39000));

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
        CHECK(streq(fty_proto_value(bmsg), "75.00"));
        fty_proto_destroy(&bmsg);
    }

    // goto T+31000ms
    zclock_sleep(int(50000 - (zclock_time() - TEST_START_MS) - 19000));
    // send some 10s min/max to differentiate the 10s and 50s min/max later on
    //    msg = fty_proto_encode_metric (
    //            nullptr,
    //            time (nullptr),
    //            10,
    //            "realpower.default",
    //            "DEV1",
    //            "42",
    //            "UNIT");
    //    mlm_client_send (producer, "realpower.default@DEV1", &msg);
    fty::shm::write_metric("DEV1", "realpower.default", "42", "UNIT", 2);
    zclock_sleep(2000);
    //    msg = fty_proto_encode_metric (
    //            nullptr,
    //            time (nullptr),
    //            10,
    //            "realpower.default",
    //            "DEV1",
    //            "242",
    //            "UNIT");
    //    mlm_client_send (producer, "realpower.default@DEV1", &msg);
    fty::shm::write_metric("DEV1", "realpower.default", "242", "UNIT", 2);
    zclock_sleep(2000);

    // goto T+46000
    zclock_sleep(int(50000 - (zclock_time() - TEST_START_MS) - 4000));
    // consume sent min/max/avg - the unit test for 1s have
    // there are 3 mins, 3 max and 3 arithmetic_mean published so far
    //    for (int i = 0; i != 9; i++)
    //    {
    //        msg = mlm_client_recv (consumer_1s);
    //        fty_proto_t *bmsg = fty_proto_decode (&msg);
    //
    //        log_debug ("subject=%s", mlm_client_subject (consumer_1s));
    //        fty_proto_print (bmsg);
    //        /* It is not reliable under memcheck, because of timing
    //        static const char* values[] = {"0", "42.000000", "242.000000", "142.000000"};
    //        bool test = false;
    //        for (int j =0; j < sizeof (values); j++)
    //        {
    //            test = streq (values [j], fty_proto_value (bmsg));
    //            if (test) {
    //                break;
    //            }
    //        }
    //        // ATTENTION: test == false , then make check will write "Segmentation fault"
    //        // instead of "Assertion failed"
    //        CHECK (test == true);
    //        */
    //        fty_proto_destroy (&bmsg);
    //    }
    // T+51000s
    zclock_sleep(int(50000 - (zclock_time() - TEST_START_MS) + 1000));
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
        CHECK(streq(fty_proto_value(bmsg), "108.50"));
        fty_proto_destroy(&bmsg);
    }

    zactor_destroy(&cm_server);
    zclock_sleep(5000);

    // to prevent false positives in memcheck - there should not be any messages in a broker
    // on the end of the run
    //    zpoller_t *poller = zpoller_new (mlm_client_msgpipe (consumer_5s), mlm_client_msgpipe (consumer_1s), nullptr);
    //    while (!zsys_interrupted) {
    //        void *which = zpoller_wait (poller, 5000);
    //
    //        if (!which)
    //            break;
    //        else
    //        if (which == mlm_client_msgpipe (consumer_1s))
    //            msg = mlm_client_recv (consumer_1s);
    //        else
    //        if (which == mlm_client_msgpipe (consumer_5s))
    //            msg = mlm_client_recv (consumer_5s);
    //
    //        zmsg_destroy (&msg);
    //    }
    //    zpoller_destroy (&poller);

    zactor_destroy(&cm_server);
    //    mlm_client_destroy (&consumer_5s);
    //    mlm_client_destroy (&consumer_1s);
    //    mlm_client_destroy (&producer);
    zactor_destroy(&server);
    CHECK(zfile_exists("state.zpl"));
    unlink("state.zpl");
    fty_shm_delete_test_dir();
}

TEST_CASE("fty mc server test with consumption", "[fty_mc_server_consumption]")
{
    // The test will last 30 sec. During this period, the power is changing twice: first after
    // 15 sec and a second time after 15 sec again. We should receive the consumption each 10s
    // and at the end, we should have the consumption of the 30 sec period in addition.

    CHECK(fty_shm_set_test_dir(".") == 0);

    unlink("state.zpl");

    fty_shm_set_default_polling_interval(10);

    static const char* endpoint = "inproc://cm-server-test";

    // create broker
    zactor_t* server = zactor_new(mlm_server, const_cast<char*>("Malamute"));
    zstr_sendx(server, "BIND", endpoint, nullptr);

    mlm_client_t *producer = mlm_client_new ();
    mlm_client_connect (producer, endpoint, 5000, "publisher");
    mlm_client_set_producer (producer, FTY_PROTO_STREAM_METRICS);

    zactor_t* cm_server = zactor_new(fty_mc_server, const_cast<char*>("fty-mc-server"));

    zstr_sendx(cm_server, "TYPES", "consumption", nullptr);
    zstr_sendx(cm_server, "STEPS", "10s", "30s", nullptr);
    zstr_sendx(cm_server, "DIR", ".", nullptr);
    zstr_sendx(cm_server, "CONNECT", endpoint, nullptr);
    zstr_sendx(cm_server, "PRODUCER", FTY_PROTO_STREAM_METRICS, nullptr);
    zstr_sendx(cm_server, "CREATE_PULL", nullptr);
    zstr_sendx(cm_server, "CONSUMER", FTY_PROTO_STREAM_METRICS, ".*", nullptr);
    zclock_sleep(500);

    // XXX: the test is sensitive on timing!!!
    //     so it must start at the beggining of the 30th second in minute (00, 30, ...)
    //     other option is to not test in second precision,
    //     which will increase the time of make check far beyond
    //     what developers would accept ;-)
    {
        int64_t now_ms = zclock_time();
        int64_t sl     = 30000 - (now_ms % 30000);
        zclock_sleep(int(sl));
    }

    // T+0s
    //printf("-----> start\n");
    {
        //printf("---------> Send 0\n");
        CHECK(fty::shm::write_metric("DEV1", "realpower.default", "100", "UNIT", 60) == 0);
        zmsg_t *msg = fty_proto_encode_metric(
            nullptr,
            static_cast<uint64_t>(time(nullptr)),
            10,
            "realpower.default",
            "DEV1",
            "100",
            "UNIT");
        mlm_client_send(producer, "realpower.default@DEV1", &msg);
    }
    zclock_sleep(11000);

    // T+11s
    // now we should have the first 10s consumption value published
    {
        fty_proto_t* bmsg = nullptr;
        fty::shm::read_metric("DEV1", "realpower.default_consumption_10s", &bmsg);
        const char* type = fty_proto_aux_string(bmsg, AGENT_CM_TYPE, "");
        CHECK(streq(type, "consumption"));
        char* consumption = nullptr;
        int r = asprintf(&consumption, "%.1f", 100.0 * 10);
        REQUIRE(r != -1); // make gcc @ rhel happy
        //printf("--->10(1): %s <> %s\n", fty_proto_value(bmsg), consumption);
        CHECK(streq(fty_proto_value(bmsg), consumption));
        zstr_free(&consumption);
        fty_proto_destroy(&bmsg);
    }
    zclock_sleep(4000);

    // T+15s
    {
        //printf("---------> Send 1\n");
        CHECK(fty::shm::write_metric("DEV1", "realpower.default", "150", "UNIT", 60) == 0);
        zmsg_t *msg = fty_proto_encode_metric(
            nullptr,
            static_cast<uint64_t>(time(nullptr)),
            10,
            "realpower.default",
            "DEV1",
            "150",
            "UNIT");
        mlm_client_send(producer, "realpower.default@DEV1", &msg);
    }
    zclock_sleep(6000);

    // T+21s
    // now we should have the second 10s consumption value published
    {
        fty_proto_t* bmsg = nullptr;
        fty::shm::read_metric("DEV1", "realpower.default_consumption_10s", &bmsg);
        const char* type = fty_proto_aux_string(bmsg, AGENT_CM_TYPE, "");
        CHECK(streq(type, "consumption"));
        char* consumption = nullptr;
        int r = asprintf(&consumption, "%.1f", 100.0 * 5 + 150.0 * 5);
        REQUIRE(r != -1); // make gcc @ rhel happy
        //printf("--->10(2): %s <> %s\n", fty_proto_value(bmsg), consumption);
        CHECK(streq(fty_proto_value(bmsg), consumption));
        zstr_free(&consumption);
        fty_proto_destroy(&bmsg);
    }
    zclock_sleep(4000);

    // T+25s
    {
        //printf("---------> Send 2\n");
        CHECK(fty::shm::write_metric("DEV1", "realpower.default", "200", "UNIT", 60) == 0);
        zmsg_t *msg = fty_proto_encode_metric (
            nullptr,
            static_cast<uint64_t>(time(nullptr)),
            10,
            "realpower.default",
            "DEV1",
            "200",
            "UNIT");
        mlm_client_send (producer, "realpower.default@DEV1", &msg);
    }
    zclock_sleep(6000);

    // T+31s
    // now we should have the third 10s consumption value published
    {
        fty_proto_t* bmsg = nullptr;
        fty::shm::read_metric("DEV1", "realpower.default_consumption_10s", &bmsg);
        const char* type = fty_proto_aux_string(bmsg, AGENT_CM_TYPE, "");
        CHECK(streq(type, "consumption"));
        char* consumption = nullptr;
        int r = asprintf(&consumption, "%.1f", 150.0 * 5 + 200.0 * 5);
        REQUIRE(r != -1); // make gcc @ rhel happy
        //printf("--->10(3): %s <> %s\n", fty_proto_value(bmsg), consumption);
        CHECK(streq(fty_proto_value(bmsg), consumption));
        zstr_free(&consumption);
        fty_proto_destroy(&bmsg);
    }
    // and we should have the first 30s consumption value published
    {
        fty_proto_t* bmsg = nullptr;
        fty::shm::read_metric("DEV1", "realpower.default_consumption_30s", &bmsg);
        const char* type = fty_proto_aux_string(bmsg, AGENT_CM_TYPE, "");
        CHECK(streq(type, "consumption"));
        char* consumption = nullptr;
        int r = asprintf(&consumption, "%.1f",
            ceil((100.0 * 15 + 150.0 * 10 + 200.0 * 5) * 10) / 10);
        REQUIRE(r != -1); // make gcc @ rhel happy
        //printf("--->30: %s <> %s\n", fty_proto_value(bmsg), consumption);
        CHECK(streq(fty_proto_value(bmsg), consumption));
        zstr_free(&consumption);
        fty_proto_destroy(&bmsg);
    }

    zactor_destroy(&cm_server);
    zclock_sleep(5000);

    mlm_client_destroy(&producer);
    zactor_destroy(&server);
    CHECK(zfile_exists("state.zpl"));
    unlink("state.zpl");
    fty_shm_delete_test_dir();
}
