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

#ifndef ORIGINTOOL_H
#define ORIGINTOOL_H

#include "always.h"
#include "beak.h"
#include "configuration.h"
#include "reverse.h"
#include "tarfile.h"
#include "statistics.h"

#include<memory>
#include<string>
#include<vector>

struct OriginTool
{
    virtual void addReverseWork(ptr<StoreStatistics> st, Path *path, FileStat *stat, Options *settings,
                                ReverseTarredFS *rfs, PointInTime *point) = 0;

    virtual void restoreFileSystem(FileSystem *view, ReverseTarredFS *rfs, PointInTime *point,
                                   Options *settings, ptr<StoreStatistics> st, FileSystem *storage_fs) = 0;

    virtual ptr<FileSystem> fs() = 0;
};

std::unique_ptr<OriginTool> newOriginTool(ptr<System> sys,
                                          ptr<FileSystem> sys_fs,
                                          ptr<FileSystem> origin_fs);


#endif
