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

#include<string>

#include "defs.h"

using namespace std;

enum LogLevel
{
	QUITE, INFO, VERBOSE, DEBUG
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
// A not serious failure that still should be logged
void warning(ComponentId ci, const char* fmt, ...);
// Debug logging
void debug(ComponentId ci, const char* fmt, ...);
// Verbose logging
void verbose(ComponentId ci, const char* fmt, ...);
// Startup messages and other information
void info(ComponentId ci, const char* fmt, ...);

#endif
