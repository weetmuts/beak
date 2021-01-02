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
#include "delta.h"
#include "restore.h"
#include "tarfile.h"
#include "monitor.h"

#include<memory>
#include<string>
#include<vector>

struct StorageTool
{
    virtual RC storeBackupIntoStorage(Backup *backup,
                                      Storage *storage,
                                      Settings *settings,
                                      ProgressStatistics *progress,
                                      Monitor *monitor) = 0;

    virtual RC copyBackupIntoStorage(Backup *backup,
                                     Path *backup_dir,
                                     FileSystem *backup_fs,
                                     Storage *storage,
                                     Settings *settings,
                                     ProgressStatistics *progress) = 0;

    virtual FileSystem *asCachedReadOnlyFS(Storage *storage,
                                           Monitor *monitor) = 0;

    virtual FileSystem *asStatOnlyFS(Storage *storage,
                                     Monitor *monitor) = 0;

    virtual RC removeBackupFiles(Storage *storage,
                                 std::vector<Path*>& files,
                                 ProgressStatistics *progress) = 0;

    virtual ~StorageTool() = default;
};

std::unique_ptr<StorageTool> newStorageTool(ptr<System> sys,
                                            ptr<FileSystem> local_fs);

#endif
