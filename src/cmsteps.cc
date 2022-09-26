/*  =========================================================================
    cmsteps - Helper class for list of steps

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

/// cmsteps - Helper class for list of steps

#include "cmsteps.h"
#include <czmq.h>

//  Structure of our class
struct _cmsteps_t
{
    zhashx_t* steps; // in [s]
    uint32_t  gcd;   // in [s]
};

//  --------------------------------------------------------------------------
// zhashx_t uint32_t* item destructor (czmq_destructor)
static void s_desctructor(void** item_p)
{
    if (item_p && *item_p) {
        free(*item_p);
        *item_p = NULL;
    }
}

//  --------------------------------------------------------------------------
//  Create a new cmsteps

cmsteps_t* cmsteps_new()
{
    cmsteps_t* self = reinterpret_cast<cmsteps_t*>(zmalloc(sizeof(cmsteps_t)));
    if (!self) return nullptr;

    //  Initialize class properties here
    self->steps = zhashx_new();
    if (!self->steps) {
        cmsteps_destroy(&self);
        return nullptr;
    }

    zhashx_set_destructor(self->steps, s_desctructor);
    return self;
}

//  --------------------------------------------------------------------------
//  Destroy the cmsteps

void cmsteps_destroy(cmsteps_t** self_p)
{
    if (self_p && *self_p) {
        cmsteps_t* self = *self_p;
        //  Free class properties here
        zhashx_destroy(&self->steps);
        //  Free object itself
        free(self);
        *self_p = nullptr;
    }
}

//  --------------------------------------------------------------------------
//  Convert the step with prefix to number in seconds
//      "42" -> 42
//      "42s" -> 42
//      "42m" -> 2520

static int64_t s_step_toint(const char* step)
{
    if (!(step && (*step))) {
        return -1; // NULL or empty
    }

    char suffix = char(tolower(step[strlen(step) - 1]));
    uint32_t times = 1;
    if (!isdigit(suffix)) {
        switch (suffix) {
            case 's':
                times = 1;
                break;
            case 'm':
                times = 60;
                break;
            case 'h':
                times = 60 * 60;
                break;
            case 'd':
                times = 24 * 60 * 60;
                break;
            default:
                return -1; // not handled
        }
    }

    int64_t ret = int64_t(atoi(step));
    if (ret < 0) {
        return -1; // NaN
    }
    ret *= times;
    if (ret > UINT32_MAX) {
        return -1; // out of bounds
    }

    return ret; // ret >= 0
}

// http://www.math.wustl.edu/~victor/mfmm/compaa/gcd.c
static uint32_t s_gcd(uint32_t a, uint32_t b)
{
    while (a != 0) {
        uint32_t c = a;
        a = b % a;
        b = c;
    }
    return b;
}

// compute greatest common divisor from steps
static uint32_t s_cmsteps_gcd(cmsteps_t* self)
{
    uint32_t gcd = 0;

    if (self && zhashx_size(self->steps) != 0) {
        gcd = *cmsteps_first(self);
        for (uint32_t* step_p = cmsteps_next(self); step_p; step_p = cmsteps_next(self)) {
            gcd = s_gcd(gcd, *step_p);
        }
    }

    return gcd;
}

//  --------------------------------------------------------------------------
//  Return greatest common divisor of steps - 0 means no steps are in a list
//  in [s]

uint32_t cmsteps_gcd(cmsteps_t* self)
{
    return self ? self->gcd : 0;
}

//  --------------------------------------------------------------------------
//  Put new step to the list, return -1 if fail (possibly wrong step)

int cmsteps_put(cmsteps_t* self, const char* step)
{
    if (!self) {
        return -1;
    }

    int64_t r = s_step_toint(step);
    if (r == -1) {
        return -1;
    }

    uint32_t* n = reinterpret_cast<uint32_t*>(malloc(sizeof(uint32_t)));
    *n = uint32_t(r);
    zhashx_update(self->steps, step, n);

    self->gcd = s_cmsteps_gcd(self);

    return 0;
}

//  --------------------------------------------------------------------------
//  Get new step to the list. Return -1 in case of error, however positive
//  result can be cast to uint32_t

int64_t cmsteps_get(cmsteps_t* self, const char* step)
{
    if (!(self && step && (*step))) {
        return -1;
    }

    const uint32_t* n = reinterpret_cast<const uint32_t*>(zhashx_lookup(self->steps, step));
    return n ? int64_t(*n) : -1;
}

//  --------------------------------------------------------------------------
//  Return iterator to first item or NULL

uint32_t* cmsteps_first(cmsteps_t* self)
{
    return self ? reinterpret_cast<uint32_t*>(zhashx_first(self->steps)) : NULL;
}

//  --------------------------------------------------------------------------
//  Return iterator to next item or NULL

uint32_t* cmsteps_next(cmsteps_t* self)
{
    return self ? reinterpret_cast<uint32_t*>(zhashx_next(self->steps)) : NULL;
}

//  --------------------------------------------------------------------------
//  Return cursor on current item

const void* cmsteps_cursor(cmsteps_t* self)
{
    return self ? zhashx_cursor(self->steps) : NULL;
}
