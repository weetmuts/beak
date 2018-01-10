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

#ifndef REVERSE_H
#define REVERSE_H

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
    Entry(FileStat s, size_t o, Path *p) :
    fs(s), offset(o), path(p) {
        is_sym_link = false;
        loaded = false;
    }

    Entry() { }

    FileStat fs;
    size_t offset;
    Path *path;
    std::string tar;
    std::vector<Entry*> dir;
    std::string link;
    bool is_sym_link;
    bool loaded;

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

    std::map<Path*,Entry> entries_;
    std::map<Path*,Path*> gz_files_;
    std::set<Path*> loaded_gz_files_;
};

struct ReverseTarredFS
{
    RC scanFileSystem(Options *settings);

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
    void loadCache(PointInTime *point, Path *path);

    PointInTime *singlePointInTime() { return single_point_in_time_; }
    bool lookForPointsInTime(PointInTimeFormat f, Path *src);
    std::vector<PointInTime> &history() { return history_; }
    PointInTime *findPointInTime(std::string s);
    bool setPointInTime(std::string g);

    ReverseTarredFS(FileSystem *fs);

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

    FileSystem *file_system_;
};

std::unique_ptr<ReverseTarredFS> newReverseTarredFS(FileSystem *fs);

#endif
