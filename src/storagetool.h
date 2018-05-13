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
#include "beak.h"
#include "configuration.h"
#include "reverse.h"
#include "tarfile.h"
#include "statistics.h"

#include<memory>
#include<string>
#include<vector>

struct StorageTool
{
    virtual RC listBeakFiles(Storage *storage,
                             std::vector<TarFileName> *files,
                             std::vector<TarFileName> *bad_files,
                             std::vector<std::string> *other_files) = 0;
    virtual RC sendBeakFilesToStorage(Path *dir, Storage *storage, std::vector<TarFileName*> *files) = 0;
    virtual RC fetchBeakFilesFromStorage(Storage *storage, std::vector<TarFileName*> *files, Path *dir) = 0;

    virtual void addForwardWork(ptr<StoreStatistics> st, Path *path, FileStat *stat, Options *settings) = 0;

    virtual ptr<FileSystem> fs() = 0;
};


std::unique_ptr<StorageTool> newStorageTool(ptr<System> sys,
                                            ptr<FileSystem> sys_fs,
                                            ptr<FileSystem> storage_fs);

#endif
