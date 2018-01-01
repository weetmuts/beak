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

#ifndef FILESYSTEM_WINAPI_H
#define FILESYSTEM_WINAPI_H

typedef int uid_t;
typedef int gid_t;
typedef int nlink_t;

enum FileTypes {
    REGTYPE,
    DIRTYPE,
    LNKTYPE,
    SYMTYPE,
    CHRTYPE,
    BLKTYPE,
    FIFTYPE
};


#define S_IFLNK  0120000
#define S_IFSOCK 0140000
#define S_ISUID  0004000
#define S_ISGID  0002000
#define S_ISVTX  0001000

/*
#define S_ISLNK(x) false
#define S_ISREG(x) true
#define S_ISCHR(x) true
#define S_ISBLK(x) true
#define S_ISDIR(x) true
#define S_ISFIFO(x) true

#define S_IFDIR  0x00001
#define S_IFLNK  0x00002
#define S_IFCHR  0x00004
#define S_IFBLK  0x00008
#define S_IFIFO  0x00010
#define S_IFSOCK 0x00020
#define S_IFREG  0x00040

#define S_IRUSR  0x00100
#define S_IWUSR  0x00200
#define S_IXUSR  0x00400
#define S_ISUID  0x01000
#define S_IRGRP  0x02000
#define S_IWGRP  0x04000
#define S_IXGRP  0x08000
#define S_ISGID  0x20000
#define S_IROTH  0x40000
#define S_IWOTH  0x80000
#define S_IXOTH  0x100000
#define S_ISVTX  0x200000
*/
ssize_t readlink(const char *path, char *dest, size_t len);

#endif
