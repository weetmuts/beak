/*
 Copyright (C) 2019 Fredrik Öhrström

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

#ifndef MONITOR_H
#define MONITOR_H

#include "always.h"
#include "filesystem.h"
#include "system.h"

#include <map>
#include <string>


enum class MonitorType
{
    LAST_LINE, TOP_LINE
};

enum class MonitorFlair
{
    PLAIN, COLOR
};

struct Monitor
{
    virtual void updateJob(pid_t pid, std::string info) = 0;
    virtual std::string lastUpdate(pid_t pid) = 0;
    virtual int startDisplay(std::string job, std::function<bool()> regular_cb) = 0;
    virtual void stopDisplay(int id) = 0;
    virtual void doWhileCallbackBlocked(std::function<void()> do_cb) = 0;
    virtual ~Monitor() = default;
};

std::unique_ptr<Monitor> newMonitor(System *sys, FileSystem *fs);

#endif
