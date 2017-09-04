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

#ifndef NOFUSE_H
#define NOFUSE_H

#include<unistd.h>

// When compiling on a platform without fuse support. Use this header instead.

typedef int (*fuse_fill_dir_t)(void *buf, const char *data, void *p, int l);

struct FuseContext {
    void *private_data;
};

FuseContext *fuse_get_context();

struct fuse_operations {
    int (*getattr)(const char *path, struct stat *stbuf);
    int (*readdir)(const char *path, void *buf, fuse_fill_dir_t filler,
                   off_t offset, struct fuse_file_info *fi);
    int (*read)(const char *path, char *buf, size_t size, off_t offset,
                struct fuse_file_info *fi);
    int (*open)(const char *path, struct fuse_file_info *fi);
    int (*readlink)(const char *path, char *buf, size_t size);
};

int fuse_main(int argc, char **argv, fuse_operations *op, void *user_data);

#endif
