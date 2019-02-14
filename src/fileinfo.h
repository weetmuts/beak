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

#define LIST_OF_FILETYPES                 \
    X(Source,source,sources)              \
    X(Config,config file,config files)    \
    X(Data,data file,data files)          \
    X(Document,document,documents)        \
    X(Build,build file,build files)       \
    X(Object,object file,object files)    \
    X(Library,library,libraries)          \
    X(Executable,executable,executables)  \
    X(VCS,vcs file,vcs files)             \
    X(Web,web file,web files)             \
    X(Archive,archive,archives)           \
    X(Runtime,runtime file,runtime files) \
    X(Audio,audio file,audio files)       \
    X(Video,video,videos)                 \
    X(Image,image,images)                 \
    X(DiskImage,disk image,disk images)   \
    X(Other,other file,other files)       \
    X(OtherDir,directory,directories)

enum class FileType : short {
#define X(type,name,names) type,
LIST_OF_FILETYPES
#undef X
};

struct FileInfo
{
    FileType type;
    // The identifier is interned. Ie, it will be a constant unique pointer,
    // ie a c string "c" for c files, "h" for h files and "Makefile" for makefiles etc.
    const char * const identifier; // suffix (c,h,tex,java) or whole file (Makefile)
    const char * const name; // source, library, other file
    const char * const names; // sources, libraries, other files
};

FileInfo fileInfo(Path *p);
const char *fileTypeName(FileType ft, bool pluralis);

#endif
