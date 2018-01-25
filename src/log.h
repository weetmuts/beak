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

#ifndef LOG_H
#define LOG_H

#include "always.h"

enum LogLevel
{
    // Errors and failures are always printed.
    QUITE,      // No info but display warnings, failures and of course stop on errors.
    INFO,       // Normal mode
    VERBOSE,    // Verbose information
    DEBUG       // Debug information
};
typedef int ComponentId;

void setLogLevel(LogLevel l);
void setLogComponents(const char *cs);
LogLevel logLevel();
void useSyslog(bool sl);
ComponentId registerLogComponent(const char *component);
void listLogComponents();

// A fatal program terminating error
void error(ComponentId ci, const char* fmt, ...);

// A serious failure that is always logged
void failure(ComponentId ci, const char* fmt, ...);

// Startup messages and other information
// Silenced with: -q
void info(ComponentId ci, const char* fmt, ...);

// A not serious failure that still should be logged.
// Silenced with: -q -q
void warning(ComponentId ci, const char* fmt, ...);

extern bool debug_logging_;

// Debug logging
// Enabled with: -v -v
#define debug(args...) {if(debug_logging_){logDebug(args);}}
void logDebug(ComponentId ci, const char* fmt, ...);
// The macro is used to avoid evaluating complex argument fed to the debug printout,
// when not running with debug enabled.

extern bool verbose_logging_;

// Verbose logging
// Enabled with: -v
#define verbose(args...) {if(verbose_logging_){logVerbose(args);}};
void logVerbose(ComponentId ci, const char* fmt, ...);
// The macro is used to avoid evaluating complex argument fed to the verbose printout,
// when not running with verbose enabled.

#endif
