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

#include <dirent.h>
#include <fcntl.h>
#include <ftw.h>
#include <sys/types.h>
#include <unistd.h>

struct FileSystemImplementation : FileSystem
{
    bool readdir(Path *p, std::vector<Path*> *vec);
    ssize_t pread(Path *p, char *buf, size_t count, off_t offset);
    void recurse(function<void(Path *p)> cb);
    
private:

    Path *root;
    Path *cache;
};

std::unique_ptr<FileSystem> newDefaultFileSystem()
{
    return std::unique_ptr<FileSystem>(new FileSystemImplementation());
}

bool FileSystemImplementation::readdir(Path *p, std::vector<Path*> *vec)
{    
    DIR *dp = NULL;
    struct dirent *dptr = NULL;

    if (NULL == (dp = opendir(p->c_str())))
    {
        return false;
    }
    while(NULL != (dptr = ::readdir(dp)))
    {
        vec->push_back(Path::lookup(dptr->d_name));
    }
    closedir(dp);
    
    return true;
}

ssize_t FileSystemImplementation::pread(Path *p, char *buf, size_t size, off_t offset)
{
    int fd = open(p->c_str(), O_RDONLY | O_NOATIME);
    if (fd == -1) return ERR;
    int rc = ::pread(fd, buf, size, offset);
    close(fd);
    return rc;
}

void FileSystemImplementation::recurse(function<void(Path *p)> cb)
{
    //int rc = nftw(root_dir.c_str(), addEntry, 256, FTW_PHYS|FTW_ACTIONRETVAL);
}

