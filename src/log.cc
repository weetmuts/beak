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

#include "log.h"

#include <stdarg.h>
#include <stddef.h>
#include <string.h>

#ifdef USE_SYSLOG
#include <syslog.h>
#endif

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <set>
#include <utility>
#include <vector>

using namespace std;

bool use_syslog = false;
LogLevel log_level = INFO;

static set<int> log_components;

static int num_components = 0;
static const char *all_components[64];

bool debug_logging_ = false;
bool verbose_logging_ = false;

#ifndef USE_SYSLOG
#define LOG_INFO 1
#define LOG_ERR 2
#define LOG_DEBUG 3
void syslog(int l, const char* fmt, ...) { }
void vsyslog(int l, const char* fmt, ...) { }
#endif

static int findComponent(const char *c) {
    for (int i=0; i<num_components; ++i) {
        if (!strcmp(c, all_components[i])) {
            return i;
        }
    }
    return -1;
}

static void logAll() {
    for (int i=0; i<num_components; ++i) {
        log_components.insert(i);
    }
}

ComponentId registerLogComponent(const char *component) {
    int c = findComponent(component);
    if (c >= 0) {
        return c;
    }
    all_components[num_components] = component;
    return num_components++;
}

void listLogComponents()
{
    vector<string> c;
    for (int i=0; i<num_components; ++i) {
        c.push_back(all_components[i]);
    }
    std::sort(c.begin(), c.end());
    for (auto & s : c) {
        printf("%s\n", s.c_str());
    }
}

void setLogLevel(LogLevel l) {
    log_level = l;
    if (log_level == VERBOSE) {
        verbose_logging_ = true;
    }
    if (log_level == DEBUG) {
        verbose_logging_ = true;
        debug_logging_ = true;
    }
}

void setLogComponents(const char *cs) {
    string components = cs;
    int c;
    size_t p = 0, pp;

    if (components == "all") {
        logAll();
        return;
    }
    while (p < components.length()) {
        pp = components.find(',', p);
        if (pp == string::npos) {
            const char *co = components.substr(p).c_str();
            c = findComponent(co);
            if (c == -1) error(0,"No such log component: \"%s\"\n", co);
            log_components.insert(c);
            break;
        } else {
            const char *co = components.substr(p, pp-p).c_str();
            c = findComponent(co);
            if (c == -1) error(0,"No such log component: \"%s\"\n", co);
            log_components.insert(c);
        }
        p = pp+1;
    }
}

LogLevel logLevel() {
    return log_level;
}

void useSyslog(bool sl) {
    use_syslog = sl;
}

void error(ComponentId ci, const char* fmt, ...) {
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

void failure(ComponentId ci, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    if (use_syslog) {
        vsyslog(LOG_ERR, fmt, args);
    }
    va_end(args);
    va_start(args, fmt);
    vfprintf(stderr, fmt, args);
    va_end(args);
}

void warning(ComponentId ci, const char* fmt, ...) {
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

void logDebug(ComponentId ci, const char* fmt, ...) {
    if (log_level == DEBUG &&
    		(log_components.size()==0 ||
    		 log_components.count(ci) == 1)) {
        va_list args;
        va_start(args, fmt);
        if (use_syslog) {
	    syslog(LOG_INFO, "%s: ", all_components[ci]);
            vsyslog(LOG_DEBUG, fmt, args);
        } else {
	    fprintf(stdout, "%s: ", all_components[ci]);
            vfprintf(stdout, fmt, args);
        }
        va_end(args);
    }
}

void logVerbose(ComponentId ci, const char* fmt, ...) {
    if (log_level >= VERBOSE &&
    		(log_components.size()==0 ||
    		 log_components.count(ci) == 1)) {
        va_list args;
        if (use_syslog) {
	    syslog(LOG_INFO, "%s: ", all_components[ci]);
	    va_start(args, fmt);
            vsyslog(LOG_INFO, fmt, args);
            va_end(args);
        }
        fprintf(stdout, "%s: ", all_components[ci]);
        va_start(args, fmt);
        vfprintf(stdout, fmt, args);
        va_end(args);
    }
}

void info(ComponentId ci, const char* fmt, ...) {
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
