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

#include <fuse/fuse.h>
#include <pthread.h>
#include <stddef.h>
#include <sys/stat.h>
#include <ctime>
#include <map>
#include <set>
#include <string>
#include <vector>

#include "libtar/lib/libtar.h"
#include "util.h"

using namespace std;

struct Entry
{
    bool isLnk() {
        return (bool) S_ISLNK(mode_bits);
    }
    bool isDir() {
        return (bool) S_ISDIR(mode_bits);
    }
    
    Entry(mode_t m, size_t s, size_t o, Path *p) :
    mode_bits(m), size(s), offset(o), path(p) {
        msecs = asecs = csecs = 0;
        mnanos = ananos = cnanos = 0;
        is_sym_link = false;
        loaded = false;
    }
    
    Entry() { }

    mode_t mode_bits;
    time_t msecs, asecs, csecs;
    long   mnanos, ananos, cnanos;
    size_t size, offset;
    Path *path;
    string tar;
    vector<Entry*> dir;
    string link;
    bool is_sym_link;
    bool loaded;
};

enum PointInTimeFormat {
    absolute_point,
    relative_point,
    both_point
};

struct PointInTime {
    int key;
    struct timespec ts;
    string ago;
    string datetime;
    string direntry;
    string filename;

    map<Path*,Entry> entries_;   
    map<Path*,Path*> gz_files_;
    set<Path*> loaded_gz_files_;
};

struct ReverseTarredFS
{
    pthread_mutex_t global;
    
    Entry *findEntry(PointInTime *point, Path *path);

    int getattrCB(const char *path, struct stat *stbuf);
    int readdirCB(const char *path, void *buf, fuse_fill_dir_t filler,
                  off_t offset, struct fuse_file_info *fi);
    int readCB(const char *path, char *buf, size_t size, off_t offset,
               struct fuse_file_info *fi);
    int readlinkCB(const char *path, char *buf, size_t s);
    
    int parseTarredfsContent(PointInTime *point, vector<char> &v, vector<char>::iterator &i,
                             Path *dir_to_prepend);
    int parseTarredfsTars(PointInTime *point, vector<char> &v, vector<char>::iterator &i);
    bool loadGz(PointInTime *point, Path *gz, Path *dir_to_prepend);
    void loadCache(PointInTime *point, Path *path);

    bool lookForPointsInTime(PointInTimeFormat f, Path *src);
    vector<PointInTime> &history() { return history_; }
    PointInTime *findPointInTime(string s);    
    bool setPointInTime(string g);
    
    ReverseTarredFS();

    Path *rootDir() { return root_dir_; }
    Path *mountDir() { return mount_dir_; }
    void setRootDir(Path *p) { root_dir_ = p; }
    void setMountDir(Path *p) { mount_dir_ = p; }
    
    private:

    Path *root_dir_;
    Path *mount_dir_;

    vector<PointInTime> history_;
    map<string,PointInTime*> points_in_time_;
    PointInTime *single_point_in_time_;
    PointInTime *most_recent_point_in_time_;
};

#endif
