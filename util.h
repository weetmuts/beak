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

#ifndef UTIL_H
#define UTIL_H

#include <stddef.h>
#include <sys/types.h>
#include <cstdint>
#include <deque>
#include <string>
#include <vector>

using namespace std;

string humanReadable(size_t s);
size_t roundoffHumanReadable(size_t s);
int parseHumanReadable(string s, size_t *out);
uint64_t clockGetTime();
size_t basepos(string& s);
string dirdiff(string& from, size_t len, string& to);
wstring to_wstring(std::string const& s);
string wto_string(std::wstring const& s);
string tolowercase(std::string const& s);
std::locale const *getLocale();
void captureStartTime();
bool isInTheFuture(struct timespec *tm);
string timeAgo(struct timespec *tm);

int gzipit(string *from, vector<unsigned char> *to);
int gunzipit(vector<char> *from, vector<char> *to);
  
extern char separator;
extern string separator_string;

struct Atom
{
    static Atom *lookup(string literal);
    static bool lessthan(Atom *a, Atom *b);
    
    string &str() {
        return literal_;
    }
    const char *c_str()
    {
        return literal_.c_str();
    }
    size_t c_str_len()
    {
        return literal_.length();
    }

    private:

    Atom(string n) : literal_(n) { }
    string literal_;
};

struct Path
{
    struct Initializer
    {
        Initializer();
    };
    static Initializer initializer_s;
    
    static Path *lookup(string p);
    static Path *lookupRoot();
    static Path *store(string p);
    static Path *commonPrefix(Path *a, Path *b);
    
    Path *parent()
	{
            return parent_;
	}
    Atom *name()
	{
            return atom_;
	}
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
    int depth() {
        return depth_;
    }
    Path *subpath(int from, int len = -1);
    Path *prepend(Path *p);
    bool isRoot() {
        return depth_ == 1 && atom_->c_str_len() == 0;
    }
    Path *unRoot() {
	if (isRoot()) return NULL;
	if (c_str()[0] != '/') {
	    return this;
	}
	return subpath(1);
    }
    bool isBelowOrEqual(Path *p) {
        if (depth_ < p->depth_) {
            return false;
        }
        Path *t = this;
        while (t != NULL && t != p) {
            t = t->parent_;
        }
        return (t == p);
    }
    
private:

    Path(Path *p, Atom *n, string &path) :
    parent_(p), atom_(n), depth_((p) ? p->depth_ + 1 : 1),
        path_cache_(path) {
	}
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

string commonPrefix(string a, string b);

uint32_t hashString(string a);

string permissionString(mode_t m);
mode_t stringToPermission(string s);

string ownergroupString(uid_t uid, gid_t gid);

void eraseArg(int i, int *argc, char **argv);

// Eat characters from the vector v, iterating using i, until the end char c is found.
// If end char == -1, then do not expect any end char, get all until eof.
// If the end char is not found, return error.
// If the maximum length is reached without finding the end char, return error.
string eatTo(vector<char> &v, vector<char>::iterator &i, int c, size_t max, bool *eof, bool *err);

// Translate binary buffer with printable strings to ascii
// with non-printabled escaped as such: \xC0 \xFF \xEE
string toHexAndText(const char *b, size_t len);
string toHexAndText(vector<char> &b);

// Translate binary buffer to lower case hex string: aa01ffc0ffee
string toHex(const char *b, size_t len);
string toHex(vector<char> &b);

// Translate hex string to binary vector.
void hex2bin(string s, vector<char> *target);

void fixEndian(long *t);

#endif
