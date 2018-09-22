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
#include "restore.h"
#include "tarfile.h"
#include "statistics.h"

#include<memory>
#include<string>
#include<vector>

// The origin tool manages the origin file system.
// Such a file system can be the actual local filesystem,
// or a virtual filesystem containing images exported by a camera app,
// or a virtual filesystem exported by a database, or something else.

struct OriginTool
{
    virtual void restoreFileSystem(FileSystem *backup_fs, // Gives access to the backups .tar and .gz files.
                                   FileSystem *backup_contents_fs, // Lists all backed up files stored in the backup.
                                   Restore *restore,
                                   PointInTime *point,
                                   Settings *settings,
                                   StoreStatistics *st) = 0;

    virtual void addRestoreWork(StoreStatistics *st,
                                Path *path,
                                FileStat *stat,
                                Settings *settings,
                                Restore *restore,
                                PointInTime *point) = 0;

    virtual ptr<FileSystem> fs() = 0;
};

std::unique_ptr<OriginTool> newOriginTool(ptr<System> sys,
                                          ptr<FileSystem> origin_fs);


#endif
