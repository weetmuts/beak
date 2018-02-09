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

#include <string>
#include <vector>

struct StorageTool
{
    static Storage checkFileSystemStorage(ptr<System> sys, std::string name);
    static Storage checkRCloneStorage(ptr<System> sys, std::string name);
    static Storage checkRSyncStorage(ptr<System> sys, std::string name);

    /*
    virtual RC listBeakFiles(std::vector<TarFileName> *files) = 0;
    virtual RC sendBeakFilesToStorageFrom(Argument from) = 0;
    virtual RC fetchBeakFilesFromStorageTo(Argument to) = 0;*/
};

#endif
