/*
 Copyright (C) 2019 Fredrik Öhrström

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

#ifndef FILETYPE_H
#define FILETYPE_H

#include"always.h"
#include"filesystem.h"

#define LIST_OF_FILETYPES \
    X(Source) \
    X(Document) \
    X(Build) \
    X(Object) \
    X(Library) \
    X(Executable) \
    X(VCS) \
    X(Runtime) \
    X(Video) \
    X(Picture) \
    X(DiskImage) \
    X(Other)

enum class FileType : short {
#define X(name) name,
LIST_OF_FILETYPES
#undef X
};

struct FileInfo
{
    FileType type;
    // The identifier is interned. Ie, it will be a constant unique pointer,
    // ie a c string "c" for c files, "h" for h files and "Makefile" for makefiles etc.
    const char * const identifier; // suffix (c,h,tex,java) or whole file (Makefile)
};

FileInfo fileInfo(Path *p);
const char *fileTypeName(FileType ft);

#endif
