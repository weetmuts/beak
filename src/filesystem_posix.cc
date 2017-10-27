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

#include "filesystem.h"

#include <ftw.h>

struct FileSystemImplementation : FileSystem
{
    std::vector<Path*> readdir(Path *p);
    std::vector<char> pread(Path *p, size_t count, off_t offset);
    
private:

    Path *root;
    Path *cache;
};

std::unique_ptr<FileSystem> newDefaultFileSystem()
{
    return std::unique_ptr<FileSystem>(new FileSystemImplementation());
}

std::vector<Path*> FileSystemImplementation::readdir(Path *p)
{
    vector<Path*> v;
    return v;
}

std::vector<char> FileSystemImplementation::pread(Path *p, size_t count, off_t offset)
{
    vector<char> v;
    return v;
}
 
