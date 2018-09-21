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

#include "tar.h"
#include "util.h"

struct Entry
{
    FileStat fs;
    size_t offset;
    Path *path;
    Path *tar;
    std::vector<Entry*> dir;
    bool is_sym_link;
    // A symbolic link can be anything! Must not point to a real file.
    std::string symlink;
    bool is_hard_link;
    // A hard link always points a real file stored in the same directory or in a subdirectory.
    Path *hard_link;
    bool loaded;
    UpdateDisk disk_update;

    Entry() : offset(0), path(0), tar(0), is_sym_link(false), is_hard_link(false),
        hard_link(0), loaded(false), disk_update(NoUpdate) { }
    Entry(FileStat s, size_t o, Path *p) : fs(s), offset(o), path(p),
        is_sym_link(false), is_hard_link(false), loaded(false),
        disk_update(NoUpdate) { }

    void checkStat(FileSystem *dst, Path *target); // Compare with stat of target and set disk_update properly.
};

enum PointInTimeFormat : short {
    absolute_point,
    relative_point,
    both_point
};

struct PointInTime {
    int key;
    struct timespec ts;
    std::string ago;
    std::string datetime;
    std::string direntry;
    std::string filename;

    std::map<Path*,Entry,depthFirstSortPath> entries_;
    std::map<Path*,Path*> gz_files_;
    std::set<Path*> loaded_gz_files_;
};

struct Restore
{
    RC loadBeakFileSystem(Settings *settings);

    pthread_mutex_t global;

    Entry *findEntry(PointInTime *point, Path *path);

    int getattrCB(const char *path, struct stat *stbuf);
    int readdirCB(const char *path, void *buf, fuse_fill_dir_t filler,
                  off_t offset, struct fuse_file_info *fi);
    int readCB(const char *path, char *buf, size_t size, off_t offset,
               struct fuse_file_info *fi);
    int readlinkCB(const char *path, char *buf, size_t s);

    int parseTarredfsContent(PointInTime *point, std::vector<char> &v, std::vector<char>::iterator &i,
                             Path *dir_to_prepend);
    int parseTarredfsTars(PointInTime *point, std::vector<char> &v, std::vector<char>::iterator &i);
    bool loadGz(PointInTime *point, Path *gz, Path *dir_to_prepend);
    Path *loadDirContents(PointInTime *point, Path *path);
    void loadCache(PointInTime *point, Path *path);

    PointInTime *singlePointInTime() { return single_point_in_time_; }
    RC lookForPointsInTime(PointInTimeFormat f, Path *src);
    std::vector<PointInTime> &history() { return history_; }
    PointInTime *findPointInTime(std::string s);
    PointInTime *setPointInTime(std::string g);

    Restore(ptr<FileSystem> fs);

    Path *rootDir() { return root_dir_; }
    Path *mountDir() { return mount_dir_; }
    void setRootDir(Path *p) { root_dir_ = p; }
    void setMountDir(Path *p) { mount_dir_ = p; }

    FileSystem *asFileSystem();

    private:

    Path *root_dir_;
    Path *mount_dir_;

    std::vector<PointInTime> history_;
    std::map<std::string,PointInTime*> points_in_time_;
    PointInTime *single_point_in_time_;
    PointInTime *most_recent_point_in_time_;

    // This is the file system where the backup containing beak files are stored.
    // It can point directly to the default OS file system or to a cached
    // storage tool file system.
    FileSystem *backup_file_system_;
};

// Restore from a file system containing a backup full of beak files
std::unique_ptr<Restore> newRestore(ptr<FileSystem> backup_fs);

#endif
