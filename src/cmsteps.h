/*  =========================================================================
    cmsteps - Helper class for list of steps

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

#ifndef CMSTEPS_H_INCLUDED
#define CMSTEPS_H_INCLUDED

#ifdef __cplusplus
extern "C" {
#endif

typedef struct _cmsteps_t cmsteps_t;

//  @interface
//  Create a new cmsteps
AGENT_CM_EXPORT cmsteps_t *
    cmsteps_new (void);

//  Destroy the cmsteps
AGENT_CM_EXPORT void
    cmsteps_destroy (cmsteps_t **self_p);

//  Self test of this class
AGENT_CM_EXPORT void
    cmsteps_test (bool verbose);

//  @end

#ifdef __cplusplus
}
#endif

#endif
