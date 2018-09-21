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

#ifndef BACKUP_H
#define BACKUP_H

#include "always.h"
#include "beak.h"
#include "filesystem.h"
#include "match.h"
#include "tarentry.h"
#include "util.h"

#ifdef FUSE_USE_VERSION
#include <fuse/fuse.h>
#else
#include "nofuse.h"
#endif
#include <pthread.h>
#include <stddef.h>
#include <sys/types.h>
#include <map>
#include <string>
#include <utility>
#include <vector>

enum FilterType { INCLUDE, EXCLUDE };

struct Filter {
    std::string rule;
    FilterType type;

    Filter(std::string rule_, FilterType type_) : rule(rule_), type(type_) { }
};

typedef int (*FileCB)(const char *,const struct stat *,int,struct FTW *);
typedef int (*GetAttrCB)(const char*, struct stat*);
typedef int (*ReaddirCB)(const char*,void*,fuse_fill_dir_t,off_t,struct fuse_file_info*);
typedef int (*ReadCB)(const char *,char *,size_t,off_t,struct fuse_file_info *);

struct Backup : FuseAPI
{
    RC scanFileSystem(Options *settings);

    pthread_mutex_t global;
    pthread_mutexattr_t global_attr;

    std::string root_dir;
    Path *root_dir_path;
    std::string mount_dir;
    Path *mount_dir_path;

    size_t target_target_tar_size = DEFAULT_TARGET_TAR_SIZE;
    size_t tar_trigger_size = DEFAULT_TAR_TRIGGER_SIZE;
    size_t target_split_tar_size = DEFAULT_SPLIT_TAR_SIZE;
    // The default setting is to trigger tars in each subdirectory below the root.
    // Even if the subdir does not qualify with enough data to create a min tar file.
    // However setting this to 1 and setting trigger size to 0, puts all content in
    // tars directly below the mount dir, ie no subdirs, only tars.
    int forced_tar_collection_dir_depth = 2;

    std::map<Path*,TarEntry*,depthFirstSortPath> files;
    std::map<Path*,TarEntry*,depthFirstSortPath> tar_storage_directories;
    std::map<Path*,TarEntry*> directories;
    std::map<ino_t,TarEntry*> hard_links; // Only inodes for which st_nlink > 1
    size_t hardlinksavings = 0;

    std::vector<std::pair<Filter,Match>> filters;
    std::vector<Match> triggers;

    int recurse();
    int addTarEntry(const char *fpath, const struct stat *sb);
    void findHardLinks();
    void findTarCollectionDirs();
    void recurseAddDir(Path *path, TarEntry *direntry);
    void addDirsToDirectories();
    void addEntriesToTarCollectionDirs();
    void pruneDirectories();
    void fixHardLinks();
    void fixTarPaths();
    size_t groupFilesIntoTars();
    void sortTarCollectionEntries();
    TarEntry *findNearestStorageDirectory(Path *a, Path *b);

    TarFile *findTarFromPath(Path *path);
    FileSystem *asFileSystem();

    // Implement a FUSE API
    int getattrCB(const char *path, struct stat *stbuf);
    int readdirCB(const char *path, void *buf, fuse_fill_dir_t filler,
                  off_t offset, struct fuse_file_info *fi);
    int readCB(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi);
    int readlinkCB(const char *path_char_string, char *buf, size_t s);

    void setMessage(std::string m) { message_ = m; }
    void setTarHeaderStyle(TarHeaderStyle ths) { tarheaderstyle_= ths; }
    Backup(ptr<FileSystem> fs);

    virtual ~Backup() = default;

private:
    size_t findNumTarsFromSize(size_t amount, size_t total_size);
    void calculateNumTars(TarEntry *te, size_t *nst, size_t *nmt, size_t *nlt,
                          size_t *sfs, size_t *mfs, size_t *lfs,
                          size_t *sc, size_t *mc);
    std::string message_;
    TarHeaderStyle tarheaderstyle_;

    FileSystem* file_system_;

};

std::unique_ptr<Backup> newBackup(ptr<FileSystem> fs);

#endif
