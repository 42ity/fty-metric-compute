#include "src/cmsteps.h"
#include "src/fty_mc_server.h"
#include <catch2/catch.hpp>

TEST_CASE("cmsteps test")
{
    //  @selftest
    //  Simple create/destroy test
    cmsteps_t* self = cmsteps_new();
    CHECK(cmsteps_gcd(self) == 0);

    cmsteps_put(self, "5h");
    CHECK(cmsteps_gcd(self) == 5 * 60 * 60);
    cmsteps_put(self, "5s");
    CHECK(cmsteps_gcd(self) == 5);

    int64_t r = cmsteps_get(self, "5s");
    CHECK(r == 5);
    r = cmsteps_get(self, "5h");
    CHECK(r == 5 * 60 * 60);
    r = cmsteps_get(self, "5X");
    CHECK(r == -1);

    for (uint32_t* it = cmsteps_first(self); it != nullptr; it = cmsteps_next(self)) {
        const char* key = reinterpret_cast<const char*>(cmsteps_cursor(self));

        CHECK(cmsteps_toint(key) == *it);
    }

    cmsteps_destroy(&self);

    // static method test
    CHECK(cmsteps_toint("42") == 42);
    CHECK(cmsteps_toint("42s") == 42);
    CHECK(cmsteps_toint("42m") == 42 * 60);
    CHECK(cmsteps_toint("42h") == 42 * 60 * 60);
    CHECK(cmsteps_toint("42d") == 42 * 24 * 60 * 60);
    CHECK(cmsteps_toint("42X") == -1);
    CHECK(cmsteps_toint("-42") == -1);

    CHECK(cmsteps_toint("24h") == cmsteps_toint("1d"));

    //  @end
    printf("OK\n");
}
