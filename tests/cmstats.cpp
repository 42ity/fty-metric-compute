#include "src/cmstats.h"
#include "src/fty_mc_server.h"
#include <catch2/catch.hpp>
#include <unistd.h>

TEST_CASE("cmstats test", "[cmstats]")
{
    static const char* file = "cmstats.zpl";
    unlink(file);

    cmstats_t* self = cmstats_new();
    REQUIRE(self);

    cmstats_print(self);
    cmstats_delete_asset(self, "SOMESTRING");
    // TODO uncomment, when tests for this function would be supported
    // cmstats_poll (self, client);
    cmstats_save(self, "itshouldbeemptyfile");
    cmstats_destroy(&self);
    self = cmstats_new();
    // XXX: the test is sensitive on timing!!!
    //     so it must start at the beggining of the second
    //     other option is to not test in second precision,
    //     which will increase the time of make check far beyond
    //     what developers would accept ;-)
    {
        int64_t now_ms = zclock_time();
        int64_t sl     = 10000 - (now_ms % 10000);
        zclock_sleep(int(sl));
    }
    zclock_sleep(1000);

    // 1. min test
    //  1.1 first metric in
    zmsg_t* msg =
        fty_proto_encode_metric(nullptr, uint64_t(time(nullptr)), 10, "TYPE", "ELEMENT_SRC", "100.989999", "UNIT");
    fty_proto_t* bmsg  = fty_proto_decode(&msg);
    fty_proto_t* stats = nullptr;
    fty_proto_print(bmsg);

    stats = cmstats_put(self, "min", "10s", 10, bmsg);
    CHECK(!stats);
    stats = cmstats_put(self, "max", "10s", 10, bmsg);
    CHECK(!stats);
    stats = cmstats_put(self, "arithmetic_mean", "10s", 10, bmsg);
    CHECK(!stats);
    stats = cmstats_put(self, "consumption", "10s", 10, bmsg);
    CHECK(!stats);
    fty_proto_destroy(&bmsg);

    zclock_sleep(2000);
    //  1.2 second metric (inside interval) in
    msg  = fty_proto_encode_metric(nullptr, uint64_t(time(nullptr)), 10, "TYPE", "ELEMENT_SRC", "42.11", "UNIT");
    bmsg = fty_proto_decode(&msg);
    fty_proto_print(bmsg);

    zclock_sleep(4000);
    stats = cmstats_put(self, "min", "10s", 10, bmsg);
    CHECK(!stats);
    stats = cmstats_put(self, "max", "10s", 10, bmsg);
    CHECK(!stats);
    stats = cmstats_put(self, "arithmetic_mean", "10s", 10, bmsg);
    CHECK(!stats);
    stats = cmstats_put(self, "consumption", "10s", 10, bmsg);
    CHECK(!stats);
    fty_proto_destroy(&bmsg);

    zclock_sleep(6100);

    //  1.3 third metric (outside interval) in
    msg =
        fty_proto_encode_metric(nullptr, uint64_t(time(nullptr)), 10, "TYPE", "ELEMENT_SRC", "42.889999999999", "UNIT");
    bmsg = fty_proto_decode(&msg);
    fty_proto_print(bmsg);

    //  1.4 check the minimal value
    stats = cmstats_put(self, "min", "10s", 10, bmsg);
    CHECK(stats);

    fty_proto_print(stats);
    CHECK(streq(fty_proto_value(stats), "42.11"));
    CHECK(streq(fty_proto_aux_string(stats, AGENT_CM_COUNT, nullptr), "2"));
    fty_proto_destroy(&stats);

    //  1.5 check the maximum value
    stats = cmstats_put(self, "max", "10s", 10, bmsg);
    REQUIRE(stats);

    fty_proto_print(stats);
    CHECK(streq(fty_proto_value(stats), "100.989999"));
    CHECK(streq(fty_proto_aux_string(stats, AGENT_CM_COUNT, nullptr), "2"));
    fty_proto_destroy(&stats);

    //  1.6 check the arithmetic_mean
    stats = cmstats_put(self, "arithmetic_mean", "10s", 10, bmsg);
    REQUIRE(stats);

    char* xxx = nullptr;
    int   r   = asprintf(&xxx, "%.2f", (100.99 + 42.1) / 2);
    REQUIRE(r != -1); // make gcc @ rhel happy
    CHECK(streq(fty_proto_value(stats), xxx));
    zstr_free(&xxx);
    CHECK(streq(fty_proto_aux_string(stats, AGENT_CM_COUNT, nullptr), "2"));

    //  1.7 check the consumption
    stats = cmstats_put(self, "consumption", "10s", 10, bmsg);
    REQUIRE(stats);

    r = asprintf(&xxx, "%.6f", (100.99 * 6 + 42.1 * 3) / 3600 / 1000);
    REQUIRE(r != -1); // make gcc @ rhel happy
    CHECK(streq(fty_proto_value(stats), xxx));
    zstr_free(&xxx);
    CHECK(streq(fty_proto_aux_string(stats, AGENT_CM_COUNT, nullptr), "2"));

    fty_proto_destroy(&bmsg);
    fty_proto_destroy(&stats);

    cmstats_save(self, "cmstats.zpl");
    cmstats_destroy(&self);
    self = cmstats_load("cmstats.zpl");

    // TRIVIA: extend the testing of self->stats
    //         hint is - uncomment the print :)
    // cmstats_print (self);
    CHECK(zhashx_lookup(self->stats, "TYPE_min_10s@ELEMENT_SRC"));
    CHECK(zhashx_lookup(self->stats, "TYPE_max_10s@ELEMENT_SRC"));
    CHECK(zhashx_lookup(self->stats, "TYPE_consumption_10s@ELEMENT_SRC"));

    cmstats_delete_asset(self, "ELEMENT_SRC");
    CHECK(!zhashx_lookup(self->stats, "TYPE_min_10s@ELEMENT_SRC"));
    CHECK(!zhashx_lookup(self->stats, "TYPE_max_10s@ELEMENT_SRC"));
    CHECK(!zhashx_lookup(self->stats, "TYPE_consumption_10s@ELEMENT_SRC"));

    cmstats_destroy(&self);
    unlink(file);
}
