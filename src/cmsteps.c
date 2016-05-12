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

/*
@header
    cmsteps - Helper class for list of steps
@discuss
@end
*/

#include "agent_cm_classes.h"

//  Structure of our class

struct _cmsteps_t {
    zhashx_t *steps;
    uint32_t gcd;
};

static void s_desctructor (void **item_p)
{
    free (*item_p);
}

//  --------------------------------------------------------------------------
//  Create a new cmsteps

cmsteps_t *
cmsteps_new (void)
{
    cmsteps_t *self = (cmsteps_t *) zmalloc (sizeof (cmsteps_t));
    assert (self);
    //  Initialize class properties here
    self->steps = zhashx_new ();
    assert (self->steps);
    zhashx_set_destructor (self->steps, s_desctructor);
    return self;
}

//  --------------------------------------------------------------------------
//  Convert the time with prefix to number in seconds
//      "42" -> 42
//      "42s" -> 42
//      "42m" -> 2520

int64_t
cmsteps_toint (const char *step)
{
    assert (step);
    assert (strlen (step) > 0);

    int64_t ret = 0;
    uint32_t times = 1;
    
    char suffix = tolower (step [strlen (step) -1]);

    if (!isdigit (suffix)) {
        switch (suffix) {
            case 's' :
                times = 1;
                break;
            case 'm' :
                times = 60;
                break;
            case 'h' :
                times = 60*60;
                break;
            case 'd' :
                times = 24*60*60;
                break;
            default :
                return -1;
        }
    }

    ret = (int64_t) atoi (step);
    if (ret < 0)
        return -1;
    ret *= times;
    if (ret > UINT32_MAX)
        return -1;

    return ret;
}

// http://www.math.wustl.edu/~victor/mfmm/compaa/gcd.c
static uint32_t
s_gcd (uint32_t a, uint32_t b)
{
    uint32_t c;
    while (a != 0) {
        c = a;
        a = b % a;
        b = c;
    }
    return b;
}

// compute greatest common divisor from steps
static uint32_t
s_cmsteps_gcd (cmsteps_t *self)
{
    assert (self);
    uint32_t gcd;

    if (zhashx_size (self->steps) == 0)
        gcd = 0;
    else
    if (zhashx_size (self->steps) == 1)
        gcd = *cmsteps_first (self);
    else {
        gcd = *cmsteps_first (self);
        for (uint32_t *step_p = cmsteps_first (self);
                       step_p != NULL;
                       step_p = cmsteps_next (self))
        {
            gcd = s_gcd (gcd, *step_p);
        }
    }

    return gcd;
}

//  --------------------------------------------------------------------------
//  Return greatest common divisor of steps - 0 means no steps are in a list

uint32_t
cmsteps_gcd (cmsteps_t *self)
{
    assert (self);
    return self->gcd;
}

//  --------------------------------------------------------------------------
//  Put new step to the list, return -1 if fail (possibly wrong step)

int
cmsteps_put (cmsteps_t *self, const char* step)
{
    assert (self);

    int64_t r = cmsteps_toint (step);
    if (r == -1)
        return -1;

    uint32_t *n = (uint32_t*) malloc (sizeof (uint32_t));
    *n = r;
    zhashx_update (self->steps, step, n);

    self->gcd = s_cmsteps_gcd (self);

    return 0;
}

//  --------------------------------------------------------------------------
//  Get new step to the list. Return -1 in case of error, however positive
//  result can be cast to uint32_t

int64_t
cmsteps_get (cmsteps_t *self, const char* step)
{
    assert (self);
    const uint32_t *n = (const uint32_t*) zhashx_lookup (self->steps, step);
    if (!n)
        return -1;
    return (int64_t) *n;
}

//  --------------------------------------------------------------------------
//  Return iterator to first item

uint32_t *
cmsteps_first (cmsteps_t *self)
{
    return (uint32_t*) zhashx_first (self->steps);
}

//  --------------------------------------------------------------------------
//  Return iterator to next item or NULL

uint32_t *
cmsteps_next (cmsteps_t *self)
{
    return (uint32_t*) zhashx_next (self->steps);
}

//  --------------------------------------------------------------------------
//  Return cursor

const void *
cmsteps_cursor (cmsteps_t *self)
{
    return zhashx_cursor (self->steps);
}

//  --------------------------------------------------------------------------
//  Destroy the cmsteps

void
cmsteps_destroy (cmsteps_t **self_p)
{
    assert (self_p);
    if (*self_p) {
        cmsteps_t *self = *self_p;
        //  Free class properties here
        zhashx_destroy (&self->steps);
        //  Free object itself
        free (self);
        *self_p = NULL;
    }
}

//  --------------------------------------------------------------------------
//  Self test of this class

void
cmsteps_test (bool verbose)
{
    printf (" * cmsteps: ");

    //  @selftest
    //  Simple create/destroy test
    cmsteps_t *self = cmsteps_new ();
    assert (cmsteps_gcd (self) == 0);

    cmsteps_put (self, "5h");
    assert (cmsteps_gcd (self) == 5*60*60);
    cmsteps_put (self, "5s");
    assert (cmsteps_gcd (self) == 5);

    int64_t r = cmsteps_get (self, "5s");
    assert (r == 5);
    r = cmsteps_get (self, "5h");
    assert (r == 5*60*60);
    r = cmsteps_get (self, "5X");
    assert (r == -1);

    for (uint32_t *it = cmsteps_first (self);
                   it != NULL;
                   it = cmsteps_next (self))
    {
        const char *key = (const char*) cmsteps_cursor (self);

        assert (cmsteps_toint (key) == *it);
    }

    cmsteps_destroy (&self);

    // static method test
    assert (cmsteps_toint ("42") == 42);
    assert (cmsteps_toint ("42s") == 42);
    assert (cmsteps_toint ("42m") == 42*60);
    assert (cmsteps_toint ("42h") == 42*60*60);
    assert (cmsteps_toint ("42d") == 42*24*60*60);
    assert (cmsteps_toint ("42X") == -1);
    assert (cmsteps_toint ("-42") == -1);

    //  @end
    printf ("OK\n");
}
