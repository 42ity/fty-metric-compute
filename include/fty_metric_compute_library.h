/*  =========================================================================
    fty-metric-compute - generated layer of public API

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

#ifndef FTY_METRIC_COMPUTE_LIBRARY_H_INCLUDED
#define FTY_METRIC_COMPUTE_LIBRARY_H_INCLUDED

//  Set up environment for the application

//  External dependencies
#include <czmq.h>
#include <malamute.h>
#include <fty-log/fty_logger.h>
#include <ftyproto.h>
#include <fty_shm.h>

//  FTY_METRIC_COMPUTE version macros for compile-time API detection
#define FTY_METRIC_COMPUTE_VERSION_MAJOR 1
#define FTY_METRIC_COMPUTE_VERSION_MINOR 0
#define FTY_METRIC_COMPUTE_VERSION_PATCH 0

#define FTY_METRIC_COMPUTE_MAKE_VERSION(major, minor, patch) \
    ((major) * 10000 + (minor) * 100 + (patch))
#define FTY_METRIC_COMPUTE_VERSION \
    FTY_METRIC_COMPUTE_MAKE_VERSION(FTY_METRIC_COMPUTE_VERSION_MAJOR, FTY_METRIC_COMPUTE_VERSION_MINOR, FTY_METRIC_COMPUTE_VERSION_PATCH)

#define FTY_MC_SERVER_T_DEFINED

//  Public classes, each with its own header file
#include "fty_mc_server.h"

#endif