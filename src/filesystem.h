/*
 Copyright (C) 2017 Fredrik Öhrström

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

#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include "util.h"

#include <memory>
#include <string>
#include <vector>

struct FileSystem
{
    virtual std::vector<Path*> readdir(Path *p) = 0;
    virtual std::vector<char> pread(Path *p, size_t count, off_t offset) = 0;
};

std::unique_ptr<FileSystem> newDefaultFileSystem();

#endif
