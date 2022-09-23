/*  =========================================================================
    fty_mc_server - Computation server implementation

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

//  Add your own public definitions here, if you need them
#define AGENT_CM_COUNT  "x-cm-count"    // how many measurements are there
#define AGENT_CM_SUM    "x-cm-sum"      // sum of the values
#define AGENT_CM_TYPE   "x-cm-type"     // type of computation (min/max/...)
#define AGENT_CM_STEP   "x-cm-step"     // computation step (in seconds)
#define AGENT_CM_LASTTS "x-cm-last-ts"  // timestamp of last metric

//  fty_mc_server actor
void fty_mc_server (zsock_t *pipe, void *args);
