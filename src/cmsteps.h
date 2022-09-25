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

#pragma once
#include <czmq.h>

// opacified structure
typedef struct _cmsteps_t cmsteps_t;

//  Create a new cmsteps
cmsteps_t* cmsteps_new();

//  Destroy the cmsteps
void cmsteps_destroy(cmsteps_t** self_p);

//  Return greatest common divisor (sec.) of steps - 0 means no steps are in a list
uint32_t cmsteps_gcd(cmsteps_t* self);

//  Put new step to the list, return -1 if fail (possibly wrong step)
int cmsteps_put(cmsteps_t* self, const char* step);

//  Get new step to the list. Return -1 in case of error, however positive
//  result can be cast to uint32_t
int64_t cmsteps_get(cmsteps_t* self, const char* step);

//  Return iterator to first item or NULL
uint32_t* cmsteps_first(cmsteps_t* self);

//  Return iterator to next item or NULL
uint32_t* cmsteps_next(cmsteps_t* self);

//  Return cursor on current item
const void* cmsteps_cursor(cmsteps_t* self);
