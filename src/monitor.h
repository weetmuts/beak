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

struct Stats
{
    size_t num_files {};
    size_t size_files {};

    size_t num_dirs {};
    size_t num_hard_links {};
    size_t num_symbolic_links {};
    size_t num_nodes {};

    size_t num_files_to_store {};
    size_t size_files_to_store {};

    size_t num_files_stored {};
    size_t size_files_stored {};
    size_t num_hard_links_stored {};
    size_t num_symbolic_links_stored {};
    size_t num_device_nodes_stored {};

    size_t num_dirs_updated {};

    size_t num_total {};

    uint64_t latest_update {};

    size_t   stat_size_files_transferred {};
    uint64_t latest_stat {};

    std::map<Path*,size_t> file_sizes;
};

struct ProgressStatistics
{
    Stats stats;

    virtual void startDisplayOfProgress() = 0;
    virtual void updateStatHint(size_t s) = 0;
    virtual void updateProgress() = 0;
    virtual void finishProgress() = 0;
    virtual void setProgress(std::string msg) = 0;
    virtual ~ProgressStatistics() {};
};

enum class ProgressDisplayType
{
    None,  // No progress at all
    Plain, // Print on terminal
    Ansi,  // Use ansi to move the cursor
};

struct Monitor
{
    virtual std::unique_ptr<ProgressStatistics> newProgressStatistics(std::string job) = 0;

    virtual void updateJob(pid_t pid, std::string info) = 0;
    virtual std::string lastUpdate(pid_t pid) = 0;
    virtual int startDisplay(std::function<bool()> regular_cb) = 0;
    virtual void stopDisplay(int id) = 0;
    virtual void doWhileCallbackBlocked(std::function<void()> do_cb) = 0;

    virtual ~Monitor() = default;
};

std::unique_ptr<Monitor> newMonitor(System *sys, FileSystem *fs);

#endif
