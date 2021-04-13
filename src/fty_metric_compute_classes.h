/*  =========================================================================
    fty_metric_compute_classes - private header file

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

#ifndef FTY_METRIC_COMPUTE_CLASSES_H_INCLUDED
#define FTY_METRIC_COMPUTE_CLASSES_H_INCLUDED

//  Platform definitions, must come first

//  External API
#include "../include/fty_metric_compute.h"

//  Extra headers

//  Internal API

#include "cmstats.h"
#include "cmsteps.h"

//  *** To avoid double-definitions, only define if building without draft ***
#ifndef FTY_METRIC_COMPUTE_BUILD_DRAFT_API

//  Self test of this class.
void cmstats_test (bool verbose);

//  Self test of this class.
void cmsteps_test (bool verbose);

//  Self test for private classes
void fty_metric_compute_private_selftest (bool verbose, const char *subtest);

#endif // FTY_METRIC_COMPUTE_BUILD_DRAFT_API

#endif
