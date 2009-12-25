/*
  ZynAddSubFX - a software synthesizer

  Util.h - Miscellaneous functions
  Copyright (C) 2002-2005 Nasca Octavian Paul
  Author: Nasca Octavian Paul

  This program is free software; you can redistribute it and/or modify
  it under the terms of version 2 of the GNU General Public License
  as published by the Free Software Foundation.

  This program is distributed in the hope that it will be useful,
  but WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
  GNU General Public License (version 2 or later) for more details.

  You should have received a copy of the GNU General Public License (version 2)
  along with this program; if not, write to the Free Software Foundation,
  Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307 USA

*/

#ifndef UTIL_H
#define UTIL_H

#include <string>
#include <sstream>
#include "../globals.h"
#include "Config.h"

//Velocity Sensing function
extern REALTYPE VelF(REALTYPE velocity, unsigned char scaling);

bool fileexists(const char *filename);

#define N_DETUNE_TYPES 4 //the number of detune types
extern REALTYPE getdetune(unsigned char type,
                          unsigned short int coarsedetune,
                          unsigned short int finedetune);

extern REALTYPE newgetdetune(unsigned char type,
                          int octave,
                          int coarsedetune,
                          int finedetune);

/**Try to set current thread to realtime priority program priority
 * \todo see if the right pid is being sent
 * \todo see if this is having desired effect, if not then look at
 * pthread_attr_t*/
void set_realtime();

extern REALTYPE *denormalkillbuf; /**<the buffer to add noise in order to avoid denormalisation*/

extern Config config;

template<class T>
std::string stringFrom(T x)
{
    std::stringstream ss;
    ss << x;
    return ss.str();
}

template<class T>
T stringTo(const char *x)
{
    std::string str = x != NULL ? x : "0"; //should work for the basic float/int
    std::stringstream ss(str);
    T ans;
    ss >> ans;
    return ans;
}

#endif

