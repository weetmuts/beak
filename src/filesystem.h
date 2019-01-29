/*
 Copyright (C) 2017-2018 Fredrik Öhrström

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
#include <map>
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

enum UpdateDisk {
    NoUpdate,
    UpdatePermissions,
    Store
};

struct FileStat {
    ino_t st_ino {};
    mode_t st_mode {};
    nlink_t st_nlink {};
    Path *hard_link {};
    uid_t st_uid {};
    gid_t st_gid {};
    dev_t st_rdev {};
    off_t st_size {};
    struct timespec st_atim {};
    struct timespec st_mtim {};
    struct timespec st_ctim {};

    UpdateDisk disk_update {};

    FileStat() { };
    FileStat(const struct stat *sb) {
        loadFrom(sb);
    }
    bool equal(FileStat *b) {
        return samePermissions(b) && sameSize(b) && sameMTime(b);
    }
    bool samePermissions(FileStat *b) { return (st_mode&07777) == (b->st_mode&07777); }
    bool sameSize(FileStat *b) { return st_size == b->st_size; }
    bool sameMTime(FileStat *b) { return st_mtim.tv_sec == b->st_mtim.tv_sec &&
            st_mtim.tv_nsec == b->st_mtim.tv_nsec; }
    void checkStat(FileSystem *dst, Path *target);

    mode_t permissions() { return st_mode & 07777; }
    void loadFrom(const struct stat *sb);
    void storeIn(struct stat *sb);
    void setAsRegularFile();
    void setAsDirectory();
    void setAsExecutable();
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

    void setIWUSR();

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

    const char *ext_c_str_() { return ext_; }

    private:

    Atom(std::string n) : literal_(n)
    {
        size_t p0 = n.rfind('.');
        if (p0 == std::string::npos) { ext_ = ""; } else { ext_ = n.c_str()+p0+1; }
    }
    std::string literal_;
    const char *ext_;
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
    // Return the c_str without the leading slash, if it exists.
    const char *c_str_nls() {
        if (c_str()[0] == '/') { return c_str()+1; }
        else { return c_str(); }
    }

    // The root aka "/" aka "" has depth 1
    // "/Hello" has depth 2
    // "Hello" has depth 1
    // "Hello/There" has depth 2
    // "/Hello/There" has depth 3
    int depth() { return depth_; }
    Path *subpath(int from, int len = -1);
    Path *prepend(Path *p);
    Path *append(std::string p);
    int findPart(Path* part) {
        Path *p = this;
        while (p) {
            if (p->atom_ == part->atom_) { return p->depth_; }
            p = p->parent_;
        }
        return -1;
    }
    bool isRoot() { return depth_ == 1 && atom_->c_str_len() == 0; }
    #ifdef PLATFORM_WINAPI
    bool isDrive() {
        const char *s = c_str();
        return depth_ == 2 && str().length()==2 && s[1] == ':' &&
            ( (s[0]>='A' && s[0]<='Z') || (s[0]>='a' && s[0]<='z'));
    }
    #endif
    Path *unRoot()
    {
        if (isRoot()) return NULL;
        if (c_str()[0] != '/')
        {
            return this;
        }
        return subpath(1);
    }
    bool isBelowOrEqual(Path *p)
    {
        if (depth_ < p->depth_) return false;
        Path *t = this;
        while (t != NULL && t != p) { t = t->parent_; }
        return (t == p);
    }
    Path *realpath();

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

struct FuseMount
{
};

enum SortOrder {
    CTimeDesc
};

enum RecurseOption {
    RecurseContinue,
    RecurseSkipSubTree,
    RecurseStop
};

struct FileSystem
{
    virtual bool readdir(Path *p, std::vector<Path*> *vec) = 0;
    virtual ssize_t pread(Path *p, char *buf, size_t size, off_t offset) = 0;
    virtual RC recurse(Path *p, std::function<RecurseOption(Path *path, FileStat *stat)> cb) = 0;
    virtual RC recurse(Path *p, std::function<RecurseOption(const char *path, const struct stat *sb)> cb) = 0;
    // List all files below p, sort on CTimeDesc
    virtual RC listFilesBelow(Path *p, std::vector<Path*> *files, SortOrder so) = 0;
    // Touch the meta data of the file to trigger an update of the ctime to NOW.
    virtual RC ctimeTouch(Path *file) = 0;
    virtual RC stat(Path *p, FileStat *fs) = 0;
    virtual RC chmod(Path *p, FileStat *stat) = 0;
    virtual RC utime(Path *p, FileStat *stat) = 0;
    virtual Path *mkTempFile(std::string prefix, std::string content) = 0;
    virtual Path *mkTempDir(std::string prefix) = 0;
            bool mkDirpWriteable(Path *p);
    virtual Path *mkDir(Path *p, std::string name) = 0;
    virtual RC rmDir(Path *p) = 0;

    virtual RC loadVector(Path *file, size_t blocksize, std::vector<char> *buf) = 0;

    virtual RC createFile(Path *file, std::vector<char> *buf) = 0;

    // file: The filename to be created or overwritten.
    // stat: The size and permissions of the to be created file.
    // cb: Callback to fetch the data to be written into the file.
    virtual bool createFile(Path *file,
                            FileStat *stat,
                            std::function<size_t(off_t offset, char *buffer, size_t len)> cb) = 0;

    virtual bool createSymbolicLink(Path *file, FileStat *stat, std::string target) = 0;
    virtual bool createHardLink(Path *file, FileStat *stat, Path *target) = 0;
    virtual bool createFIFO(Path *file, FileStat *stat) = 0;
    virtual bool readLink(Path *file, std::string *target) = 0;

    virtual bool deleteFile(Path *file) = 0;

    // Enable watching of filesystem changes. Used to warn the user
    // that the filesystem was changed during backup...
    virtual RC enableWatch() = 0;
    // Start watching a directory.
    virtual RC addWatch(Path *dir) = 0;
    // Return number of modifications made during watch. Hopefully zero.
    virtual int endWatch() = 0;

    virtual ~FileSystem() = default;

    FileSystem(const char *n) : name_(n) {}

    const char *name() { return name_; }

    private:

    const char *name_ {};
};

// The default file system for this computer/OS.
std::unique_ptr<FileSystem> newDefaultFileSystem();

// A file system where you can only stat the files...
std::unique_ptr<FileSystem> newStatOnlyFileSystem(std::map<Path*,FileStat> contents);

// Access a fuse exported file system as a FileSystem.
FileSystem *newFileSystem(FuseAPI *api);


Path *configurationFile();
Path *cacheDir();

dev_t MakeDev(int maj, int min);
int MajorDev(dev_t d);
int MinorDev(dev_t d);
std::string ownergroupString(uid_t uid, gid_t gid);
std::string permissionString(FileStat *fs);
mode_t stringToPermission(std::string s);

#ifdef PLATFORM_WINAPI
uid_t geteuid();
gid_t getegid();

char *mkdtemp(char *pattern);
pid_t fork();

#endif

#endif
