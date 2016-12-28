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

#ifndef FORWARD_H
#define FORWARD_H

#include<assert.h>

#include"defs.h"
#include"tarfile.h"
#include"tarentry.h"
#include"libtar.h"
#include"util.h"

#include<fuse.h>
#include<regex.h>

#include<map>
#include<string>
#include<vector>

using namespace std;

enum FilterType { INCLUDE, EXCLUDE };

struct Filter {
    string rule;
    FilterType type;

    Filter(string rule_, FilterType type_) : rule(rule_), type(type_) { }
};
    
typedef int (*FileCB)(const char *,const struct stat *,int,struct FTW *);

typedef int (*GetAttrCB)(const char*, struct stat*);
typedef int (*ReaddirCB)(const char*,void*,fuse_fill_dir_t,off_t,struct fuse_file_info*);
typedef int (*ReadCB)(const char *,char *,size_t,off_t,struct fuse_file_info *);

struct TarredFS {
    pthread_mutex_t global;
    
    string root_dir;
    string mount_dir;

    size_t target_target_tar_size = DEFAULT_TARGET_TAR_SIZE;
    size_t tar_trigger_size = DEFAULT_TAR_TRIGGER_SIZE;
    size_t target_split_tar_size = DEFAULT_SPLIT_TAR_SIZE;
    // The default setting is to trigger tars in each subdirectory below the root.
    // Even if the subdir does not qualify with enough data to create a min tar file.
    // However setting this to 0 and setting trigger size to 0, puts all content in
    // tars directly below the mount dir, ie no subdirs, only tars.
    int forced_tar_collection_dir_depth = 1;
    
    map<string,TarEntry*,depthFirstSort> files;
    map<TarEntry*,pair<size_t,size_t>> tar_storage_dirs;
    map<string,TarEntry*> directories;
    
    vector<pair<Filter,regex_t>> filters;

    int recurse(FileCB cb);
    int addTarEntry(const char *fpath, const struct stat *sb, struct FTW *ftwbuf);
    void findTarCollectionDirs();
    void recurseAddDir(string path, TarEntry *direntry);
    void addDirsToDirectories();
    void addEntriesToTarCollectionDirs();
    void pruneDirectories();

    
    size_t groupFilesIntoTars();
    void sortTarCollectionEntries();
    TarFile *findTarFromPath(string path);   
    int getattrCB(const char *path, struct stat *stbuf);
    int readdirCB(const char *path, void *buf, fuse_fill_dir_t filler,
                  off_t offset, struct fuse_file_info *fi);
    int readCB(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi);

private:    
    size_t findNumTarsFromSize(size_t amount, size_t total_size);
    void calculateNumTars(TarEntry *te, size_t *nst, size_t *nmt, size_t *nlt,
                          size_t *sfs, size_t *mfs, size_t *lfs,
                          size_t *sc, size_t *mc);
};

#endif
