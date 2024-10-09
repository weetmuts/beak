/*
 Copyright (C) 2018-2024 Fredrik Öhrström

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

#include "always.h"

#include "configuration.h"
#include "monitor.h"
#include "system.h"
#include "tarfile.h"

#include <map>
#include <string>
#include <vector>

bool gphoto2Detect(System *sys);
std::string gphoto2EstablishAccess(System *sys);
std::string gphoto2ReEstablishAccess(System *sys, bool hint_unplug);

RC gphoto2ListBeakFiles(Storage *storage,
                       std::vector<TarFileName> *files,
                       std::vector<TarFileName> *bad_files,
                       std::vector<std::string> *other_files,
                       std::map<Path*,FileStat> *contents,
                       ptr<System> sys,
                       ProgressStatistics *progress);

RC gphoto2FetchFiles(Storage *storage,
                    std::vector<Path*> *files,
                    Path *local_dir,
                    System *sys,
                    FileSystem *local_fs,
                    ProgressStatistics *progress);

RC gphoto2SendFiles(Storage *storage,
                   std::vector<Path*> *files,
                   Path *local_dir,
                   FileSystem *local_fs,
                   ptr<System> sys,
                   ProgressStatistics *progress);

RC gphoto2DeleteFiles(Storage *storage,
                     std::vector<Path*> *files,
                     FileSystem *local_fs,
                     ptr<System> sys,
                     ProgressStatistics *progress);

RC gphoto2ListFiles(Storage *storage,
                   std::map<Path*,FileStat> *contents,
                   ptr<System> sys,
                   ProgressStatistics *st);
