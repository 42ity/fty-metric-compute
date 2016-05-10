/*  =========================================================================
    bios_cm_server - Computation server implementation

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

/*
@header
    bios_cm_server - Computation server implementation
@discuss
@end
*/

#include "agent_cm_classes.h"

//  Structure of our class

struct _bios_cm_server_t {
    int filler;     //  Declare class properties here
};


//  --------------------------------------------------------------------------
//  Create a new bios_cm_server

bios_cm_server_t *
bios_cm_server_new (void)
{
    bios_cm_server_t *self = (bios_cm_server_t *) zmalloc (sizeof (bios_cm_server_t));
    assert (self);
    //  Initialize class properties here
    return self;
}


//  --------------------------------------------------------------------------
//  Destroy the bios_cm_server

void
bios_cm_server_destroy (bios_cm_server_t **self_p)
{
    assert (self_p);
    if (*self_p) {
        bios_cm_server_t *self = *self_p;
        //  Free class properties here
        //  Free object itself
        free (self);
        *self_p = NULL;
    }
}

//  --------------------------------------------------------------------------
//  Self test of this class

void
bios_cm_server_test (bool verbose)
{
    printf (" * bios_cm_server: ");

    //  @selftest
    //  Simple create/destroy test
    bios_cm_server_t *self = bios_cm_server_new ();
    assert (self);
    bios_cm_server_destroy (&self);
    //  @end
    printf ("OK\n");
}
