/*
 Copyright (C) 2018 Fredrik Öhrström

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

#ifndef STATISTICS_H
#define STATISTICS_H

#include "always.h"
#include "filesystem.h"

#include <map>

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

    std::map<Path*,size_t> file_sizes;
};

struct ProgressStatistics
{
    Stats stats;

    virtual void startDisplayOfProgress() = 0;
    virtual void updateProgress() = 0;
    virtual void finishProgress() = 0;
    virtual ~ProgressStatistics() {};
};

enum ProgressDisplayType
{
    ProgressDisplayNone,          // No progress at all
    ProgressDisplayTerminal,      // Print on terminal
    ProgressDisplayTerminalAnsi,  // Use ansi to move the cursor
    ProgressDisplayNotification   // Use the OS notification system
};

std::unique_ptr<ProgressStatistics> newProgressStatistics(ProgressDisplayType t);

#endif
