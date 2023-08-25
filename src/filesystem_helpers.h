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

struct ReadOnlyFileSystem : FileSystem
{
    // The base provides nop implementations for all write functions.
    // I.e. inherit this class and you got a read-only file system.

    RC chmod(Path *p, FileStat *stat);
    RC utime(Path *p, FileStat *stat);
    Path *tempDir();
    Path *mkTempFile(std::string prefix, std::string content);
    Path *mkTempDir(std::string prefix);
    Path *mkDir(Path *path, std::string name, int permissions);
    RC rmDir(Path *path);
    RC createFile(Path *file, std::vector<char> *buf);
    bool createFile(Path *path, FileStat *stat,
                    std::function<size_t(off_t offset, char *buffer, size_t len)> cb);
    bool createSymbolicLink(Path *path, FileStat *stat, std::string target);
    bool createHardLink(Path *path, FileStat *stat, Path *target);
    bool createFIFO(Path *path, FileStat *stat);
    bool deleteFile(Path *file);
    void allowAccessTimeUpdates();
    RC enableWatch();
    RC addWatch(Path *dir);
    int endWatch();

    RC mountDaemon(Path *dir, FuseAPI *fuseapi, bool foreground=false, bool debug=false);
    std::unique_ptr<FuseMount> mount(Path *dir, FuseAPI *fuseapi, bool debug=false);
    RC umount(ptr<FuseMount> fuse_mount);

    ReadOnlyFileSystem(const char *n) : FileSystem(n) {}
};

struct StatOnlyFileSystem : ReadOnlyFileSystem
{
StatOnlyFileSystem(System *sys, std::map<Path*,FileStat> &contents) : ReadOnlyFileSystem("StatOnlyFileSystem"), contents_(contents) { }

    bool readdir(Path *p, std::vector<Path*> *vec);
    ssize_t pread(Path *p, char *buf, size_t count, off_t offset);
    RC recurse(Path *root, std::function<RecurseOption(Path *path, FileStat *stat)> cb);
    RC recurse(Path *root, std::function<RecurseOption(const char *path, const struct stat *sb)> cb);
    RC ctimeTouch(Path *p);

    RC stat(Path *p, FileStat *fs);
    RC loadVector(Path *file, size_t blocksize, std::vector<char> *buf);
    bool readLink(Path *file, std::string *target);
    FILE *openAsFILE(Path *f, const char *mode);

    protected:

    std::map<Path*,FileStat> contents_;
};

struct CacheEntry
{
    FileStat stat;
    Path *path {};
    bool cached {}; // Have we a cached version of this file/dir?
    std::map<Path*,CacheEntry*> direntries; // If this is a directory, list its contents here.

    CacheEntry() { }
    CacheEntry(FileStat s, Path *p, bool c) : stat(s), path(p), cached(c) { }

    // Check if the file exists in the cache, and the file has the correct size
    // and ownership settings, if so return true, because we believe
    // it is a properly cached file. If not, then return false, the cache is
    // empty or broken.
    bool isCached(FileSystem *cache_fs, Path *cache_dir, Path *f);
};

// The cached file system base implementation can only cache plain files.
// The cache is used to cache beak backup files: .tar files and and .gz index files
// fetched from a remote storage location.

struct ReadOnlyCacheFileSystemBaseImplementation : ReadOnlyFileSystem
{
    ReadOnlyCacheFileSystemBaseImplementation(const char *name,
                                              ptr<FileSystem> cache_fs,
                                              Path *cache_dir,
                                              int depth,
                                              Monitor *monitor) :
    ReadOnlyFileSystem(name), cache_fs_(cache_fs), cache_dir_(cache_dir),drop_prefix_depth_(depth), monitor_(monitor) {}

    virtual void refreshCache() = 0;

    // Implement the two following methods to complete your cached filesystem.

    // Store the entiry directory structure of the filesystem you want to cache in
    // the supplied entries map.
    virtual RC loadDirectoryStructure(std::map<Path*,CacheEntry> *entries) = 0;

    // Fetch a file to be cached and store it in the cache_fs_:cache_dir_ + file
    virtual RC fetchFile(Path *file) = 0;
    virtual RC fetchFiles(std::vector<Path*> *files) = 0;

    // The base provides implementations for the file system api below.
    bool readdir(Path *p, std::vector<Path*> *vec);
    ssize_t pread(Path *p, char *buf, size_t count, off_t offset);
    RC recurse(Path *root, std::function<RecurseOption(Path *path, FileStat *stat)> cb);
    RC recurse(Path *root, std::function<RecurseOption(const char *path, const struct stat *sb)> cb);
    RC ctimeTouch(Path *p);

    RC stat(Path *p, FileStat *fs);
    RC loadVector(Path *file, size_t blocksize, std::vector<char> *buf);
    bool readLink(Path *file, std::string *target);

    protected:

    ptr<FileSystem> cache_fs_ {};
    Path *cache_dir_ {};
    std::map<Path*,CacheEntry> entries_;
    int drop_prefix_depth_ {};
    bool fileCached(Path *p);
    CacheEntry *cacheEntry(Path *p);
    Monitor *monitor_ {};

    RecurseOption recurse_helper_(Path *root, std::function<RecurseOption(Path *path, FileStat *stat)> cb);
};

struct MapEntry
{
    FileStat stat;
    Path *path {};
    Path *source {};
    // If this is a directory, from is NULL and its contents is listed here instead.
    std::map<Path*,MapEntry*> direntries;

    MapEntry() { }
    MapEntry(FileStat st, Path *p, Path *s) : stat(st), path(p), source(s) { assert(path); }
};

// Restructure an existing filesystem with new filenames/paths and timestamps but
// read content from existing source files.
struct MapFileSystem : ReadOnlyFileSystem
{
MapFileSystem(FileSystem *origin_fs) :
    ReadOnlyFileSystem("MapFileSystem"), origin_fs_(origin_fs)
    {
        FileStat st;
        st.setAsDirectory();
        entries_[Path::lookupRoot()] = MapEntry(st, Path::lookupRoot(), NULL);
    }

    void mapFile(FileStat stat, Path *path, Path *source);
    RecurseOption recurse_helper_(Path *p,
                                  std::function<RecurseOption(Path *path, FileStat *stat)> cb);
    void addDirToParent(Path *dir);

    bool readdir(Path *p, std::vector<Path*> *vec);
    ssize_t pread(Path *p, char *buf, size_t count, off_t offset);
    RC recurse(Path *root, std::function<RecurseOption(Path *path, FileStat *stat)> cb);
    RC recurse(Path *root, std::function<RecurseOption(const char *path, const struct stat *sb)> cb);
    RC ctimeTouch(Path *p);

    RC stat(Path *p, FileStat *fs);
    RC loadVector(Path *file, size_t blocksize, std::vector<char> *buf);
    bool readLink(Path *file, std::string *target);
    FILE *openAsFILE(Path *f, const char *mode);

    protected:

    FileSystem *origin_fs_;
    std::map<Path*,MapEntry> entries_;
};

// A file system to render existing files under new names/paths and stats.
// Add filemappings mapfs->addFile(from, to, filestat);
std::unique_ptr<MapFileSystem> newMapFileSystem(FileSystem *fs);


#endif
