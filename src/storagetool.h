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

#ifndef STORAGE_H
#define STORAGE_H

#include "always.h"
#include "backup.h"
#include "beak.h"
#include "configuration.h"
#include "restore.h"
#include "tarfile.h"
#include "statistics.h"

#include<memory>
#include<string>
#include<vector>

struct StorageTool
{
    virtual RC storeFileSystemIntoStorage(FileSystem *beaked_fs,
                                          FileSystem *origin_fs,
                                          Storage *storage,
                                          ptr<StoreStatistics> st,
                                          ptr<Backup> ffs,
                                          Options *settings) = 0;

    virtual RC listPointsInTime(Storage *storage, std::vector<std::pair<Path*,struct timespec>> *v) = 0;

    // Return a filesystem for the storage location.
    virtual ptr<FileSystem> fs() = 0;
};

std::unique_ptr<StorageTool> newStorageTool(ptr<System> sys,
                                            ptr<FileSystem> sys_fs,
                                            ptr<FileSystem> local_storage_fs);

#endif
