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
#include <fuse.h>
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

struct Backup
{
    RC scanFileSystem(Argument *origin, Settings *settings, ProgressStatistics *progress);

    pthread_mutex_t global;
    pthread_mutexattr_t global_attr;

    std::string root_dir;
    Path *root_dir_path;
    std::string mount_dir;
    Path *mount_dir_path;

    size_t tar_target_size = 10ull*1024*1024;
    size_t tar_trigger_size = 20ull*1024*1024;
    size_t tar_split_size = 50ull*1024*1024;

    // The default setting is to trigger tars in each subdirectory below the root.
    // Even if the subdir does not qualify with enough data to create a min tar file.
    // However setting this to 1 and setting trigger size to 0, puts all content in
    // tars directly below the mount dir, ie no subdirs, only tars.
    int forced_tar_collection_dir_depth = 2;

    std::map<Path*,TarEntry,depthFirstSortPath> files;
    // Store dynamic allcations of tar entries for the destructor.
    std::vector<std::unique_ptr<TarEntry>> dynamics;
    std::map<Path*,TarEntry*,depthFirstSortPath> tar_storage_directories;
    std::map<Path*,TarEntry*> directories;
    std::map<ino_t,TarEntry*> hard_links; // Only inodes for which st_nlink > 1
    size_t hardlinksavings = 0;

    std::vector<std::pair<Filter,Match>> filters;
    std::vector<Match> triggers;
    std::vector<Match> contentsplits;

    int recurse();
    RecurseOption addTarEntry(Path *abspath, FileStat *st);
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

    TarFile *findTarFromPath(Path *path, uint *partnr);
    FileSystem *asFileSystem();
    FileSystem *originFileSystem() { return origin_fs_; }
    FuseAPI *asFuseAPI();

    void setConfig(std::string c) { config_ = c; }
    void setTarHeaderStyle(TarHeaderStyle ths) { tarheaderstyle_= ths; }
    Backup(ptr<FileSystem> origin_fs);

    virtual ~Backup() = default;

private:
    size_t findNumTarsFromSize(size_t amount, size_t total_size);
    void calculateNumTars(TarEntry *te, size_t *nst, size_t *nmt, size_t *nlt,
                          size_t *sfs, size_t *mfs, size_t *lfs,
                          size_t *sc, size_t *mc);
    std::string config_;
    TarHeaderStyle tarheaderstyle_;

    FileSystem* origin_fs_;

    bool found_future_dated_file_ {};

    std::unique_ptr<FileSystem> as_file_system_;
    std::unique_ptr<FuseAPI> as_fuse_api_;
};

std::unique_ptr<Backup> newBackup(ptr<FileSystem> fs);

#endif
