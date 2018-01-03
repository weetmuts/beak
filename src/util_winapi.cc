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

#define WINVER 0x0601
#define _WIN32_WINNT 0x0601

#include <windows.h>

#include "util.h"

#include "log.h"

#include <ctime>

using namespace std;

static ComponentId UTIL = registerLogComponent("util");
static ComponentId TMP = registerLogComponent("tmp");

#define KB 1024ul

extern struct timespec start_time_; // Inside util.cc

void captureStartTime() {
    time_t t;
    time(&t);

    start_time_.tv_sec = t;
    start_time_.tv_nsec = 0;
}

uint64_t clockGetUnixTime()
{
    time_t t;
    time(&t);
    return t;
}

uint64_t clockGetTime()
{
    uint64_t millis = GetTickCount64();

    return (uint64_t) millis * 1000LL;
}

pid_t fork() {
    return 0;
}
