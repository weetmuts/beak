/*
 Copyright (C) 2018 Fredrik Öhrström

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

#ifndef FILESYSTEM_HELPERS_H
#define FILESYSTEM_HELPERS_H

#include "always.h"

#include "filesystem.h"
#include "restore.h"

#include <vector>
#include <string>

// The cached file system base implementation can only cache plain files.
// The cache is used to cache beak backup files: .tar files and and .gz index files
// fetched from a remote storage location.

struct CacheEntry
{
    FileStat fs;
    Path *path {};
    bool cached {}; // Have we a cached version of this file/dir?
    std::vector<CacheEntry*> direntries; // If this is a directory, list its contents here.

    CacheEntry() { }
    CacheEntry(FileStat s, Path *p, bool c) : fs(s), path(p), cached(c) { }

    // Check if the dst:target exists and the file has the correct size
    // and ownership settings, if so return true, because we believe
    // it is a properly cached file. If not return false, the cache is
    // empty or broken.
    bool isCached(FileSystem *dst, Path *target);
};

struct ReadOnlyCacheFileSystemBaseImplementation : FileSystem
{
    ReadOnlyCacheFileSystemBaseImplementation(ptr<FileSystem> cache_fs, Path *cache_dir) :
    cache_fs_(cache_fs), cache_dir_(cache_dir) {}

    // Implement the two following methods to complete your cached filesystem.

    // Store the entiry directory structure of the filesystem you want to cache in
    // the supplied entries map.
    virtual void loadDirectoryStructure(std::map<Path*,CacheEntry> *entries) = 0;

    // Fetch a file to be cached and store it in the cache_fs_:cache_dir_ + file
    virtual void fetchFile(Path *file) = 0;

    // The base provides implementations for the file system api below.

    bool readdir(Path *p, std::vector<Path*> *vec);
    ssize_t pread(Path *p, char *buf, size_t count, off_t offset);
    void recurse(Path *root, std::function<void(Path *path, FileStat *stat)> cb);
    RC stat(Path *p, FileStat *fs);
    RC chmod(Path *p, FileStat *stat);
    RC utime(Path *p, FileStat *stat);
    Path *mkTempFile(std::string prefix, std::string content);
    Path *mkTempDir(std::string prefix);
    Path *mkDir(Path *path, std::string name);
    RC rmDir(Path *path);
    RC loadVector(Path *file, size_t blocksize, std::vector<char> *buf);
    RC createFile(Path *file, std::vector<char> *buf);
    bool createFile(Path *path, FileStat *stat,
                    std::function<size_t(off_t offset, char *buffer, size_t len)> cb);
    bool createSymbolicLink(Path *path, FileStat *stat, std::string target);
    bool createHardLink(Path *path, FileStat *stat, Path *target);
    bool createFIFO(Path *path, FileStat *stat);
    bool readLink(Path *file, std::string *target);
    bool deleteFile(Path *file);
    RC mountDaemon(Path *dir, FuseAPI *fuseapi, bool foreground=false, bool debug=false);
    std::unique_ptr<FuseMount> mount(Path *dir, FuseAPI *fuseapi, bool debug=false);
    RC umount(ptr<FuseMount> fuse_mount);
    RC enableWatch();
    RC addWatch(Path *dir);
    int endWatch();

    protected:

    ptr<FileSystem> cache_fs_ {};
    Path *cache_dir_ {};
    std::map<Path*,CacheEntry> entries_;

    bool fileCached(Path *p);
    CacheEntry *cacheEntry(Path *p);
};

#endif
