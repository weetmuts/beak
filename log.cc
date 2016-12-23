/*  
    Copyright (C) 2016 Fredrik Öhrström

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

#include<assert.h>

#include"log.h"

#include<errno.h>
#include<stdio.h>
#include<stdlib.h>
#include<stdarg.h>
#include<syslog.h>

bool use_syslog = false;
LogLevel log_level = INFO;

void setLogLevel(LogLevel l) {
    log_level = l;
}

void useSyslog(bool sl) {
    use_syslog = sl;
}

void error(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    if (use_syslog) {
        vsyslog(LOG_ERR, fmt, args);
    }
    va_end(args);
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
    exit(1);
}

void failure(const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    if (use_syslog) {
        vsyslog(LOG_ERR, fmt, args);
    } 
    va_end(args);
    if (!log_level == QUITE) {
        va_start(args, fmt);
        vfprintf(stderr, fmt, args);
        va_end(args);
    }
}

void warning(const char* fmt, ...) {
    va_list args;
    if (use_syslog) {
        va_start(args, fmt);
        vsyslog(LOG_INFO, fmt, args);
        va_end(args);
    }
    va_start(args, fmt);
    vfprintf(stdout, fmt, args);
    va_end(args);        
}

void debug(const char* fmt, ...) {
    if (log_level == DEBUG) {
        va_list args;
        va_start(args, fmt);
        if (use_syslog) {
            vsyslog(LOG_DEBUG, fmt, args);
        } else {
            vfprintf(stdout, fmt, args);
        }
        va_end(args);
    }
}

void verbose(const char* fmt, ...) {
    if (log_level >= VERBOSE) {
        va_list args;
        if (use_syslog) {
            va_start(args, fmt);
            vsyslog(LOG_INFO, fmt, args);
            va_end(args);
        }
        va_start(args, fmt);
        vfprintf(stdout, fmt, args);
        va_end(args);
    }
}

void info(const char* fmt, ...) {
    if (log_level >= INFO) {
        va_list args;
        if (use_syslog) {
            va_start(args, fmt);
            vsyslog(LOG_INFO, fmt, args);
            va_end(args);
        }
        va_start(args, fmt);
        vfprintf(stdout, fmt, args);
        va_end(args);        
    }
}

