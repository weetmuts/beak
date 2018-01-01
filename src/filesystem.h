/*
 Copyright (C) 2017 Fredrik Öhrström

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

#ifndef FILESYSTEM_H
#define FILESYSTEM_H

#include "always.h"

#include <deque>
#include <functional>
#include <memory>
#include <memory.h>
#include <string>
#include <vector>
#include <sys/stat.h>
#include <sys/types.h>

#ifdef FUSE_USE_VERSION
#include <fuse/fuse.h>
#else
#include "nofuse.h"
#endif

#define MAX_FILE_NAME_LENGTH 255
#define MAX_PATH_LENGTH 4096
#define MAXPATH 4096
#define ARG_MAX 4096

#ifdef PLATFORM_WINAPI
#include "filesystem_winapi.h"
#endif

struct FileStat;
struct Atom;
struct Path;
struct FuseAPI;
struct FileSystem;

int loadVector(Path *file, size_t blocksize, std::vector<char> *buf);
int writeVector(std::vector<char> *buf, Path *file);

struct FileStat {
    ino_t st_ino;
    mode_t st_mode;
    nlink_t st_nlink;
    uid_t st_uid;
    gid_t st_gid;
    dev_t st_rdev;
    off_t st_size;
    struct timespec st_atim;
    struct timespec st_mtim;
    struct timespec st_ctim;

    FileStat() { memset(this, 0,sizeof(FileStat)); };
    FileStat(const struct stat *sb) {
        loadFrom(sb);
    }
    void loadFrom(const struct stat *sb);
    void storeIn(struct stat *sb);
    bool isRegularFile();
    bool isDirectory();
    bool isSymbolicLink();
    bool isCharacterDevice();
    bool isBlockDevice();
    bool isFIFO();
    bool isSOCK();
    bool isISUID();
    bool isISGID();
    bool isISVTX();

    bool isIRUSR();
    bool isIWUSR();
    bool isIXUSR();

    bool isIRGRP();
    bool isIWGRP();
    bool isIXGRP();

    bool isIROTH();
    bool isIWOTH();
    bool isIXOTH();

    std::string uidName();
    std::string gidName();
};

extern char separator;
extern std::string separator_string;

struct Atom
{
    static Atom *lookup(std::string literal);
    static bool lessthan(Atom *a, Atom *b);

    std::string &str() { return literal_; }
    const char *c_str() { return literal_.c_str(); }
    size_t c_str_len() { return literal_.length(); }

    private:

    Atom(std::string n) : literal_(n) { }
    std::string literal_;
};

struct Path
{
    struct Initializer { Initializer(); };
    static Initializer initializer_s;

    static Path *lookup(std::string p);
    static Path *lookupRoot();
    static Path *store(std::string p);
    static Path *commonPrefix(Path *a, Path *b);

    Path *parent() { return parent_; }
    Atom *name() { return atom_; }
    Path *appendName(Atom *n);
    Path *parentAtDepth(int i);
    std::string &str() { return path_cache_; }
    const char *c_str() { return &path_cache_[0]; }
    size_t c_str_len() { return path_cache_.length(); }

    // The root aka "/" aka "" has depth 1
    // "/Hello" has depth 2
    // "Hello" has depth 1
    // "Hello/There" has depth 2
    // "/Hello/There" has depth 3
    int depth() { return depth_; }
    Path *subpath(int from, int len = -1);
    Path *prepend(Path *p);
    Path *append(std::string &p);
    bool isRoot() { return depth_ == 1 && atom_->c_str_len() == 0; }
    Path *unRoot() {
	if (isRoot()) return NULL;
	if (c_str()[0] != '/') {
	    return this;
	}
	return subpath(1);
    }
    bool isBelowOrEqual(Path *p) {
        if (depth_ < p->depth_) return false;
        Path *t = this;
        while (t != NULL && t != p) { t = t->parent_; }
        return (t == p);
    }

    private:

    Path(Path *p, Atom *n, std::string &path) :
    parent_(p), atom_(n), depth_((p) ? p->depth_ + 1 : 1),path_cache_(path) { }
    Path *parent_;
    Atom *atom_;
    int depth_;
    std::string path_cache_;

    std::deque<Path*> nodes();
    Path *reparent(Path *p);
};

struct depthFirstSortPath
{
    // Special path comparison operator that sorts file names and directories in this order:
    // This is the order necessary to find tar collection dirs depth first.
    // TEXTS/filter/alfa
    // TEXTS/filter
    // TEXTS/filter.zip
    static bool lessthan(Path *f, Path *t);
    inline bool operator()(Path *a, Path *b) const
    {
        return lessthan(a, b);
    }
};

struct TarSort
{
    // Special path comparison operator that sorts file names and directories in this order:
    // This is the default order for tar files, the directory comes first,
    // then subdirs, then content, then hard links.
    // TEXTS/filter
    // TEXTS/filter/alfa
    // TEXTS/filter.zip
    static bool lessthan(Path *a, Path *b);
    inline bool operator()(Path *a, Path *b) const
    {
        return lessthan(a, b);
    }
};

struct FuseAPI {
    virtual int getattrCB(const char *path,
                          struct stat *stbuf) = 0;
    virtual int readdirCB(const char *path,
                          void *buf,
                          fuse_fill_dir_t filler,
                          off_t offset,
                          struct fuse_file_info *fi) = 0;
    virtual int readCB(const char *path,
                       char *buf,
                       size_t size,
                       off_t offset,
                       struct fuse_file_info *fi) = 0;
    virtual int readlinkCB(const char *path_char_string,
                           char *buf,
                           size_t s) = 0;
};

struct FileSystem
{
    virtual bool readdir(Path *p, std::vector<Path*> *vec) = 0;
    virtual ssize_t pread(Path *p, char *buf, size_t size, off_t offset) = 0;
    virtual void recurse(std::function<void(Path *p)> cb) = 0;
    virtual bool stat(Path *p, FileStat *fs) = 0;
    virtual Path *mkTempDir(std::string prefix) = 0;
};

std::unique_ptr<FileSystem> newDefaultFileSystem();
std::unique_ptr<FileSystem> newFileSystem(FuseAPI *api);
FileSystem *defaultFileSystem();

dev_t MakeDev(int maj, int min);
int MajorDev(dev_t d);
int MinorDev(dev_t d);
std::string ownergroupString(uid_t uid, gid_t gid);
std::string permissionString(FileStat *fs);
mode_t stringToPermission(std::string s);

#ifdef PLATFORM_WINAPI
uid_t geteuid();
gid_t getegid();

char *realpath(const char *path, char *resolved_path);
char *mkdtemp(char *pattern);
int fork();

#endif

#endif
