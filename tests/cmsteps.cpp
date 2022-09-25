#include "src/cmsteps.h"
#include <catch2/catch.hpp>

static int print_steps(cmsteps_t* self)
{
    if (!self) return -1;

    int cnt = 0;
    for (uint32_t* step = cmsteps_first(self); step; step = cmsteps_next(self))
    {
        const char* step_str = reinterpret_cast<const char*>(cmsteps_cursor(self));
        printf("-->[%02d] %3s: %7u s.\n", cnt, step_str, *step);
        cnt++;
    }
    printf("-->gcd: %u s.\n", cmsteps_gcd(self));

    return cnt;
}

TEST_CASE("cmsteps", "[cmsteps]")
{
    SECTION("cmsteps_1")
    {
        cmsteps_t* self = cmsteps_new();
        REQUIRE(self);

        CHECK(cmsteps_first(self) == NULL);
        CHECK(cmsteps_gcd(self) == 0);

        CHECK(cmsteps_put(self, "5h") == 0);
        CHECK(cmsteps_gcd(self) == 5 * 60 * 60);
        CHECK(cmsteps_put(self, "1h") == 0);
        CHECK(cmsteps_gcd(self) == 60 * 60);
        CHECK(cmsteps_put(self, "30m") == 0);
        CHECK(cmsteps_gcd(self) == 30 * 60);
        CHECK(cmsteps_put(self, "1m") == 0);
        CHECK(cmsteps_gcd(self) == 60);
        CHECK(cmsteps_put(self, "5s") == 0);
        CHECK(cmsteps_gcd(self) == 5);

        const int total = 5;
        int cnt = print_steps(self);
        CHECK(cnt == total);

        CHECK(cmsteps_get(self, NULL) == -1);
        CHECK(cmsteps_get(self, "") == -1);
        CHECK(cmsteps_get(self, "5X") == -1);

        CHECK(cmsteps_get(self, "5s") == 5);
        CHECK(cmsteps_get(self, "5h") == 5 * 60 * 60);

        cmsteps_destroy(&self);
        REQUIRE(!self);
    }

    SECTION("cmsteps_2")
    {
        cmsteps_t* self = cmsteps_new();
        REQUIRE(self);

        // agent STEPS
        for (auto& step : {"15m", "30m", "1h", "8h", "24h", "7d", "30d"}) {
            CHECK(cmsteps_put(self, step) == 0);
        }

        CHECK(cmsteps_gcd(self) == 15 * 60);

        const int total = 7;
        int cnt = print_steps(self);
        CHECK(cnt == total);

        cmsteps_destroy(&self);
        REQUIRE(!self);
    }
}
