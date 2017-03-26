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

#include "util.h"

#include <grp.h>
#include <pwd.h>
#include <stddef.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <cassert>
#include <cctype>
#include <cerrno>
#include <codecvt>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <iterator>
#include <locale>
#include <map>
#include <sstream>
#include <utility>

#include "log.h"

using namespace std;

static ComponentId UTIL = registerLogComponent("util");
static ComponentId TMP = registerLogComponent("tmp");

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
	fprintf(stderr, "Störst %zu\n", s);
	s /= KB * KB * KB * KB * KB;
	fprintf(stderr, "Minskat %zu\n", s);
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

// Return microseconds
uint64_t clockGetTime()
{
	timespec ts;
	clock_gettime(CLOCK_MONOTONIC, &ts);
	return (uint64_t) ts.tv_sec * 1000000LL + (uint64_t) ts.tv_nsec / 1000LL;
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
	debug(TMP,"\n\nd=%d \n%s\n%s\n", d, a->c_str(), b->c_str());
	Path *ap = a->parentAtDepth(d);
	Path *bp = b->parentAtDepth(d);
	debug(TMP,"\n%s\n%s\n", ap->c_str(), bp->c_str());
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

string permissionString(mode_t m)
{
	stringstream ss;

	if (S_ISDIR(m))
		ss << "d";
	else if (S_ISLNK(m))
		ss << "l";
	else if (S_ISCHR(m))
		ss << "c";
	else if (S_ISBLK(m))
		ss << "b";
	else if (S_ISFIFO(m))
		ss << "p";
	else if (S_ISSOCK(m))
		ss << "s";
	else
	{
		assert(S_ISREG(m));
		ss << "-";
	}
	if (m & S_IRUSR)
		ss << "r";
	else
		ss << "-";
	if (m & S_IWUSR)
		ss << "w";
	else
		ss << "-";
	if (m & S_IXUSR)
		ss << "x";
	else
		ss << "-";
	if (m & S_IRGRP)
		ss << "r";
	else
		ss << "-";
	if (m & S_IWGRP)
		ss << "w";
	else
		ss << "-";
	if (m & S_IXGRP)
		ss << "x";
	else
		ss << "-";
	if (m & S_IROTH)
		ss << "r";
	else
		ss << "-";
	if (m & S_IWOTH)
		ss << "w";
	else
		ss << "-";
	if (m & S_IXOTH)
		ss << "x";
	else
		ss << "-";
	return ss.str();
}

mode_t stringToPermission(string s)
{
	mode_t rc = 0;

	if (s[0] == 'd')
		rc |= S_IFDIR;
	else if (s[0] == 'l')
		rc |= S_IFLNK;
	else if (s[0] == 'c')
		rc |= S_IFCHR;
	else if (s[0] == 'b')
		rc |= S_IFBLK;
	else if (s[0] == 'p')
		rc |= S_IFIFO;
	else if (s[0] == 's')
		rc |= S_IFSOCK;
	else if (s[0] == '-')
		rc |= S_IFREG;
	else
		goto err;

	if (s[1] == 'r')
		rc |= S_IRUSR;
	else if (s[1] != '-')
		goto err;
	if (s[2] == 'w')
		rc |= S_IWUSR;
	else if (s[2] != '-')
		goto err;
	if (s[3] == 'x')
		rc |= S_IXUSR;
	else if (s[3] != '-')
		goto err;

	if (s[4] == 'r')
		rc |= S_IRGRP;
	else if (s[4] != '-')
		goto err;
	if (s[5] == 'w')
		rc |= S_IWGRP;
	else if (s[5] != '-')
		goto err;
	if (s[6] == 'x')
		rc |= S_IXGRP;
	else if (s[6] != '-')
		goto err;

	if (s[7] == 'r')
		rc |= S_IROTH;
	else if (s[7] != '-')
		goto err;
	if (s[8] == 'w')
		rc |= S_IWOTH;
	else if (s[8] != '-')
		goto err;
	if (s[9] == 'x')
		rc |= S_IXOTH;
	else if (s[9] != '-')
		goto err;

	return rc;

	err: return 0;
}

string ownergroupString(uid_t uid, gid_t gid)
{
	struct passwd pwd;
	struct passwd *result;
	char buf[16000];
	stringstream ss;

	int rc = getpwuid_r(uid, &pwd, buf, sizeof(buf), &result);
	if (result == NULL)
	{
		if (rc == 0)
			ss << uid;
		else
		{
			errno = rc;
			error(UTIL, "Internal error getpwuid_r %d", errno);
		}
	}
	else
	{
		ss << pwd.pw_name;
	}
	ss << "/";

	struct group grp;
	struct group *gresult;

	rc = getgrgid_r(gid, &grp, buf, sizeof(buf), &gresult);
	if (gresult == NULL)
	{
		if (rc == 0)
			ss << gid;
		else
		{
			errno = rc;
			error(UTIL, "Internal error getgrgid_r %d", errno);
		}
	}
	else
	{
		ss << grp.gr_name;
	}

	return ss.str();
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

string eatTo(vector<char> &v, vector<char>::iterator &i, char c, size_t max)
{
	string s;

	while (max > 0 && i != v.end() && *i != c)
	{
		s += *i;
		i++;
		max--;
	}
	if (max == 0 && *i != c)
	{
		return "";
	}
	if (i != v.end())
	{
		i++;
	}
	return s;
}

string toHext(const char *b, size_t len)
{
	string s;
	char buf[32];

	for (size_t j = 0; j < len; j++)
	{
		if (b[j] >= 32 && b[j] <= 'z')
		{
			s += b[j];
		}
		else
		{
			memset(buf, 0, 32);
			snprintf(buf, 31, "~%02x/", ((unsigned int) b[j]) & 255);
			s += buf;
		}
		if (j > 0 && j % 64 == 0)
			s += "\n";
	}

	return s;
}

string toHex(const char *b, size_t len)
{
    string s;
    char buf[3];
    
    for (size_t j = 0; j < len; j++)
    {
        memset(buf, 0, 3);
        snprintf(buf, 2, "%02x", ((unsigned int) b[j]) & 255);
        s += buf;
    }
    return s;
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
		Path *np = new Path(parent, Atom::lookup(basename_(p)));
		interned_paths[p] = np;
		return np;
	}
	Path *np = new Path(NULL, Atom::lookup(basename_(p)));
	interned_paths[p] = np;
	return np;
}

Path *Path::lookupRoot()
{
	return lookup("");
}

const char *Path::c_str()
{
	if (path_cache_ == NULL)
	{
		string p = path();
		path_cache_ = new char[p.length() + 1];
		memcpy(path_cache_, p.c_str(), p.length() + 1);
		path_cache_len_ = p.length();
	}
	return path_cache_;
}

size_t Path::c_str_len()
{
	if (path_cache_ == NULL)
	{
		c_str();
	}
	return path_cache_len_;
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

string Path::path()
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
}

Path *Path::reparent(Path *parent)
{
	return new Path(parent, atom_);
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
			rs += p->name()->literal();
		}
		i++;
	}
	return lookup(rs);
}

Path* Path::prepend(Path *p)
{
	string rs = p->path() + "/" + path();
	return lookup(rs);
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
	interned_paths[""] = new Path(NULL, root);
}

Path::Initializer Path::initializer_s;
