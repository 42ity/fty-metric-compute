/*  =========================================================================
    cmstats - Computing the stats on metrics

    Copyright (C) 2016 Eaton                                               
                                                                           
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

#ifndef CMSTATS_H_INCLUDED
#define CMSTATS_H_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _cmstats_t cmstats_t;

//  @interface
//  Create a new cmstats
AGENT_CM_EXPORT cmstats_t *
    cmstats_new (void);

//  Destroy the cmstats
AGENT_CM_EXPORT void
    cmstats_destroy (cmstats_t **self_p);

//  Print the cmstats
AGENT_CM_EXPORT void
    cmstats_print (cmstats_t *self);

// Compute the $type value for given step - if the interval is over and new metric
// is already inside the interval, NULL is returned
// Otherwise new bios_proto_t metric is returned. Caller is responsible for
// destroying the value.
//
// Type is supposed to be
// * min - to find a minimum value inside given interval
// * max - for find a maximum value
// * arithmetic_mean - to compute arithmetic mean
//
AGENT_CM_EXPORT bios_proto_t*
    cmstats_put (cmstats_t *self, const char* type, const char *sstep, uint32_t step, bios_proto_t *bmsg);

//  Remove all the entries related to device dev from stats
AGENT_CM_EXPORT void
    cmstats_delete_dev (cmstats_t *self, const char *dev);

//  Polling handler - publish && reset the computed values
AGENT_CM_EXPORT void
    cmstats_poll (cmstats_t *self, mlm_client_t *client, bool verbose);

//  Save the cmstats to filename, return -1 if fail
AGENT_CM_EXPORT int
    cmstats_save (cmstats_t *self, const char *filename);

//  Load the cmstats from filename, return NULL if fail
AGENT_CM_EXPORT cmstats_t *
    cmstats_load (const char *filename);

//  Self test of this class
AGENT_CM_EXPORT void
    cmstats_test (bool verbose);

//  @end

#ifdef __cplusplus
}
#endif

#endif
