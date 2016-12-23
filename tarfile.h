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

#ifndef TARFILE_H
#define TARFILE_H

#include<assert.h>

#include<map>
#include<string>
#include<vector>

struct TarEntry;

using namespace std;

enum TarContents { DIR_TAR, SMALL_FILES_TAR, MEDIUM_FILES_TAR, SINGLE_LARGE_FILE_TAR };
    
struct TarFile {
    // A virtual tar can contain small files, medium files or a single large file.
    TarContents tar_contents = SMALL_FILES_TAR;    

    // Name of the tar, tar00000000.tar taz00000000.tar tal00000000.tar tam00000000.tar
    string name;
    uint32_t hash;
    bool hash_initialized = false;
    size_t size;
    map<size_t,TarEntry*> contents;
    vector<size_t> offsets;
    size_t tar_offset = 0;
    struct timespec mtim;
    TarEntry *volume_header;
    
    TarFile() { }
    TarFile(TarContents tc, int n, bool dirs);
    void addEntryLast(TarEntry *entry);
    void addEntryFirst(TarEntry *entry);
    void addVolumeHeader();
    void finishHash();
    pair<TarEntry*,size_t> findTarEntry(size_t offset);

    void calculateSHA256Hash();
};

#endif