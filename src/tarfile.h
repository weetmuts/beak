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

#include "always.h"
#include "filesystem.h"
#include "util.h"

#include <stddef.h>
#include <cstdint>
#include <ctime>
#include <map>
#include <string>
#include <utility>
#include <vector>
#include <openssl/sha.h>


struct TarEntry;

enum TarContents
{
    REG_FILE, DIR_TAR, SMALL_FILES_TAR, MEDIUM_FILES_TAR, SINGLE_LARGE_FILE_TAR
};

#define REG_FILE_CHAR 'z'
#define DIR_TAR_CHAR 'y'
#define SMALL_FILES_TAR_CHAR 's'
#define MEDIUM_FILES_TAR_CHAR 'm'
#define SINGLE_LARGE_FILE_TAR_CHAR 'l'

struct TarFileName {
    Path *path;
    TarContents type;
    int version;
    time_t secs;
    long nsecs;
    size_t size;
    std::string header_hash;
    std::string content_hash;
    std::string suffix;

    bool equals(TarFileName *tfn) {
        return tfn->type == type &&
            tfn->version == version &&
            tfn->secs == secs &&
            tfn->nsecs == nsecs &&
            tfn->size == size &&
            tfn->header_hash == header_hash &&
            tfn->content_hash == content_hash &&
            tfn->suffix == suffix;
    }

    bool isIndexFile() {
        return type == REG_FILE && suffix == "gz";
    }

    static bool isIndexFile(Path *);

    bool parseFileName(std::string &name);

    static char chartype(TarContents type) {
        switch (type) {
        case REG_FILE: return REG_FILE_CHAR;
        case DIR_TAR: return DIR_TAR_CHAR;
        case SMALL_FILES_TAR: return SMALL_FILES_TAR_CHAR;
        case MEDIUM_FILES_TAR: return MEDIUM_FILES_TAR_CHAR;
        case SINGLE_LARGE_FILE_TAR: return SINGLE_LARGE_FILE_TAR_CHAR;
        }
        return 0;
    }

    static bool typeFromChar(char c, TarContents *tc) {
        switch (c) {
        case REG_FILE_CHAR: *tc = REG_FILE; return true;
        case DIR_TAR_CHAR: *tc = DIR_TAR; return true;
        case SMALL_FILES_TAR_CHAR: *tc = SMALL_FILES_TAR; return true;
        case MEDIUM_FILES_TAR_CHAR: *tc = MEDIUM_FILES_TAR; return true;
        case SINGLE_LARGE_FILE_TAR_CHAR: *tc = SINGLE_LARGE_FILE_TAR; return true;
        }
        return false;
    }

};

struct TarFile
{
    TarFile()
    {
    }
    TarFile(TarEntry *d, TarContents tc, int n);
    std::string name()
    {
        return name_;
    }
    Path* path() {
        return path_;
    }
    size_t size()
    {
        return size_;
    }
    void fixSize()
    {
        size_ = current_tar_offset_;
    }
    void addEntryLast(TarEntry *entry);
    void addEntryFirst(TarEntry *entry);

    void finishHash();
    std::pair<TarEntry*, size_t> findTarEntry(size_t offset);

    void calculateHash();
    void calculateHash(std::vector<TarFile*> &tars, std::string &contents);
    std::vector<char> &hash();

    void fixName();
    void setName(std::string s);

    size_t currentTarOffset()
    {
        return current_tar_offset_;
    }
    struct timespec *mtim()
    {
        return &mtim_;
    }
    void updateMtim(struct timespec *mtim);

    std::string line(Path *p);

    static bool parseFileName(std::string &name, TarFileName *c);

    // Write size bytes of the contents of the tar file into buf,
    // start reading at offest in the tar file.
    size_t copy(char *buf, size_t size, off_t offset, FileSystem *fs);

    // file: Write the tarfile contents into this file.
    // stat: With this size and permissions.
    // src_fs: Fetch the tarfile contents from this filesystem
    // dst_fs: Store into this filesystem
    // off: Start storing from this offset in the tar file.
    bool createFile(Path *file, FileStat *stat,
                    FileSystem *src_fs, FileSystem *dst_fs, size_t off,
                    std::function<void(size_t)> update_progress);

private:

    TarEntry *in_directory_;
    // A virtual tar can contain small files, medium files or a single large file.
    TarContents tar_contents = SMALL_FILES_TAR;

    // Name of the tar, tar00000000.tar taz00000000.tar tal00000000.tar tam00000000.tar
    std::string name_;
    Path *path_;
    uint32_t hash_;
    bool hash_initialized = false;
    size_t size_;
    std::map<size_t, TarEntry*> contents_;
    std::vector<size_t> offsets;
    size_t current_tar_offset_ = 0;
    struct timespec mtim_;
    UpdateDisk disk_update;

    void calculateSHA256Hash();
    void calculateSHA256Hash(std::vector<TarFile*> &tars, std::string &content);

    std::vector<char> sha256_hash_;
};

#endif
