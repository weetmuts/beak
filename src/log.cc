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

#include <assert.h>
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

static set<int> log_components_;
static set<int> trace_components_;

static int num_components_ = 0;
#define MAX_NUM_COMPONENTS 64
static const char *all_components_[MAX_NUM_COMPONENTS];

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
    for (int i=0; i<num_components_; ++i) {
        if (!strcmp(c, all_components_[i])) {
            return i;
        }
    }
    return -1;
}

static void logAll(set<int> *components) {
    for (int i=0; i<num_components_; ++i) {
        components->insert(i);
    }
}

ComponentId registerLogComponent(const char *component)
{
    int c = findComponent(component);
    if (c >= 0) {
        return c;
    }
    c = num_components_;
    all_components_[num_components_] = component;
    assert(num_components_ < MAX_NUM_COMPONENTS);

    // Check if the log component is enabled through an env variable.
    // Instead of the command line.
    char name[128];
    strcpy(name, "BEAK_DEBUG_");
    strcat(name, component);
    char *val = getenv(name);
    if (val != NULL) {
        log_components_.insert(c);
        setLogLevel(DEBUG);
    }

    // Check if the trace component is enabled through an env variable.
    // Instead of the command line.
    strcpy(name, "BEAK_TRACE_");
    strcat(name, component);
    val = getenv(name);
    if (val != NULL) {
        trace_components_.insert(c);
        setLogLevel(TRACE);
    }

    return num_components_++;
}

void listLogComponents()
{
    vector<string> c;
    for (int i=0; i<num_components_; ++i) {
        c.push_back(string(all_components_[i]));
    }
    sort(c.begin(), c.end());
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

void addRemoveComponent_(const char *co, set<int> *components)
{
    // You can do --log=all,-systemio,-lock to reduce the amount of logging.
    if (!strncmp(co, "all", 3)) {
        logAll(components);
    } else {
        if (co[0] == '-') {
            int c = findComponent(co+1);
            if (c == -1) error(0,"No such log component: \"%s\"\n", co+1);
            components->erase(c);
        } else {
            int c = findComponent(co);
            if (c == -1) error(0,"No such log component: \"%s\"\n", co);
            components->insert(c);
        }
    }
}

void setLogOrTraceComponents_(const char *cs, set<int> *components)
{
    string clist = cs;
    size_t p = 0, pp;

    while (p < clist.length()) {
        pp = clist.find(',', p);
        if (pp == string::npos) {
            const char *co = clist.substr(p).c_str();
            addRemoveComponent_(co, components);
            break;
        } else {
            const char *co = clist.substr(p, pp-p).c_str();
            addRemoveComponent_(co, components);
        }
        p = pp+1;
    }
}

void setLogComponents(const char *cs)
{
    setLogOrTraceComponents_(cs, &log_components_);
}

void setTraceComponents(const char *cs)
{
    setLogOrTraceComponents_(cs, &trace_components_);
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
        syslog(LOG_ERR, "Fatal error in %s: ", all_components_[ci]);
        vsyslog(LOG_ERR, fmt, args);
    }
    va_end(args);
    va_start(args, fmt);
    fprintf(stderr, "Fatal error in %s: ", all_components_[ci]);
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

void logSystem(ComponentId ci, const char* fmt, ...) {
    va_list args;
    va_start(args, fmt);
    vsyslog(LOG_INFO, fmt, args);
    va_end(args);
}

void logDebug(ComponentId ci, const char* fmt, ...) {
    if (log_level == DEBUG &&
    		(log_components_.size()==0 ||
    		 log_components_.count(ci) == 1)) {
        va_list args;
        va_start(args, fmt);
        if (use_syslog) {
	    syslog(LOG_INFO, "%s: ", all_components_[ci]);
            vsyslog(LOG_DEBUG, fmt, args);
        } else {
	    fprintf(stdout, "%s: ", all_components_[ci]);
            vfprintf(stdout, fmt, args);
        }
        va_end(args);
    }
}

void logTrace(ComponentId ci, const char* fmt, ...) {
    if (log_level == TRACE &&
        (trace_components_.size()==0 ||
         trace_components_.count(ci) == 1)) {
        va_list args;
        va_start(args, fmt);
        if (use_syslog) {
	    syslog(LOG_INFO, "%s: ", all_components_[ci]);
            vsyslog(LOG_DEBUG, fmt, args);
        } else {
	    fprintf(stdout, "%s: ", all_components_[ci]);
            vfprintf(stdout, fmt, args);
        }
        va_end(args);
    }
}

void logVerbose(ComponentId ci, const char* fmt, ...) {
    if (log_level >= VERBOSE &&
        (log_components_.size()==0 ||
         log_components_.count(ci) == 1)) {
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
        fflush(stdout);
    }
}
