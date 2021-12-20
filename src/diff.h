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

#ifndef DIFF_H
#define DIFF_H

#include"always.h"
#include"beak.h"
#include"configuration.h"

struct Diff
{
    virtual RC diff(FileSystem *old_fs, Path *old_path,
                    FileSystem *new_fs, Path *new_path,
                    ProgressStatistics *progress) = 0;
    virtual void report(bool all_added) = 0;

    virtual ~Diff() = default;
};

std::unique_ptr<Diff> newDiff(bool detailed, int depth);

#endif
