/*  
    Copyright (C) 2016 Fredrik Öhrström

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

#ifndef TARENTRY_H
#define TARENTRY_H

#include<assert.h>

#include"libtar.h"
#include"tarfile.h"

#include<map>
#include<string>
#include<vector>

using namespace std;

struct TarEntry {

    int num_long_path_blocks;
    int num_long_link_blocks;
    int num_header_blocks;
    size_t header_size;

    // Full path and name, to read the file from the underlying file system.
    string abspath;
    // Just the name of the file.
    string name;
    // The path below root_dir, starts with a /.
    string path;
    // The path inside the tar, does not start with a /.
    // And can be much shorter than path, because the tar can be located
    // deep in the tree below root_dir.
    string tarpath;
    // The hash of the tarpath is used to spread the files into tars.
    uint32_t tarpath_hash;
    // The target file stored inside a symbolic link.
    string link;

    // This is a re-construction of how the entry would be listed by "tar tv"
    // tv_line_left is accessbits, ownership
    // tv_line_size is the size, to be left padded with space
    // tv_line_right is the last modification time
    string tv_line_left, tv_line_size, tv_line_right;

    struct stat sb;
    TarFile *tar_file;
    size_t tar_offset;
    size_t size;
    size_t blocked_size;
    size_t disk_size;

    // If this is a directory, then all children sizes are summed here.
    size_t children_size;
    TarEntry *parent;
    TAR *tar;
    bool is_tar_storage_dir;
    vector<TarEntry*> dirs; // Directories to be listed inside this TarEntry
    vector<string> files; // Files to be listed inside this TarEntry (ie the virtual tar files..)
    size_t num_tars = 0;
    TarFile dir_tar;
    bool dir_tar_in_use = false;
    map<size_t,TarFile> small_tars;  // Small file tars in side this TarEntry
    map<size_t,TarFile> medium_tars;  // Medium file tars in side this TarEntry   
    map<size_t,TarFile> large_tars;  // Large file tars in side this TarEntry   
    vector<TarEntry*> entries; // The contents stored in the tar files.
    TarEntry *tar_collection_dir = NULL; // This entry is stored in this tar collection dir.
    bool added_to_directory = false;
    int depth = 0;
    bool virtual_file = false;
    string content;

    TarEntry() { }
    TarEntry(string p, const struct stat *b, string root_dir, bool header = false);    
    void removePrefix(size_t len);
    void setContent(string c);
    size_t copy(char *buf, size_t size, size_t from);
    const bool isDir();
};

#endif
