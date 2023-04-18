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
#include "fileinfo.h"
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
static ComponentId TEST_FILEINFOS = registerLogComponent("test_fileinfos");
static ComponentId TEST_GZIP = registerLogComponent("test_filesystem");
static ComponentId TEST_KEEP = registerLogComponent("test_keep");
static ComponentId TEST_FIT = registerLogComponent("test_fit");
static ComponentId TEST_HUMANREADABLE = registerLogComponent("test_human_readable");
static ComponentId TEST_HEXSTRING = registerLogComponent("test_hexstring");
static ComponentId TEST_SPLIT = registerLogComponent("test_split");
static ComponentId TEST_READSPLIT = registerLogComponent("test_readsplit");
static ComponentId TEST_CONTENTSPLIT = registerLogComponent("test_contentsplit");

void testMatch(string pattern, const char *path, bool should_match);

bool verbose_ = false;
bool err_found_ = false;

unique_ptr<System> sys;
unique_ptr<FileSystem> fs;
void testParsing();
void testPaths();
void testMatching();
void testRandom();
void testFileSystem();
void testFileInfos();
void testGzip();
void testKeeps();
void testHumanReadable();
void testHexStrings();
void testFit();
void testSplitLogic();
void testContentSplit();
void testReadSplitLogic();
void testSHA256();

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
        sys = newSystem();
        fs = newDefaultFileSystem(sys.get());
        testParsing();
        testPaths();
        testMatching();
        testRandom();
        testFileSystem();
        testFileInfos();
        testGzip();
        testKeeps();
        testHumanReadable();
        testHexStrings();
//        testFit();
        testSplitLogic();
        testReadSplitLogic();
//        testContentSplit();
        testSHA256();

        if (!err_found_) {
            printf("OK: testinternals\n");
        } else {
            printf("ERROR: testinernals\n");
        }
    }
    catch (string e) {
        fprintf(stderr, "%s\n", e.c_str());
    }
}

void testParsing()
{
    RC rc = RC::OK;
    size_t out;
    out = 0;
    rc = parseHumanReadable("1 GiB", &out);
    assert(rc.isOk() && out == (1024*1024*1024));
    out = 0;
    rc = parseHumanReadable("1G", &out);
    assert(rc.isOk() && out == (1024*1024*1024));
    out = 0;
    rc = parseHumanReadable("1M", &out);
    assert(rc.isOk() && out == (1024*1024));
    out = 0;
    rc = parseHumanReadable("295.037M", &out);
    assert(rc.isOk() && out == 309368717);
    out = 0;
    rc = parseHumanReadable("   94.988M  ", &out);
    assert(rc.isOk() && out == 99602137);
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

void testFileType(const char *path, FileType expected_ft, const char *expected_id)
{
    Path *p = Path::lookup(path);
    FileInfo fi = fileInfo(p);

    if (fi.type != expected_ft || strcmp(fi.identifier, expected_id))
    {
        error(TEST_FILEINFOS, "Expected file type \"%s\" with identifier (%s) for path \"%s\", but got \"%s\" (%s)\n",
              fileTypeName(expected_ft, false), expected_id, path, fileTypeName(fi.type, false), fi.identifier);
    }
}


void testFileInfos()
{
    testFileType("/home/bar/foo.c", FileType::Source, "c");
    testFileType("/home/bar/foo.C", FileType::Source, "c");
    testFileType("/home/intro.tex", FileType::Document, "tex");
    testFileType("/home/intro.docx", FileType::Document, "docx");
}

void testGzip()
{
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

    if (w != r) {
        verbose(TEST_GZIP, "Gzip Gunzip fail!\n");
        err_found_ = true;
    }
}

void testKeep(string k, uint64_t all, uint64_t daily, uint64_t weekly, uint64_t monthly)
{
    Keep keep;

    verbose(TEST_KEEP, "Testing Keep \"%s\" ", k.c_str());
    keep.parse(k);
    if (keep.all != all ||
        keep.daily != daily ||
        keep.weekly != weekly ||
        keep.monthly != monthly)
    {
        verbose(TEST_KEEP, "Keep parse \"%s\" failed!\n", k.c_str());
        verbose(TEST_KEEP, "Expected / Got \n"
                "all=%" PRINTF_TIME_T "d / %" PRINTF_TIME_T "d \n"
                "daily=%" PRINTF_TIME_T "d / %" PRINTF_TIME_T "d \n"
                "weekly=%" PRINTF_TIME_T "d / %" PRINTF_TIME_T "d \n"
                "monthly=%" PRINTF_TIME_T "d / %" PRINTF_TIME_T "d \n",
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
    testKeep("all:10d", 3600*24*10, 0, 0, 0);
    testKeep("all: 7d     daily:2w", 3600*24*7, 3600*24*14, 0, 0);
    testKeep("all:1d daily: 1w weekly:1m monthly:1y",
             3600*24/*all*/, 3600*24*7/*daily*/, 3600*24*31/*weekly*/, 3600*24*366/*monthly*/);
    testKeep("weekly:1y",
             0/*all*/, 0/*daily*/, 3600*24*366/*weekly*/, 0/*monthly*/);
    testKeep("monthly:10y",
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
        fs = newDefaultFileSystem(sys.get());

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
        *p = (char)0xff;
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

void testSHA256()
{
    string gzfile_contents = "ABC";
    vector<char> sha256_hash;
    sha256_hash.resize(SHA256_DIGEST_LENGTH);
    {
        SHA256_CTX sha256ctx;
        SHA256_Init(&sha256ctx);
        SHA256_Update(&sha256ctx, gzfile_contents.c_str(), gzfile_contents.length());
        SHA256_Final((unsigned char*)&sha256_hash[0], &sha256ctx);
    }
    string hex = toHex(sha256_hash);
    //fprintf(stderr, "sha256sum of \"%s\" is %s\n", gzfile_contents.c_str(), hex.c_str());

}
