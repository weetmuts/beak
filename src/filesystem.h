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

#include <functional>
#include <memory>
#include <string>
#include <vector>

#ifdef FUSE_USE_VERSION
#include <fuse/fuse.h>
#else
#include "nofuse.h"
#endif

struct FuseAPI {
    virtual int getattrCB(const char *path,
                          struct stat *stbuf) = 0;
    virtual int readdirCB(const char *path,
                          void *buf,
                          fuse_fill_dir_t filler,
                          off_t offset,
                          struct fuse_file_info *fi) = 0;
    virtual int readCB(const char *path,
                       char *buf,
                       size_t size,
                       off_t offset,
                       struct fuse_file_info *fi) = 0;    
    virtual int readlinkCB(const char *path_char_string,
                           char *buf,
                           size_t s) = 0;
};

struct FileSystem
{
    virtual bool readdir(Path *p, std::vector<Path*> *vec) = 0;
    virtual ssize_t pread(Path *p, char *buf, size_t size, off_t offset) = 0;
    virtual void recurse(function<void(Path *p)> cb) = 0;
};

std::unique_ptr<FileSystem> newDefaultFileSystem();
std::unique_ptr<FileSystem> newFileSystem(FuseAPI *api);

#endif
