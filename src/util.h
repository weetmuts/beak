/*
 Copyright (C) 2016-2017 Fredrik Öhrström

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

#ifndef UTIL_H
#define UTIL_H

#include"config.h"
#include"defs.h"

#include<cstdint>
#include<deque>
#include<memory.h>
#include<stddef.h>
#include<string>
#include<sys/types.h>
#include<sys/stat.h>
#include<vector>

using namespace std;

#define XSTR(s) STR(s)
#define STR(s) #s

string humanReadable(size_t s);
size_t roundoffHumanReadable(size_t s);
int parseHumanReadable(string s, size_t *out);
size_t basepos(string& s);
wstring to_wstring(std::string const& s);
string wto_string(std::wstring const& s);
string tolowercase(std::string const& s);
std::locale const *getLocale();
uint32_t hashString(string a);
void eraseArg(int i, int *argc, char **argv);
// Eat characters from the vector v, iterating using i, until the end char c is found.
// If end char == -1, then do not expect any end char, get all until eof.
// If the end char is not found, return error.
// If the maximum length is reached without finding the end char, return error.
string eatTo(vector<char> &v, vector<char>::iterator &i, int c, size_t max, bool *eof, bool *err);
// Eat whitespace (space and tab, not end of lines).
void eatWhitespace(vector<char> &v, vector<char>::iterator &i, bool *eof);
// Remove leading and trailing white space
void trimWhitespace(string *s);
// Translate binary buffer with printable strings to ascii
// with non-printabled escaped as such: \xC0 \xFF \xEE
string toHexAndText(const char *b, size_t len);
string toHexAndText(vector<char> &b);
string toHex(const char *b, size_t len);
string toHex(vector<char> &b);
void hex2bin(string s, vector<char> *target);
void fixEndian(long *t);
bool isInTheFuture(struct timespec *tm);
string timeAgo(struct timespec *tm);

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
        memset(this, 0,sizeof(FileStat));
        st_ino = sb->st_ino;
        st_mode = sb->st_mode;
        st_nlink = sb->st_nlink;
        st_uid = sb->st_uid;
        st_gid = sb->st_gid;
        st_rdev = sb->st_rdev;
        st_size = sb->st_size;
#if HAS_ST_MTIM==yes
        st_atim = sb->st_atim;
        st_mtim = sb->st_mtim;
        st_ctim = sb->st_ctim;
#else
        st_atim.tv_sec = sb->st_atime;
        st_mtim.tv_sec = sb->st_mtime;
        st_ctim.tv_sec = sb->st_ctime;
        st_atim.tv_nsec = 0;
        st_mtim.tv_nsec = 0;
        st_ctim.tv_nsec = 0;
#endif        
    }
    void storeIn(struct stat *sb) {
        memset(sb, 0, sizeof(struct stat));
        sb->st_ino = st_ino;
        sb->st_mode = st_mode;
        sb->st_nlink = st_nlink;
        sb->st_uid = st_uid;
        sb->st_gid = st_gid;
        sb->st_rdev = st_rdev;
        sb->st_size = st_size;
        sb->st_atim = st_atim;
        sb->st_mtim = st_mtim;
        sb->st_ctim = st_ctim;        
    }
    bool isRegularFile()
    {
	return S_ISREG(st_mode);
    }
    bool isDirectory()
    {
	return S_ISDIR(st_mode);
    }
    bool isSymbolicLink()
    {
	return S_ISLNK(st_mode);
    }
    bool isCharacterDevice()
    {
	return S_ISCHR(st_mode);
    } 
    bool isBlockDevice()
    {
	return S_ISBLK(st_mode);
    }
    bool isFIFO()
    {
	return S_ISFIFO(st_mode);
    }
    bool isISUID() { return st_mode & S_ISUID; } // set uid
    bool isISGID() { return st_mode & S_ISGID; } // set gid
    bool isISVTX() { return st_mode & S_ISVTX; } // sticky

    bool isIRUSR() { return st_mode & S_IRUSR; }
    bool isIWUSR() { return st_mode & S_IWUSR; }
    bool isIXUSR() { return st_mode & S_IXUSR; }

    bool isIRGRP() { return st_mode & S_IRGRP; }
    bool isIWGRP() { return st_mode & S_IWGRP; }
    bool isIXGRP() { return st_mode & S_IXGRP; }

    bool isIROTH() { return st_mode & S_IROTH; }
    bool isIWOTH() { return st_mode & S_IWOTH; }
    bool isIXOTH() { return st_mode & S_IXOTH; }    
};
extern char separator;
extern string separator_string;

////////////////////////////////////////////////////////
//
// Platform specific code
//
string ownergroupString(uid_t uid, gid_t gid);
string permissionString(mode_t m);
mode_t stringToPermission(string s);
uint64_t clockGetTime();
void captureStartTime();
int gzipit(string *from, vector<unsigned char> *to);
int gunzipit(vector<char> *from, vector<char> *to);
dev_t MakeDev(int maj, int min);
int MajorDev(dev_t d);
int MinorDev(dev_t d);
//
//
///////////////////////////////////////////////////////

#ifdef WINAPI
#define S_ISLNK(x) false
ssize_t readlink(const char *path, char *dest, size_t len);
#endif

struct Atom
{
    static Atom *lookup(string literal);
    static bool lessthan(Atom *a, Atom *b);
    
    string &str() { return literal_; }
    const char *c_str() { return literal_.c_str(); }
    size_t c_str_len() { return literal_.length(); }

    private:

    Atom(string n) : literal_(n) { }
    string literal_;
};

struct Path
{
    struct Initializer { Initializer(); };
    static Initializer initializer_s;
    
    static Path *lookup(string p);
    static Path *lookupRoot();
    static Path *store(string p);
    static Path *commonPrefix(Path *a, Path *b);
    
    Path *parent() { return parent_; }
    Atom *name() { return atom_; }
    Path *appendName(Atom *n);
    Path *parentAtDepth(int i);
    string &str() { return path_cache_; }
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

    Path(Path *p, Atom *n, string &path) :
    parent_(p), atom_(n), depth_((p) ? p->depth_ + 1 : 1),path_cache_(path) { }
    Path *parent_;
    Atom *atom_;
    int depth_;
    string path_cache_;
    
    deque<Path*> nodes();
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

#endif
