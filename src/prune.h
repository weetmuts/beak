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

#ifndef PRUNE_H
#define PRUNE_H

#include "always.h"
#include "configuration.h"
#include <map>

struct Prune
{
    virtual void addPointInTime(uint64_t p) = 0;
    virtual void prune(std::map<uint64_t,bool> *result) = 0;
    virtual void pointHasLostFiles(uint64_t point, int num_files, size_t size_files) = 0;
    virtual void verbosePruneDecisions() = 0;
    virtual uint64_t mostRecentWeeklyBackup() = 0;

    virtual ~Prune() = default;
};

std::unique_ptr<Prune> newPrune(uint64_t now, const Keep &keep);

#endif
