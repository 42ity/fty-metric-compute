/*  =========================================================================
    cmstats - Computing the stats on metrics
    Note: This file was manually amended, see below

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
#include <fty_proto.h>

//  Add your own public definitions here, if you need them
#define AGENT_CM_COUNT  "x-cm-count"    // how many measurements are there
#define AGENT_CM_SUM    "x-cm-sum"      // sum of the values
#define AGENT_CM_TYPE   "x-cm-type"     // type of computation (min/max/...)
#define AGENT_CM_STEP   "x-cm-step"     // computation step (in seconds)
#define AGENT_CM_LASTTS "x-cm-last-ts"  // timestamp of last metric

// opacified structure
typedef struct _cmstats_t cmstats_t;

//  Create a new cmstats
cmstats_t* cmstats_new(void);

//  Destroy the cmstats
void cmstats_destroy(cmstats_t** self_p);

//  Print the cmstats
void cmstats_print(cmstats_t* self);

//  Says if the given metric (quantity@assetName) if handled by the cmstats
bool cmstats_exist(cmstats_t* self, const char* metricName);

// Update statistics with "aggr_fun" and "step" for the incomming message "bmsg"
// Caller is responsible for destroying the value that was returned.
//
// parameter "aggr_fun" is supposed to be
// * min - to find a minimum value inside the given interval
// * max - to find a maximum value inside the given interval
// * arithmetic_mean - to compute an arithmetic mean inside the given interval
//
// \param self - statistics object
// \param aggr_fun - a type of aggregation ( min, max, avg )
// \param sstep - string representation of the step to be used in topic creation
// \param step - in [s]
// \param bmsg - message with received new RAW value
//
// \return NULL - if nothing to publish
//                  * if we just started the computation
//                  * if we are in the middle of computation (inside the interval)
//         ret  - if we have just completed the computation for the interval and
//                started new one. ( The old one is returned)
//
fty_proto_t* cmstats_put(cmstats_t* self, const char* aggr_fun, const char* sstep, uint32_t step, fty_proto_t* bmsg);

//  Remove all the asset_name entries from stats
void cmstats_delete_asset(cmstats_t* self, const char* asset_name);

//  Polling handler - publish && reset the computed values if needed
void cmstats_poll(cmstats_t* self);

//  Save the cmstats to filename
// returns 0 if success, else <0
int cmstats_save(cmstats_t* self, const char* filename);

//  Load the cmstats from filename, return NULL if fail
cmstats_t* cmstats_load(const char* filename);
