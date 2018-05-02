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
#include "tarfile.h"

#include<memory>
#include<string>
#include<vector>

struct StoreStatistics
{
    size_t num_files, size_files, num_dirs;
    size_t num_hard_links, num_symbolic_links, num_nodes;

    size_t num_files_to_store, size_files_to_store;
    size_t num_files_stored, size_files_stored;
    size_t num_dirs_updated;

    size_t num_files_handled, size_files_handled;
    size_t num_dirs_handled;

    size_t num_hard_links_stored;
    size_t num_symbolic_links_stored;
    size_t num_device_nodes_stored;

    size_t num_total, num_total_handled;

    uint64_t prev, start;
    bool info_displayed;

    StoreStatistics();
    void displayProgress();
    void finishProgress();
};

struct StorageTool
{
    virtual RC listBeakFiles(Storage *storage,
                             std::vector<TarFileName> *files,
                             std::vector<TarFileName> *bad_files,
                             std::vector<std::string> *other_files) = 0;
    virtual RC sendBeakFilesToStorage(Path *dir, Storage *storage, std::vector<TarFileName*> *files) = 0;
    virtual RC fetchBeakFilesFromStorage(Storage *storage, std::vector<TarFileName*> *files, Path *dir) = 0;
};

std::unique_ptr<StorageTool> newStorageTool(ptr<System> sys, ptr<FileSystem> fs);


#endif
