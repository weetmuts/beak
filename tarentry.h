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

#include<assert.h>

#include"libtar.h"
#include"tarfile.h"

#include<fcntl.h>

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
    // The hash is used to spread the files into tars.
    uint32_t tarpath_hash;
    // The target file stored inside a symbolic link.
    string link;
   
    struct stat sb;
    int tflag;
    TarFile *tar_file;
    size_t size;
    size_t blocked_size;
    size_t disk_size;

    // If this is a directory, then all children sizes are summed here.
    size_t children_size;
    size_t chunked_size;
    TarEntry *parent;
    TAR *tar;
    bool is_chunk_point;
    vector<TarEntry*> dirs; // Directories to be listed inside this TarEntry
    vector<string> files; // Files to be listed inside this TarEntry (ie the virtual tar files..)
    size_t num_tars = 0;
    TarFile dir_tar;
    map<size_t,TarFile> small_tars;  // Small file tars in side this TarEntry
    map<size_t,TarFile> medium_tars;  // Medium file tars in side this TarEntry   
    map<size_t,TarFile> large_tars;  // Large file tars in side this TarEntry   
    vector<TarEntry*> entries; // The contents stored in the tar files.
    TarEntry *chunk_point = NULL;
    bool added_to_directory = false;
    int depth = 0;
    
    TarEntry() { }
    TarEntry(string p, struct stat b, int f, string root_dir);    
    void removePrefix(size_t len);    
    size_t copy(char *buf, size_t size, size_t from);
    const bool isDir();
};
