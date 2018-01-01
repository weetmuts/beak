/*
 Copyright (C) 2016-2017 Fredrik Öhrström

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

#ifndef TAR_H
#define TAR_H

#include"filesystem.h"

#include<string>
#include<sys/stat.h>

#define T_BLOCKSIZE		512

struct sparse
{
  char offset[12];
  char numbytes[12];
};

#define SPARSES_IN_OLDGNU_HEADER 4

struct TarHeaderContents {
    // GNU Header style
    char name_[100];
    char mode_[8];
    char uid_[8];
    char gid_[8];
    char size_[12];
    char mtime_[12];
    char checksum_[8];
    char typeflag_;
    char linkname_[100];
    char magic_[6];
    char version_[2];
    char uname_[32];
    char gname_[32];
    char devmajor_[8];
    char devminor_[8];
    char atime[12]; // For incremental archives
    char ctime[12]; // For incremental archives
    char offset[12]; // For multivolume archives
    char longnames[4]; // Not used
    char padding1;
    struct sparse sp[SPARSES_IN_OLDGNU_HEADER];
    char isextended;
    char realsize[12];
    char padding2[17];
};

struct TarHeader
{
    private:

    union {
        unsigned char buf[T_BLOCKSIZE];
        TarHeaderContents members;
    } content;

    size_t num_long_path_blocks_;
    size_t num_long_link_blocks_;
    size_t num_header_blocks_;

    public:

    static size_t calculateSize(FileStat *fs, Path *tarpath, Path *link, bool is_hard_link);
    TarHeader();
    TarHeader(FileStat *fs, Path *tar, Path *link, bool is_hard_link, bool full);

    char *buf() { return content.members.name_; }
    char type() { return content.members.typeflag_; }
    void setLongLinkType(TarHeader *file);
    void setLongPathType(TarHeader *file);
    void setSize(size_t s);

    void calculateChecksum();

    size_t numLongPathBlocks() { return num_long_path_blocks_; }
    size_t numLongLinkBlocks() { return num_long_link_blocks_; }
    size_t numHeaderBlocks() { return num_header_blocks_; }
};

#endif
