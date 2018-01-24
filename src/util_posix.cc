/*
 Copyright (C) 2016-2017 Fredrik Öhrström

 This program is free software: you can redistribute it and/or modify
 it under the terms of the GNU General Public License as published by
 the Free Software Foundation, either version 3 of the License, or
 (at your option) any later version.

 This program is distributed in the hope that it will be useful,
 but WITHOUT ANY WARRANTY; without even the implied warranty of
 MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 GNU General Public License for more details.

 You should have received a copy of the GNU General Public License
 along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "util.h"

#include <zlib.h>

#include "log.h"

#define KB 1024ul

using namespace std;

//static ComponentId UTIL = registerLogComponent("util");

extern struct timespec start_time_; // Inside util.cc

// Seconds since 1970-01-01 Z timezone.
uint64_t clockGetUnixTime()
{
    return time(NULL);
}

// Return microseconds
uint64_t clockGetTime()
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t) ts.tv_sec * 1000000LL + (uint64_t) ts.tv_nsec / 1000LL;
}

void captureStartTime() {
    clock_gettime(CLOCK_REALTIME, &start_time_);
}
