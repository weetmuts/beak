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

#include "contentsplit.h"
#include "filesystem.h"
#include "fit.h"
#include "log.h"
#include "match.h"
#include "restore.h"
#include "tar.h"
#include "util.h"

#include <assert.h>

using namespace std;

static ComponentId TEST_MATCH = registerLogComponent("test_match");
static ComponentId TEST_RANDOM = registerLogComponent("test_random");
static ComponentId TEST_FILESYSTEM = registerLogComponent("test_filesystem");
static ComponentId TEST_GZIP = registerLogComponent("test_filesystem");
static ComponentId TEST_KEEP = registerLogComponent("test_keep");
static ComponentId TEST_FIT = registerLogComponent("test_fit");
static ComponentId TEST_HUMANREADABLE = registerLogComponent("human_readable");
static ComponentId TEST_HEXSTRING = registerLogComponent("hex_string");
static ComponentId TEST_SPLIT = registerLogComponent("split");
static ComponentId TEST_READSPLIT = registerLogComponent("readsplit");
static ComponentId TEST_CONTENTSPLIT = registerLogComponent("contentsplit");

void testMatch(string pattern, const char *path, bool should_match);

bool verbose_ = false;
bool err_found_ = false;

unique_ptr<FileSystem> fs;
void testPaths();
void testMatching();
void testRandom();
void testFileSystem();
void testGzip();
void testKeeps();
void testHumanReadable();
void testHexStrings();
void testFit();
void testSplitLogic();
void testContentSplit();
void testReadSplitLogic();
void predictor(int argc, char **argv);

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
    if (argc > 1 && string("--predictor") == argv[1]) {
        predictor(argc, argv);
        return 0;
    }
    try {
        fs = newDefaultFileSystem();

        testPaths();
        testMatching();
        testRandom();
        testFileSystem();
        testGzip();
        testKeeps();
        testHumanReadable();
        testHexStrings();
//        testFit();
        testSplitLogic();
        testReadSplitLogic();
//        testContentSplit();

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

void testPaths()
{
    int depth = 0;
    Path *p = Path::lookup("/home/fredrik/.git/objects");
    Path *gp = Path::lookup(".git");

    depth = p->findPart(gp);
    if (depth != 4) {
        error(TEST_MATCH, "Expected findPart %s in %s to return depth %d, but got %d\n",
                gp->c_str(), p->c_str(), 4, depth);
        err_found_ = true;
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
    assert(fs->mkDirpWriteable(test));

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

void testKeep(string k, time_t tz_offset, time_t all, time_t daily, time_t weekly, time_t monthly)
{
    Keep keep;

    verbose(TEST_KEEP, "Testing Keep \"%s\" ", k.c_str());
    keep.parse(k);
    if (keep.tz_offset != tz_offset ||
        keep.all != all ||
        keep.daily != daily ||
        keep.weekly != weekly ||
        keep.monthly != monthly)
    {
        verbose(TEST_KEEP, "Keep parse \"%s\" failed!\n", k.c_str());
        verbose(TEST_KEEP, "Expected / Got \n"
                "tz=%" PRINTF_TIME_T "d / %" PRINTF_TIME_T "d \n"
                "all=%" PRINTF_TIME_T "d / %" PRINTF_TIME_T "d \n"
                "daily=%" PRINTF_TIME_T "d / %" PRINTF_TIME_T "d \n"
                "weekly=%" PRINTF_TIME_T "d / %" PRINTF_TIME_T "d \n"
                "monthly=%" PRINTF_TIME_T "d / %" PRINTF_TIME_T "d \n",
                tz_offset, keep.tz_offset,
                all, keep.all,
                daily, keep.daily,
                weekly, keep.weekly,
                monthly, keep.monthly);
        err_found_ = true;
    } else {
        verbose(TEST_KEEP, " OK\n");
    }
}

void testKeeps()
{
    testKeep("tz:+0100 all:10d", 3600, 3600*24*10, 0, 0, 0);
    testKeep("tz:+0000  all: 7d     daily:2w", 0, 3600*24*7, 3600*24*14, 0, 0);
    testKeep(" tz : -1030 all:1d daily: 1w weekly:1m monthly:1y", -3600*10.5,
             3600*24/*all*/, 3600*24*7/*daily*/, 3600*24*31/*weekly*/, 3600*24*366/*monthly*/);
    testKeep("tz:+0500 weekly:1y", 3600*5,
             0/*all*/, 0/*daily*/, 3600*24*366/*weekly*/, 0/*monthly*/);
    testKeep("tz:+1001 monthly:10y", 3600*10+60,
             0/*all*/, 0/*daily*/, 0/*weekly*/, 366*24*3600*10/*monthly*/);
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

void testHRTime(size_t v, string expected) {
    string s = humanReadableTimeTwoDecimals(v);
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

    testHRTime(123, "123us");
    testHRTime(43232, "43.23ms");
    testHRTime(970000000, "970.00s");
}

void testHexString(size_t v, size_t mv, string expected) {
    string s = toHex(v, mv);
    if (s == expected) {
        debug(TEST_HEXSTRING,"%ju = %s\n", v, s.c_str());
    } else {
        err_found_ = true;
        debug(TEST_HEXSTRING,"%ju = %s but expected %s\n", v, s.c_str(), expected.c_str());
    }
}

void testHexStrings()
{
    testHexString(2, 8, "2");
    testHexString(32, 60, "20");
    testHexString(53, 2160, "035");
    testHexString(54, 65535, "0036");
    testHexString(192, 193, "c0");
    testHexString(1234567, 99999999, "012d687");
}

void testFit()
{
    vector<pair<double,double>> xy;
    xy.push_back({ -3, 0.9 });
    xy.push_back({ -2, 0.8 });
    xy.push_back({ -1, 0.4 });
    xy.push_back({ -0.2, 0.2 });
    xy.push_back({ 1, 0.1 });
    xy.push_back({ 3, 0 });

    double a,b,c;

    fitSecondOrderCurve(xy, &a, &b, &c);

    if (int(a*1000)!=27 ||
        int(b*1000)!=-162 ||
        int(c*1000)!=229) {
        verbose(TEST_FIT, "Error in fit, expected 0.0278 0.1628 0.2291 but got %f %f %f\n",
                a, b, c);
    }
    else
    {
        verbose(TEST_FIT, "Test fit, second order OK\n");
    }

    vector<pair<double,double>> xy2;
    xy2.push_back({ 0.1, 0.1});
    xy2.push_back({ 0.35, 0.45 });
    xy2.push_back({ 0.6, 0.8 });

    double aa,bb;

    fitFirstOrderCurve(xy2, &aa, &bb);

    if (int(a*10)!=14 ||
        int(b*100)!=4) {
        verbose(TEST_FIT, "Error in fit, expected 1.4 -0.04 but got %f %f\n",
                a, b);
    }
    else
    {
        verbose(TEST_FIT, "Test fit, 1st order OK\n");
    }
}


void predictor(int argc, char **argv)
{
    if (argc < 3) {
        error(TEST_FIT,"You must supply a log file with statistics.\n");
    }
    try {
        fs = newDefaultFileSystem();

        Path *log = Path::lookup(argv[2]);
        vector<char> buf;
        RC rc = fs->loadVector(log, 32768, &buf);
        if (rc.isErr()) {
            error(TEST_FIT,"Could not read file \"%s\"\n", log->c_str());
        }

        vector<SecsBytes> secsbytes;
        bool eof = false, err = false;
        auto i = buf.begin();
        while (!eof) {
            // statistics: stored(secs,bytes)\t2.00\t340639232
            string line = eatTo(buf, i,  '\n', 1024, &eof, &err);
            if (err) break;
            string pre = line.substr(0,30);
            if (pre == "statistics: stored(secs,bytes)") {
                size_t tab1 = line.find('\t',32);
                size_t tab2 = line.find('\t',tab1+1);
                string secs_s = line.substr(31,tab1-31);
                string bytes_s = line.substr(tab1+1,tab2);
                double bytes = (double)atol(bytes_s.c_str());
                double secs = (double)atol(secs_s.c_str());
                secsbytes.push_back({secs,bytes});
            }
        }
        size_t max_bytes = secsbytes[secsbytes.size()-1].bytes;
        for (size_t i = 0; i<secsbytes.size(); ++i) {
            double secs = secsbytes[i].secs;
            double bytes = secsbytes[i].bytes;
            double eta_1s_speed, eta_immediate, eta_average;
            predict_all(secsbytes, i, max_bytes, &eta_1s_speed, &eta_immediate, &eta_average);
            printf("statistics: stored(secs,bytes)\t"
                   "%.1f\t"
                   "%.0f\t"
                   "%.0f\t"
                   "%.0f\t"
                   "%.0f\n",
                   secs,
                   bytes,
                   eta_1s_speed,
                   eta_immediate,
                   eta_average);
        }
    } catch (string e) {
        fprintf(stderr, "ERR: %s\n", e.c_str());
    }
}

extern void splitParts_(size_t tar_file_size, // Includes the tar headers for the file.
                        size_t split_size,
                        TarHeaderStyle ths,
                        uint *num_parts,
                        size_t *part_size,
                        size_t *last_part_size,
                        size_t *part_header_size);

void splitCheck(const char *test, size_t file_size, size_t tar_header_size, TarHeaderStyle type, size_t split_size,
                uint expect_num_parts, size_t expect_part_size, size_t expect_last_part_size, size_t expect_part_header_size)

{
    size_t tar_size = file_size+tar_header_size;
    uint num_parts = 0;
    size_t part_size = 0;
    size_t last_part_size = 0;
    size_t part_header_size = 0;

    splitParts_(tar_size, split_size, type, &num_parts, &part_size, &last_part_size, &part_header_size);
    verbose(TEST_SPLIT, "%s\n"
            "file_size=%zu tar_header_size=%zu tar_size=%zu num_parts=%u part_size=%zu last_part_size=%zu part_header_size=%zu\n",
            test, file_size, tar_header_size, tar_size, num_parts, part_size, last_part_size, part_header_size);

    if (num_parts != expect_num_parts || part_size != expect_part_size ||
        last_part_size != expect_last_part_size || part_header_size != expect_part_header_size)
    {
        error(TEST_SPLIT,"Split calculated the wrong values!\n");
    }
}

void testSplitLogic()
{

    // Small tar header for original file that is going to be split.
    splitCheck("Simple header, 700M / 50M", 700*1024*1024, 512, Simple, 50*1024*1024,
               15, 50*1024*1024, 7680, 512);

    splitCheck("Large header, 700M / 50M", 700*1024*1024, 512*3, Simple, 50*1024*1024,
               15, 50*1024*1024, 8704, 512);

    splitCheck("No headers, 500M / 50M", 500*1024*1024, 0, None, 50*1024*1024,
               10, 50*1024*1024, 50*1024*1024, 0);

    splitCheck("Tiny parts No headers, 32768 / 1024", 32768, 0, None, 1024,
               32, 1024, 1024, 0);

    splitCheck("Tiny parts Small headers, 32768 / 1024", 32768, 512, Simple, 1024,
               64, 1024, 1024, 512);

    splitCheck("Tiny parts Small headers except tar header, 32768 / 1024", 32768, 512*3, Simple, 1024,
               66, 1024, 1024, 512);

}

void testReadSplitLogic()
{
    size_t file_size = 3*1000*1000;
    size_t split_size = 500*1000;
    size_t header_size = 512*3;
    size_t total_size = file_size+header_size;
    char *from = new char[total_size];
    char *to = new char[total_size];
    char *p = from;
    for (size_t i=0; i<header_size; ++i) {
        *p = 255;
        p++;
    }
    for (size_t i=0; i<file_size; ++i) {
        *p = i%256;
        p++;
    }
    RestoreEntry re;
    re.offset_ = header_size;
    splitParts_(file_size+header_size, split_size, Simple, &re.num_parts,
                &re.part_size, &re.last_part_size, &re.part_offset);

    verbose(TEST_READSPLIT, "Read test file: file_size=%zu split_size=%zu => "
            "num_parts=%zu part_size=%zu last_part_size=%zu part_offset=%zu\n",
            file_size, split_size, re.num_parts, re.part_size, re.last_part_size, re.part_offset);

    size_t sum = 0;
    re.readParts(0, to, 3*1000*1000,
                 [&](uint partnr, off_t offset_inside_part, char *buffer, size_t length) {
                     sum+=length;
                     verbose(TEST_READSPLIT, "Reading part=%u offset_inside_part=%zd len=%zu sum=%zu\n",
                             partnr, offset_inside_part, length, sum);
                     return (ssize_t)0;
                 });

    delete [] from;
    delete [] to;
}

void testContentSplit()
{
    vector<ContentChunk> chunks;
    splitContent(Path::lookup("gurka"), &chunks, ((size_t)100));

    error(TEST_CONTENTSPLIT,"Content split calculated the wrong values.\n");

}
