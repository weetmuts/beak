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

#include "filesystem.h"
#include "log.h"
#include "match.h"
#include "util.h"

#include <assert.h>

using namespace std;

static ComponentId TEST_MATCH = registerLogComponent("test_match");
static ComponentId TEST_RANDOM = registerLogComponent("test_random");
static ComponentId TEST_FILESYSTEM = registerLogComponent("test_filesystem");
static ComponentId TEST_GZIP = registerLogComponent("test_filesystem");
static ComponentId TEST_KEEP = registerLogComponent("test_keep");
static ComponentId TEST_HUMANREADABLE = registerLogComponent("human_readable");

void testMatch(string pattern, const char *path, bool should_match)
    throw (string);

bool verbose_ = false;
bool err_found_ = false;

unique_ptr<FileSystem> fs;
void testMatching();
void testRandom();
void testFileSystem();
void testGzip();
void testKeeps();
void testHumanReadable();

int main(int argc, char *argv[])
{
    if (argc > 1 && string("--verbose") == argv[1]) {
        setLogLevel(VERBOSE);
        verbose_ = true;
    }
    if (argc > 1 && string("--debug") == argv[1]) {
        setLogLevel(DEBUG);
        setLogComponents("all");
    }
    try {
        fs = newDefaultFileSystem();

        testMatching();
        testRandom();
        testFileSystem();
        testGzip();
        testKeeps();
        testHumanReadable();

        if (!err_found_) {
            printf("OK\n");
        } else {
            printf("Errors detected!\n");
        }
    }
    catch (string e) {
        fprintf(stderr, "%s\n", e.c_str());
    }
}

void testMatching()
{
    testMatch("/Alfa/**", "Alfa/beta/gamma", false);
    testMatch("/Alfa/**", "/Alfa/beta/gamma", true);
    testMatch("/Alfa/beta/**", "/Alfa/beta/gamma", true);
    testMatch("/Alfa/beta/**", "/Alfa/betagamma", false);

    testMatch("Alfa/**", "Alfa/beta/gamma", true);
    testMatch("Alfa/**", "AlfaBeta/gamma", false);
    testMatch("Alfa/**", "/xx/yy/Alfa/gamma", true);

    testMatch("*.jpg", "alfa.jpg", true);
    testMatch("*.jpg", "/Alfa/betA/x.jpg", true);
    testMatch("*.jpg", ".jpgalfa", false);

    testMatch("log*", "log.txt", true);
    testMatch("loggo*", "/Alfa/Beta/loggo*", true);
    testMatch("log*", "/log", true);
    testMatch("log*", "alfalog", false);
}

void testMatch(string pattern, const char *path, bool should_match)
    throw (string)
{
    if (should_match) {
        verbose(TEST_MATCH,"\"%s\" matches pattern \"%s\" ", path, pattern.c_str());
    }
    else {
        verbose(TEST_MATCH,"\"%s\" should not match pattern \"%s\" ", path, pattern.c_str());
    }
    Match m;
    m.use(pattern);
    bool r = m.match(path);

    if (r == should_match) {
        verbose(TEST_MATCH, "OK\n");
    }
    else {
        verbose(TEST_MATCH, "ERR!\n");
        err_found_ = true;
    }

    if (!verbose_) {
        if (r != should_match) {
            string s = "";
            if (!should_match) s = " NOT ";
            throw string("Failure: ")+pattern+" should "+s+" match "+path;
        }
    }
}

void testRandom()
{
    for (int i=0; i<100; ++i) {
        string s = randomUpperCaseCharacterString(6);
        verbose(TEST_RANDOM, "RND=>%s<\n", s.c_str());
    }
}

void testFileSystem()
{
    Path *p = fs->mkTempDir("beak_test");
    fs->mkDir(p, "alfa");
    fs->mkDir(p, "beta");

    vector<Path*> contents;
    bool b = fs->readdir(p, &contents);
    if (!b) {
        assert(0);
    } else {
        for (auto i : contents) {
            verbose(TEST_FILESYSTEM,"DIRENTRY %s\n", i->c_str());
        }
    }

    Path *test = p->append("x/y/z");
    assert(fs->mkDirp(test));

    Path *rp = contents[0]->realpath();
    verbose(TEST_FILESYSTEM,"REALPATH %s %s\n", contents[0]->c_str(), rp->c_str());
}


void testGzip() {
    string s = "Hejsan Hoppsan ";
    string w;
    for (int i=0; i<10; ++i) {
        w += s;
    }

    vector<char> buf;
    gzipit(&w, &buf);
    vector<char> out;
    gunzipit(&buf, &out);

    string r(out.begin(), out.end());

//    printf("FROM \"%s\"\n", w.c_str());
//    printf("TO   \"%s\"\n", r.c_str());
    if (w != r) {
        verbose(TEST_GZIP, "Gzip Gunzip fail!\n");
        err_found_ = true;
    }
}

void testKeep(string k, time_t tz_offset, time_t all, time_t daily, time_t weekly,
              time_t monthly, time_t yearly)
{
    Keep keep;

    verbose(TEST_KEEP, "Testing Keep \"%s\"\n", k.c_str());
    keep.parse(k);
    if (keep.tz_offset != tz_offset ||
        keep.all != all ||
        keep.daily != daily ||
        keep.weekly != weekly ||
        keep.monthly != monthly ||
        keep.yearly != yearly)
    {
        verbose(TEST_KEEP, "Keep parse \"%s\" failed!\n", k.c_str());
        verbose(TEST_KEEP, "Expected / Got \n"
                "tz=%" PRINTF_TIME_T "d / %" PRINTF_TIME_T "d \n"
                "all=%" PRINTF_TIME_T "d / %" PRINTF_TIME_T "d \n"
                "daily=%" PRINTF_TIME_T "d / %" PRINTF_TIME_T "d \n"
                "weekly=%" PRINTF_TIME_T "d / %" PRINTF_TIME_T "d \n"
                "monthly=%" PRINTF_TIME_T "d / %" PRINTF_TIME_T "d \n"
                "yearly=%" PRINTF_TIME_T "d / %" PRINTF_TIME_T "d \n",
                tz_offset, keep.tz_offset,
                all, keep.all,
                daily, keep.daily,
                weekly, keep.weekly,
                monthly, keep.monthly,
                yearly, keep.yearly);
        err_found_ = true;
    }
}

void testKeeps()
{
    testKeep("tz:+0100 all:10d", 3600, 3600*24*10, 0, 0, 0, 0);
    testKeep("tz:+0000  all: 7d     daily:2w", 0, 3600*24*7, 3600*24*14, 0, 0, 0);
    testKeep(" tz : -1030 all:1d daily: 1w weekly:1m monthly:1y", -3600*10.5,
             3600*24/*all*/, 3600*24*7/*daily*/, 3600*24*31/*weekly*/, 3600*24*366/*monthly*/, 0/*yearly*/);
    testKeep("tz:+0500 weekly:1y", 3600*5,
             0/*all*/, 0/*daily*/, 3600*24*366/*weekly*/, 0/*monthly*/, 0/*yearly*/);
    testKeep("tz:+1001 yearly:10y", 3600*10+60,
             0/*all*/, 0/*daily*/, 0/*weekly*/, 0/*monthly*/, 3600*24*366*10/*yearly*/);
}

void testHR(size_t v, string expected) {
    string s = humanReadableTwoDecimals(v);
    if (s == expected) {
        debug(TEST_HUMANREADABLE,"%ju = %s\n", v, s.c_str());
    } else {
        err_found_ = true;
        debug(TEST_HUMANREADABLE,"%ju = %s but expected %s\n", v, s.c_str(), expected.c_str());
    }
}

void testHumanReadable()
{
    testHR(65536, "64.00 KiB");
    testHR(66000, "64.45 KiB");
    testHR(65536+1024*3.5, "67.50 KiB");
    testHR(65536+1024*3.02, "67.02 KiB");
    testHR(1024*1024*3.5, "3.50 MiB");
#if SIZEOF_TIME_T == 8
    testHR(1024*1024*1024*512.77, "512.77 GiB");
    testHR(1024*1024*1024*1023.99, "1023.99 GiB");
#endif
}
