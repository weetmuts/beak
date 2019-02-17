/*
 Copyright (C) 2016-2018 Fredrik Öhrström

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

#ifndef RESTORE_H
#define RESTORE_H

#include "always.h"
#include "beak.h"

#ifdef FUSE_USE_VERSION
#include <fuse/fuse.h>
#else
#include "nofuse.h"
#endif

#include <pthread.h>
#include <stddef.h>
#include <sys/stat.h>
#include <ctime>
#include <map>
#include <set>
#include <string>
#include <utility>
#include <vector>

#include "index.h"
#include "tar.h"
#include "tarfile.h"
#include "util.h"

struct RestoreEntry
{
    FileStat fs;
    Path *path {};
    Path *tar {};
    std::vector<RestoreEntry*> dir;
    bool is_sym_link {};
    // A symbolic link can be anything! Must not point to a real file.
    std::string symlink;
    // A hard link always points a real file stored in the same directory or in a subdirectory.
    // The actual pointer is stored in the FileStat.
    size_t offset_ {};
    uint num_parts {};
    size_t part_offset {};
    size_t part_size {};
    size_t last_part_size {};
    bool loaded {};
    UpdateDisk disk_update {};

    RestoreEntry() {}
    RestoreEntry(FileStat s, size_t o, Path *p) : fs(s), path(p), offset_(o) {}
    void loadFromIndex(IndexEntry *ie);
    bool findPartContainingOffset(size_t file_offset, uint *partnr, size_t *offset_inside_part);
    size_t lengthOfPart(uint partnr);
    ssize_t readParts(off_t file_offset, char *buffer, size_t length,
                   std::function<ssize_t(uint partnr, off_t offset_inside_part, char *buffer, size_t length)> cb);

};

enum PointInTimeFormat : short {
    absolute_point,
    relative_point,
    both_point
};

struct PointInTime {
    int key;
    std::string ago;
    std::string datetime;
    std::string direntry;
    std::string filename;

    bool hasPath(Path *p) { return entries_.count(p) == 1; }
    RestoreEntry *getPath(Path *p) { if (hasPath(p)) { return &entries_[p]; } else { return NULL; } }
    RestoreEntry *addPath(Path *p) {
        assert(entries_.count(p) == 0);
        entries_[p] = RestoreEntry();
        return &entries_[p];
    }
    void addTar(Path *p) {
        tars_.push_back(p);
    }
    bool hasLoadedGzFile(Path *gz) { return loaded_gz_files_.count(gz) == 1; }
    void addLoadedGzFile(Path *gz) { loaded_gz_files_.insert(gz); }
    bool hasGzFiles() { return gz_files_.size() != 0; }
    void addGzFile(Path *parent, Path *p) { gz_files_[parent] = p; }
    Path *getGzFile(Path *p) { if (gz_files_.count(p) == 1) { return gz_files_[p]; } else { return NULL; } }
    std::vector<Path*> *tars() { return &tars_; }

    const struct timespec *ts() { return &ts_; }
    uint64_t point() { return point_; }

    PointInTime(time_t sec, unsigned int nsec) {
        ts_.tv_sec = sec;
        ts_.tv_nsec = nsec;
        point_ = 1000000000ull*sec + nsec;
    }
private:

    struct timespec ts_;
    uint64_t point_;
    std::vector<Path*> tars_;
    std::map<Path*,RestoreEntry,depthFirstSortPath> entries_;
    std::map<Path*,Path*> gz_files_;
    std::set<Path*> loaded_gz_files_;
};

struct Restore
{
    RC loadBeakFileSystem(Argument *storage);

    pthread_mutex_t global;
    pthread_mutexattr_t global_attr;

    RestoreEntry *findEntry(PointInTime *point, Path *path);

    int getattrCB(const char *path, struct stat *stbuf);
    int readdirCB(const char *path, void *buf, fuse_fill_dir_t filler,
                  off_t offset, struct fuse_file_info *fi);
    int readCB(const char *path, char *buf, size_t size, off_t offset,
               struct fuse_file_info *fi);
    int readlinkCB(const char *path, char *buf, size_t s);

    bool loadGz(PointInTime *point, Path *gz, Path *dir_to_prepend);

    Path *loadDirContents(PointInTime *point, Path *path);
    void loadCache(PointInTime *point, Path *path);

    PointInTime *singlePointInTime() { return single_point_in_time_; }
    PointInTime *mostRecentPointInTime() { return most_recent_point_in_time_; }
    RC lookForPointsInTime(PointInTimeFormat f, Path *src);
    std::vector<PointInTime> &history() { return history_; }
    PointInTime *findPointInTime(std::string s);
    PointInTime *setPointInTime(std::string g);

    Restore(FileSystem *backup_fs);

    Path *rootDir() { return root_dir_; }
    void setRootDir(Path *p) { root_dir_ = p; }

    ptr<FileSystem> asFileSystem() { return contents_fs_; }
    FuseAPI *asFuseAPI();
    FileSystem *backupFileSystem() { return backup_fs_; }

    private:

    Path *root_dir_;

    std::vector<PointInTime> history_;
    std::map<std::string,PointInTime*> points_in_time_;
    PointInTime *single_point_in_time_;
    PointInTime *most_recent_point_in_time_;

    // This is the file system where the backup containing beak files are stored.
    // It can point directly to the default OS file system or to a cached
    // storage tool file system.
    FileSystem *backup_fs_;

    std::unique_ptr<FileSystem> contents_fs_;
};

// Restore from a file system containing a backup full of beak files
std::unique_ptr<Restore> newRestore(ptr<FileSystem> backup_fs);

#endif
