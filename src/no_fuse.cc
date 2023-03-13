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

#include "no_fuse.h"

FuseContext *fuse_get_context() {
    return NULL;
}

int fuse_main(int argc, char **argv, fuse_operations *op, void *user_data)
{
    return 0;
}

struct fuse * fuse_new (struct fuse_chan *chan, struct fuse_args *args,
                        const struct fuse_operations *op, size_t op_size, void *private_data)
{
    return NULL;
}

void fuse_exit(struct fuse *fuse)
{
}

struct fuse_chan *fuse_mount (const char *mountpoint, struct fuse_args *args)
{
    return NULL;
}

void fuse_unmount(const char *s, struct fuse_chan *chan)
{
}

int fuse_loop_mt(struct fuse *f)
{
    return 0;
}
