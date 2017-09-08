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

#include"log.h"
#include"util.h"

#include<cassert>
#include<cctype>
#include<cerrno>
#include<codecvt>
#include<cstdint>
#include<cstdio>
#include<cstdlib>
#include<ctime>
#include<iterator>
#include<locale>
#include<map>
#include<stddef.h>
#include<string.h>
#include<sys/stat.h>
#include<sys/time.h>
#include<utility>

using namespace std;

char separator = 0;
string separator_string = string("\0",1);
struct timespec start_time_;

#define KB 1024ull

string humanReadable(size_t s)
{
    if (s < KB)
    {
        return to_string(s);
    }
    if (s < KB * KB)
    {
        s /= KB;
        return to_string(s) + "K";
    }
    if (s < KB * KB * KB)
    {
        s /= KB * KB;
        return to_string(s) + "M";
    }
    if (s < KB * KB * KB * KB)
    {
        s /= KB * KB * KB;
        return to_string(s) + "G";
    }
    if (s < KB * KB * KB * KB * KB)
    {
        s /= KB * KB * KB * KB;
        return to_string(s) + "T";
    }
    s /= KB * KB * KB * KB * KB;
    return to_string(s) + "P";
}

size_t roundoffHumanReadable(size_t s)
{
    if (s < KB)
    {
        return s;
    }
    if (s < KB * KB)
    {
        s /= KB;
        s *= KB;
        return s;
    }
    if (s < KB * KB * KB)
    {
        s /= KB * KB;
        s *= KB * KB;
        return s;
    }
    if (s < KB * KB * KB * KB)
    {
        s /= KB * KB * KB;
        s *= KB * KB * KB;
        return s;
    }
    if (s < KB * KB * KB * KB * KB)
    {
        s /= KB * KB * KB * KB;
        s *= KB * KB * KB * KB;
        return s;
    }
    s /= KB * KB * KB * KB * KB;
    s *= KB * KB * KB * KB * KB;
    return s;
}

int parseHumanReadable(string s, size_t *out)
{
    size_t mul = 1;
    char c = s.back();
    
    if (s.length() > 256)
    {
        return ERR;
    }
    if (c == 'K')
    {
        mul = KB;
        s = s.substr(0, s.length() - 1);
    }
    else if (c == 'M')
    {
        mul = KB * KB;
        s = s.substr(0, s.length() - 1);
    }
    else if (c == 'G')
    {
        mul = KB * KB * KB;
        s = s.substr(0, s.length() - 1);
    }
    else if (c == 'T')
    {
        mul = KB * KB * KB * KB;
        s = s.substr(0, s.length() - 1);
    }
    
    for (auto c : s)
    {
        if (!isdigit(c))
        {
            return ERR;
        }
    }
    
    *out = mul * atol(s.c_str());
    return OK;
}

size_t basepos(string &s)
{
    return s.find_last_of('/');
}

string basename_(string &s)
{
    if (s.length() == 0 || s == "")
    {
        return "";
    }
    size_t e = s.length() - 1;
    if (s[e] == '/')
    {
        e--;
    }
    size_t p = s.find_last_of('/', e);
    return s.substr(p + 1, e - p + 1);
}

/**
 * dirname_("/a") return "" ie the root
 * dirname_("/a/") return "" ie the root
 * dirname_("/a/b") return "/a"
 * dirname_("/a/b/") return "/a"
 * dirname_("a/b") returns "a"
 * dirname_("a/b/") returns "a"
 * dirname_("") returns NULL
 * dirname_("/") returns NULL
 * dirname_("a") returns NULL
 * dirname_("a/") returns NULL
 */
static pair<string, bool> dirname_(string &s)
{
    // Drop trailing slashes!
    if (s.length() > 0 && s.back() == '/')
    {
        s = s.substr(0, s.length() - 1);
    }
    if (s.length() == 0)
    {
        return pair<string, bool>("", false);
    }
    size_t p = s.find_last_of('/');
    if (p == string::npos)
        return pair<string, bool>("", false);
    if (p == 0)
        return pair<string, bool>("", true);
    return pair<string, bool>(s.substr(0, p), true);
}

#define NO_ANSWER 0
#define YES_LESS_THAN 1
#define YES_GREATER_THAN 2

static int compareSameLengthPaths(Path *a, Path *b)
{
    if (a == b)
    {
        return NO_ANSWER;
    }
    assert(a->depth() == b->depth());
    int rc = compareSameLengthPaths(a->parent(), b->parent());
    
    if (rc == NO_ANSWER)
    {
        if (a->name() == b->name())
        {
            return NO_ANSWER;
        }
        if (Atom::lessthan(a->name(), b->name()))
        {
            return YES_LESS_THAN;
        }
        return YES_GREATER_THAN;
    }
    return rc;
}

bool depthFirstSortPath::lessthan(Path *a, Path *b)
{
    if (a == b)
    {
        return false;
    }
    if (a->depth() > b->depth())
    {
        return true;
    }
    if (a->depth() < b->depth())
    {
        return false;
    }
    
    bool rc = compareSameLengthPaths(a, b) == YES_LESS_THAN;
    return rc;
}

/**
 Special path comparison operator that sorts file names and directories in this order:
 This is the default order for tar files, the directory comes first,
 then subdirs, then content, then hard links.
 TEXTS/filter
 TEXTS/filter/alfa
 TEXTS/filter.zip
 */
bool TarSort::lessthan(Path *a, Path *b)
{
    if (a == b) {
        // Same path!
        return false;
    }
    int d = min(a->depth(), b->depth());
    Path *ap = a->parentAtDepth(d);
    Path *bp = b->parentAtDepth(d);
    if (ap == bp) {
        // Identical stem, one is simply deeper.
        if (a->depth() < b->depth()) {
            return true;
        }
        return false;
    }
    // Stem is not identical, compare the contents.
    return compareSameLengthPaths(ap, bp) == YES_LESS_THAN;
}

unsigned djb_hash(const char *key, int len)
{
    const unsigned char *p = reinterpret_cast<const unsigned char*>(key);
    unsigned h = 0;
    int i;
    
    for (i = 0; i < len; i++)
    {
        h = 33 * h + p[i];
    }
    
    return h;
}

uint32_t jenkins_one_at_a_time_hash(char *key, size_t len)
{
    uint32_t hash, i;
    for (hash = i = 0; i < len; ++i)
    {
        hash += key[i];
        hash += (hash << 10);
        hash ^= (hash >> 6);
    }
    hash += (hash << 3);
    hash ^= (hash >> 11);
    hash += (hash << 15);
    return hash;
}

uint32_t hashString(string a)
{
    return djb_hash(a.c_str(), a.length());
}


void eraseArg(int i, int *argc, char **argv)
{
    for (int j = i + 1;; ++j)
    {
        argv[j - 1] = argv[j];
        if (argv[j] == 0)
            break;
    }
    (*argc)--;
}

string eatTo(vector<char> &v, vector<char>::iterator &i, int c, size_t max, bool *eof, bool *err)
{
    string s;
    
    *eof = false;
    *err = false;
    while (max > 0 && i != v.end() && (c == -1 || *i != c))
    {
        s += *i;
        i++;
        max--;
    }
    if (c != -1 && *i != c)
    {
        *err = true;
        if (i == v.end()) {
            *eof = true;
        }
        return "";
    }
    if (i != v.end())
    {
        i++;
    }
    if (i == v.end()) {
        *eof = true;
    }
    return s;
}

string toHexAndText(const char *b, size_t len)
{
    string s;
    char buf[5];
    
    for (size_t j = 0; j < len; j++)
    {
        if (b[j] >= ' ' && b[j] <= 'z')
        {
            s.append(&b[j], 1);
        }
        else
        {
            memset(buf, 0, 5);
            snprintf(buf, 5, "\\x%02X", ((unsigned int) b[j]) & 255);
            s.append(buf);
        }
        if (j > 0 && j % 32 == 0)
            s.append("\n");
    }
    
    return s;
}

string toHexAndText(vector<char> &b)
{
    return toHexAndText(&b[0], b.size());
}

string toHex(const char *b, size_t len)
{
    string s;
    char buf[3];
    
    for (size_t j = 0; j < len; j++)
    {
        memset(buf, 0, 3);
        snprintf(buf, 3, "%02x", ((unsigned int) b[j]) & 255);
        s.append(buf);
    }
    return s;
}

string toHex(vector<char> &b)
{
    return toHex(&b[0], b.size());
}

int char2int(char input)
{
    if(input >= '0' && input <= '9')
        return input - '0';
    if(input >= 'A' && input <= 'F')
        return input - 'A' + 10;
    if(input >= 'a' && input <= 'f')
        return input - 'a' + 10;
    return -1;
}

void hex2bin(string s, vector<char> *target)
{
    char *src = &s[0];
    if (!src) return;
    while(*src && src[1]) {
        if (*src == ' ') {
            src++;
        } else {
            target->push_back(char2int(*src)*16 + char2int(src[1]));
            src += 2;
        }
    }
}

std::locale const user_locale("");

std::locale const *getLocale()
{
    return &user_locale;
}

std::wstring to_wstring(std::string const& s)
{
    std::wstring_convert<std::codecvt_utf8<wchar_t> > conv;
    return conv.from_bytes(s);
}

std::string wto_string(std::wstring const& s)
{
    std::wstring_convert<std::codecvt_utf8<wchar_t> > conv;
    return conv.to_bytes(s);
}

std::string tolowercase(std::string const& s)
{
    auto ss = to_wstring(s);
    for (auto& c : ss)
    {
        c = std::tolower(c, user_locale);
    }
    return wto_string(ss);
}

bool isInTheFuture(struct timespec *tm)
{
    // What happens with summer and winter time changes?    
    return tm->tv_sec > start_time_.tv_sec ||
        (tm->tv_sec == start_time_.tv_sec && tm->tv_nsec > start_time_.tv_nsec);
}

string timeAgo(struct timespec *tm)
{
    if (start_time_.tv_sec == tm->tv_sec &&
        start_time_.tv_nsec == tm->tv_nsec) {
        return "Now";
    }
    string msg = "ago";
    time_t diff = start_time_.tv_sec - tm->tv_sec;
    
    if (start_time_.tv_sec < tm->tv_sec ||
        (start_time_.tv_sec == tm->tv_sec &&
         start_time_.tv_nsec < tm->tv_nsec)) {
        // Time is in the future
        msg = "in the future";
        diff = tm->tv_sec - start_time_.tv_sec;
    } 
    
    if (diff == 0) {
        return "a second "+msg;
    }
    if (diff < 60) {
        return "a minute "+msg;
    }
    if (diff < 60*60) {
        int minutes = diff/60;
        return to_string(minutes)+" minutes "+msg;
    }
    if (diff < 60*60*24) {
        int hours = diff/(60*60);
        return to_string(hours)+" hours "+msg;
    }
    if (diff < 60*60*24*7) {
        int days = diff/(60*60*24);
        return to_string(days)+" days "+msg;
    }
    if (diff < 60*60*24*7*4) {
        int weeks = diff/(60*60*24*7);
        return to_string(weeks)+" weeks "+msg;
    }
    int months = diff/(60*60*24*7*4);
    return to_string(months)+" months "+msg;
}

static map<string, Atom*> interned_atoms;

Atom *Atom::lookup(string n)
{
    auto l = interned_atoms.find(n);
    if (l != interned_atoms.end())
    {
        return l->second;
    }
    Atom *na = new Atom(n);
    interned_atoms[n] = na;
    return na;
}

bool Atom::lessthan(Atom *a, Atom *b)
{
    if (a == b)
    {
        return 0;
    }
    // We are not interested in any particular locale dependent sort order here,
    // byte-wise is good enough for the map keys.
    int rc = strcmp(a->literal_.c_str(), b->literal_.c_str());
    return rc < 0;
}

static map<string, Path*> interned_paths;

Path *Path::lookup(string p)
{
    assert(p.back() != '\n');
    if (p.back() == '/')
    {
        p = p.substr(0, p.length() - 1);
    }
    auto pl = interned_paths.find(p);
    if (pl != interned_paths.end())
    {
        return pl->second;
    }
    auto s = dirname_(p);
    if (s.second)
    {
        Path *parent = lookup(s.first);
        Path *np = new Path(parent, Atom::lookup(basename_(p)), p);
        interned_paths[p] = np;
        return np;
    }
    Path *np = new Path(NULL, Atom::lookup(basename_(p)), p);
    interned_paths[p] = np;
    return np;
}

Path *Path::lookupRoot()
{
    return lookup("");
}

deque<Path*> Path::nodes()
{
    deque<Path*> v;
    Path *p = this;
    while (p)
    {
        v.push_front(p);
        p = p->parent();
    }
    return v;
}

Path *Path::appendName(Atom *n) {
    string s = str()+"/"+n->str();
    return lookup(s);    
}

Path *Path::parentAtDepth(int i)
{
    int d = depth_;
    Path *p = this;
    assert(d >= i);
    while (d > i && p) {
        p = p->parent_;
        d--;
    }
    return p;
}

/*
string &Path::str()
{
    if (path_cache_)
    {
        return string(path_cache_);
    }
    
    string rs;
    int i = 0;
    auto v = nodes();
    for (auto p : v)
    {
        if (i > 0)
            rs += "/";
        rs += p->name()->literal();
        i++;
    }
    path_cache_ = new char[rs.length() + 1];
    memcpy(path_cache_, rs.c_str(), rs.length() + 1);
    path_cache_len_ = rs.length();
    
    return rs;
    }*/

Path *Path::reparent(Path *parent)
{
    string s = parent->str()+"/"+atom_->str();
    return new Path(parent, atom_, s);
}

Path* Path::subpath(int from, int len)
{
    if (len == 0)
    {
        return NULL;
    }
    string rs;
    auto v = nodes();
    int i = 0, to = v.size();
    if (len != -1)
    {
        to = from + len;
    }
    for (auto p : v)
    {
        if (i >= from && i < to)
        {
            if (i > from)
                rs += "/";
            rs += p->name()->str();
        }
        i++;
    }
    return lookup(rs);
}

Path* Path::prepend(Path *p)
{
    string concat;
    if (p->str().front() == '/' && str().front() == '/') {
        concat = p->str() + str();
    } else {
        concat = p->str() + "/" + str();
    }
    Path *pa = lookup(concat);
    return pa;
}

Path* Path::commonPrefix(Path *a, Path *b)
{
    auto av = a->nodes();
    auto bv = b->nodes();
    auto ai = av.begin();
    auto bi = bv.begin();
    int i = 0;
    
    while (ai != av.end() && bi != bv.end() && (*ai)->name() == (*bi)->name())
    {
        i++;
        ai++;
        bi++;
    }
    return a->subpath(0, i);
}

Path::Initializer::Initializer()
{
    Atom *root = Atom::lookup("");
    string s = string("");
    interned_paths[""] = new Path(NULL, root, s);
}

Path::Initializer Path::initializer_s;

/*
bool Path::splitInto(size_t name_len, Path **name, size_t prefix_len, Path **prefx)
{
    char *s = c_str();
    char *e = c_str()+c_str_len();

    while (e > s && *e != '/') {
	e--;
    }
    size_t nlen = c_str_len() - (e-s);
    // Ouch, filename did not fit in the name field...
    if (nlen > name_len) return false;
    
    while (e > s) {
    }
}
*/
void toLittleEndian(uint16_t *t)
{}

void toLittleEndian(uint32_t *t)
{}

void fixEndian(long *t)
{
}

ssize_t readlink(const char *path, char *dest, size_t len)
{
    return -1;
}
