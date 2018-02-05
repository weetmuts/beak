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
#include <openssl/sha.h>
#include <stddef.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <utility>
#include <zlib.h>

using namespace std;

char separator = 0;
string separator_string = string("\0",1);
struct timespec start_time_;

#define KB 1024ull

void strprintf(std::string &s, const char* fmt, ...)
{
    char buf[4096];
    va_list args;
    va_start(args, fmt);
    vsnprintf(buf, 4095, fmt, args);
    va_end(args);
    s = buf;
}

string humanReadable(size_t s)
{
    if (s < KB)
    {
        return to_string(s) + " B";
    }
    if (s < KB * KB)
    {
        s /= KB;
        return to_string(s) + " KiB";
    }
    if (s < KB * KB * KB)
    {
        s /= KB * KB;
        return to_string(s) + " MiB";
    }
    if (s < KB * KB * KB * KB)
    {
        s /= KB * KB * KB;
        return to_string(s) + " GiB";
    }
    if (s < KB * KB * KB * KB * KB)
    {
        s /= KB * KB * KB * KB;
        return to_string(s) + " TiB";
    }
    s /= KB * KB * KB * KB * KB;
    return to_string(s) + " PiB";
}

string helper(size_t scale, size_t s, string suffix)
{
    size_t o = s;
    s /= scale;
    size_t diff = o-(s*scale);
    if (diff == 0) {
        return to_string(s) + ".00 "+suffix;
    }
    size_t dec = (int)(100*(diff+1) / scale);
    return to_string(s) + ((dec<10)?".0":".") + to_string(dec) + " "+suffix;
}

string humanReadableTwoDecimals(size_t s)
{
    if (s < KB)
    {
        return to_string(s) + "B";
    }
    if (s < KB * KB)
    {
        return helper(KB, s, "KiB");
    }
    if (s < KB * KB * KB)
    {
        return helper(KB*KB, s, "MiB");
    }
#if SIZEOF_SIZE_T == 8
    if (s < KB * KB * KB * KB)
    {
        return helper(KB*KB*KB, s, "GiB");
    }
    if (s < KB * KB * KB * KB * KB)
    {
        return helper(KB*KB*KB*KB, s, "TiB");
    }
    return helper(KB*KB*KB*KB*KB, s, "PiB");
#else
    return helper(KB*KB*KB, s, "GiB");
#endif
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
    string suffix;
    char c = s.back();

    while (c < '0' || c > '9') {
        if (c != ' ') {
            suffix = s.back() + suffix;
        }
        s.pop_back();
        c = s.back();
    }

    if (s.length() > 256)
    {
        return ERR;
    }
    if (suffix == "K" || suffix == "KiB")
    {
        mul = KB;
    }
    else if (suffix == "M" || suffix == "MiB")
    {
        mul = KB * KB;
    }
    else if (suffix == "G" || suffix == "GiB")
    {
        mul = KB * KB * KB;
    }
    else if (suffix == "T" || suffix == "TiB")
    {
        mul = KB * KB;
        mul *= KB * KB;
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

bool parseTimeZoneOffset(std::string o, time_t *out)
{
    if (o.length() != 5) return false;
    if (o[0] != '-' && o[0] != '+') return false;
    if (!isdigit(o[1]) || !isdigit(o[2]) || !isdigit(o[3]) || !isdigit(o[4])) return false;
    int hh = o[1]-48;
    int hl = o[2]-48;
    int mh = o[3]-48;
    int ml = o[4]-48;
    time_t offset = 60*(ml+mh*10)+3600*(hl+hh*10);
    if (o[0] == '-') *out = -offset;
    else *out = offset;
    return true;
}

string getLengthOfTime(time_t t)
{
    if (0 == (t % (3600*24*366))) {
        return to_string(t/(3600*24*366))+"y";
    }
    if (0 == (t % (3600*24*31))) {
        return to_string(t/(3600*24*31))+"m";
    }
    if (0 == (t % (3600*24*7))) {
        return to_string(t/(3600*24*7))+"w";
    }
    if (0 == (t % (3600*24))) {
        return to_string(t/(3600*24))+"d";
    }
    return "";
}

bool parseLengthOfTime(string s, time_t *out)
{
    time_t mul = 1;
    char c = s.back();

    if (s.length() > 16)
    {
        return false;
    }
    s = s.substr(0, s.length() - 1);
    if (c == 'd')
    {
        mul = 3600*24;
    }
    else if (c == 'w')
    {
        mul = 3600*24*7;
    }
    else if (c == 'm')
    {
        mul = 3600*24*31;
    }
    else if (c == 'y')
    {
        mul = 3600*24*366;
    }
    else
    {
        return false;
    }

    for (auto c : s)
    {
        if (!isdigit(c))
        {
            return false;
        }
    }

    *out = mul * atol(s.c_str());
    if (*out == 0) return false;
    return true;
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

string eatToSkipWhitespace(vector<char> &v, vector<char>::iterator &i, int c, size_t max, bool *eof, bool *err)
{
    eatWhitespace(v, i, eof);
    if (*eof) {
        if (c != -1) {
            *err = true;
        }
        return "";
    }
    string s = eatTo(v,i,c,max,eof,err);
    trimWhitespace(&s);
    return s;
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

void eatWhitespace(vector<char> &v, vector<char>::iterator &i, bool *eof)
{
    *eof = false;
    while (i != v.end() && (*i == ' ' || *i == '\t'))
    {
        i++;
    }
    if (i == v.end()) {
        *eof = true;
    }
}

void trimWhitespace(string *s)
{
    const char *ws = " \t";
    s->erase(0, s->find_first_not_of(ws));
    s->erase(s->find_last_not_of(ws) + 1);
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

#ifdef PLATFORM_WINAPI
#define LOCALE_STRING "C"
#else
#define LOCALE_STRING ""
#endif

locale const user_locale(LOCALE_STRING);

locale const *getLocale()
{
    return &user_locale;
}

wstring to_wstring(string const& s)
{
    wstring_convert<codecvt_utf8<wchar_t> > conv;
    return conv.from_bytes(s);
}

string wto_string(wstring const& s)
{
    wstring_convert<codecvt_utf8<wchar_t> > conv;
    return conv.to_bytes(s);
}

string tolowercase(string const& s)
{
    auto ss = to_wstring(s);
    for (auto& c : ss)
    {
        c = tolower(c, user_locale);
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

    string s = "";
    if (diff == 0) {
        return "a second "+msg;
    }
    if (diff < 60) {
        return "a minute "+msg;
    }
    if (diff < 60*60) {
        int minutes = diff/60;
	if (minutes > 1) s = "s";
        return to_string(minutes)+" minute"+s+" "+msg;
    }
    if (diff < 60*60*24) {
        int hours = diff/(60*60);
	if (hours > 1) s = "s";
        return to_string(hours)+" hour"+s+" "+msg;
    }
    if (diff < 60*60*24*7) {
        int days = diff/(60*60*24);
	if (days > 1) s = "s";
	return to_string(days)+" day"+s+" "+msg;
    }
    if (diff < 60*60*24*7*4) {
        int weeks = diff/(60*60*24*7);
	if (weeks > 1) s = "s";
        return to_string(weeks)+" week"+s+" "+msg;
    }
    int months = diff/(60*60*24*7*4);
    if (months > 1) s = "s";
    return to_string(months)+" month"+s+" "+msg;
}

void toLittleEndian(uint16_t *t)
{}

void toLittleEndian(uint32_t *t)
{}

void fixEndian(long *t)
{
}

struct Entropy {
    SHA256_CTX sha256ctx;
    vector<char> sha256_hash_;
    vector<char> pool_;

    Entropy() {
        uint64_t a, b;

        a = clockGetUnixTime();
        b = clockGetTime();
        SHA256_Init(&sha256ctx);
        SHA256_Update(&sha256ctx, &a, sizeof(a));
        SHA256_Update(&sha256ctx, &b, sizeof(b));

        sha256_hash_.resize(SHA256_DIGEST_LENGTH);
        SHA256_Final((unsigned char*)&sha256_hash_[0], &sha256ctx);
        pool_.resize(SHA256_DIGEST_LENGTH);
        memcpy(&pool_[0], &sha256_hash_[0], SHA256_DIGEST_LENGTH);
    }

    vector<unsigned char> getBytes(int len) {
        assert(len<=SHA256_DIGEST_LENGTH/2);

        // Extract a string
        char buf[len];
        for (int i=0; i<len; ++i) {
            buf[i] = pool_[i]^pool_[i+SHA256_DIGEST_LENGTH/2];
        }

        uint64_t c = clockGetTime();
        SHA256_Update(&sha256ctx, &c, sizeof(c));
        SHA256_Update(&sha256ctx, &pool_[0], SHA256_DIGEST_LENGTH);
        SHA256_Final((unsigned char*)&sha256_hash_[0], &sha256ctx);
        for (int i=0; i<SHA256_DIGEST_LENGTH; ++i) {
            pool_[i] ^= sha256_hash_[i];
        }

        return vector<unsigned char>(buf, buf+len);
    }
};

static Entropy entropy_;

string randomUpperCaseCharacterString(int len)
{
    assert(len<=SHA256_DIGEST_LENGTH/2);

    vector<unsigned char> v = entropy_.getBytes(len);
    string s;
    s.resize(len);

    for (int i=0; i<len; ++i) {
        unsigned char x = v[i]%36;
        if (x<10) {
            s[i] = '0'+x;
        } else {
            s[i] = 'A'+x-10;
        }
    }
    return s;
}

#define CHUNK_SIZE 128*1024

void compress_memory(char *in, size_t len, vector<char> *to)
{
    char chunk[CHUNK_SIZE];

    z_stream strm;
    strm.zalloc = 0;
    strm.zfree = 0;
    strm.next_in = (unsigned char*)in;
    strm.avail_in = len;
    strm.next_out = (unsigned char*)chunk;
    strm.avail_out = CHUNK_SIZE;

    gz_header head;
    memset(&head, 0, sizeof(head));

    int rc = deflateInit2_(&strm, Z_BEST_COMPRESSION, Z_DEFLATED, MAX_WBITS + 16, MAX_MEM_LEVEL,
                           Z_DEFAULT_STRATEGY, ZLIB_VERSION, (int)sizeof(z_stream));
    assert(rc == Z_OK);

    rc = deflateSetHeader(&strm, &head);
    assert(rc == Z_OK);

    while (strm.avail_in != 0)
    {
        int res = deflate(&strm, Z_NO_FLUSH);
        assert(res == Z_OK);
        if (strm.avail_out == 0)
        {
            to->insert(to->end(), chunk, chunk+CHUNK_SIZE);
            strm.next_out = (unsigned char*)chunk;
            strm.avail_out = CHUNK_SIZE;
        }
    }

    int deflate_res = Z_OK;
    while (deflate_res == Z_OK)
    {
        if (strm.avail_out == 0)
        {
            to->insert(to->end(), chunk, chunk + CHUNK_SIZE);
            strm.next_out = (unsigned char*)chunk;
            strm.avail_out = CHUNK_SIZE;
        }
        deflate_res = deflate(&strm, Z_FINISH);
    }

    assert(deflate_res == Z_STREAM_END);
    to->insert(to->end(), chunk, chunk+CHUNK_SIZE-strm.avail_out);
    deflateEnd(&strm);
}

int gzipit(string *from, vector<char> *to)
{
    compress_memory(&(*from)[0], from->length(), to);
    return OK;
}

void decompress_memory(char *in, size_t len, std::vector<char> *to)
{
    char chunk[CHUNK_SIZE];

    z_stream strm;
    strm.zalloc = 0;
    strm.zfree = 0;
    strm.next_in = (unsigned char*)in;
    strm.avail_in = len;
    strm.next_out = (unsigned char*)chunk;
    strm.avail_out = CHUNK_SIZE;

    int rc = inflateInit2(&strm, MAX_WBITS + 16);
    assert(rc == Z_OK);

    do {
        strm.avail_out = CHUNK_SIZE;
        strm.next_out = (unsigned char*)chunk;
        rc = inflate(&strm, Z_NO_FLUSH);
        assert(rc != Z_STREAM_ERROR);

        size_t have = CHUNK_SIZE-strm.avail_out;
        to->insert(to->end(), chunk, chunk+have);
    } while (strm.avail_out == 0);

    assert(rc == Z_STREAM_END);
    inflateEnd(&strm);
}

int gunzipit(vector<char> *from, vector<char> *to)
{
    decompress_memory(&(*from)[0], from->size(), to);
    return OK;
}

int stringToType(std::string s, char **names, int n)
{
    for (int i=0; i<n; ++i) {
        if (s == names[i]) {
            return i;
        }
    }
    return -1;
}

time_t getTimeZoneOffset()
{
    time_t rawtime = time(NULL);
    struct tm *ptm = gmtime(&rawtime);
    time_t gmt = mktime(ptm);
    ptm = localtime(&rawtime);
    time_t offset = rawtime - gmt;
    // We ignore dayligt savings here. (ptm->tm_isdst)
    return offset;
}

string getTimeZoneOffsetAsString(time_t t)
{
    string s;

    if (t>=0) s += "+";
    else {    s += "-"; t = -t; }

    time_t hours = t/3600;
    time_t hoursten = hours/10;
    time_t hoursone = hours-hoursten*10;

    time_t minutes = (t-hours*3600)/60;
    time_t minutesten = minutes/10;
    time_t minutesone = minutes-minutesten*10;

    assert(t == (hoursten*3600*10 + hoursone*3600 + minutesten*60*10 + minutes*60));
    assert(hoursten <= 1 && hoursten >= 0);
    assert(hoursone <= 9 && hoursone >= 0);
    assert(minutesten <= 5 && minutesten >= 0);
    assert(minutesone <= 9 && minutesone >= 0);

    s += to_string(hoursten);
    s += to_string(hoursone);
    s += to_string(minutesten);
    s += to_string(minutesone);
    return s;
}
