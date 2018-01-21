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

void testMatch(string pattern, const char *path, bool should_match)
    throw (string);

bool verbose_ = false;
bool err_found_ = false;

unique_ptr<FileSystem> fs;
void testMatching();
void testRandom();
void testFileSystem();
void testGzip();

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
