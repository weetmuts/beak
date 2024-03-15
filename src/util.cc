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
    int n = vsnprintf(buf, 4095, fmt, args);
    va_end(args);
    assert(n >= 0 && n <= 4095);
    buf[n] = 0;
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
        return to_string(s) + ".00"+suffix;
    }
    size_t dec = (int)(100*(diff+1) / scale);
    return to_string(s) + ((dec<10)?".0":".") + to_string(dec) + suffix;
}

string humanReadableTwoDecimals(size_t s)
{
    if (s < KB)
    {
        return to_string(s) + " B";
    }
    if (s < KB * KB)
    {
        return helper(KB, s, " KiB");
    }
    if (s < KB * KB * KB)
    {
        return helper(KB*KB, s, " MiB");
    }
#if SIZEOF_SIZE_T == 8
    if (s < KB * KB * KB * KB)
    {
        return helper(KB*KB*KB, s, " GiB");
    }
    if (s < KB * KB * KB * KB * KB)
    {
        return helper(KB*KB*KB*KB, s, " TiB");
    }
    return helper(KB*KB*KB*KB*KB, s, " PiB");
#else
    return helper(KB*KB*KB, s, " GiB");
#endif
}

std::string humanReadableTimeTwoDecimals(uint64_t micros)
{
    if (micros < 1000)
    {
        return to_string(micros) + "us";
    }
    if (micros < 1000 * 1000)
    {
        return helper(1000, micros, "ms");
    }
    return helper(1000*1000, micros, "s");
}

std::string humanReadableTime(int seconds, bool show_seconds)
{
    int days  = seconds / (3600*24);
    seconds -= (days * 3600*24);
    int hours = seconds / 3600;
    seconds -= (hours * 3600);
    int minutes = seconds / 60;
    seconds -= (minutes * 60);

    std::string s = "";
    if (days > 0) {
        s += to_string(days)+"d";
    }
    if (hours > 0) {
        s += to_string(hours)+"h";
    }
    if (minutes > 0) {
        if (!show_seconds) minutes++;
        s += to_string(minutes)+"m";
    }
    if (show_seconds) {
        if (seconds < 10 && s.length() > 0) {
            s += "0";
        }
        s += to_string(seconds)+"s";
    }
    return s;
}

std::string toHex(size_t value, size_t max_value)
{
    // The max_value is used to calculate the width of the string,
    // since we left pad with zeroes here.
    char format[16];
    int n = 0;
    while (max_value != 0) {
        n++;
        max_value >>= 4;
    }
    assert(n < 8);
    snprintf(format, sizeof(format), "%%0%dx", n);

    char buf[64];
    snprintf(buf, sizeof(buf), format, value);
    return buf;
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

string keepDigits(string &s)
{
    string r;

    for (auto c : s)
    {
        if (isdigit(c)) {
            r.push_back(c);
        }
    }
    return r;
}

RC parseHumanReadable(string s, size_t *out)
{
    size_t mul = 1;
    string suffix;
    char c = s.back();

    while (s.front() == ' ') s.erase(0,1);

    // Extract the suffix from the end
    while (c < '0' || c > '9') {
        if (c != ' ') {
            suffix = s.back() + suffix;
        }
        s.pop_back();
        c = s.back();
    }

    if (s.length() > 256)
    {
        return RC::ERR;
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

    string n;
    bool found_dot = false, found_digit = false, found_digit_after_dot = false;
    for (auto c : s)
    {
        if (isdigit(c)) {
            n.push_back(c);
            found_digit = true;
            if (found_dot) {
                found_digit_after_dot = true;
            }
        }
        else
        if (c == '.' && found_digit && !found_dot) {
            n.push_back(c);
            found_dot = true;
        }
        else
        {
            return RC::ERR;
        }
    }

    if (found_dot) {
        if (!found_digit_after_dot) {
            return RC::ERR;
        }
        *out = (size_t)(((double)mul) * atof(n.c_str()));
    }
    else
    {
        *out = mul * atol(n.c_str());
    }
    return RC::OK;
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

    if (s == "forever") {
        // Forever is 500 years....
        // Its because uin64_t nanos will only run to that.
        *out = 3600ull*24*366*500;
        return true;
    }
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
    if (c != -1 && (i == v.end() || *i != c))
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

string toHexAndText(const char *b, size_t len, int line_length)
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
        if (j > 0 && j % line_length == 0)
            s.append("\n");
    }

    return s;
}

string toHexAndText(vector<char> &b, int line_length)
{
    return toHexAndText(&b[0], b.size(), line_length);
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
    if (b.size() == 0) return "";
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

bool hex2bin(string s, vector<char> *target)
{
    char *src = &s[0];
    if (!src) return false;
    while(*src && src[1]) {
        if (*src == ' ') {
            src++;
        } else {
            int a = char2int(*src);
            int b = char2int(src[1]);
            if (a < 0 || b < 0) return false;
            target->push_back(a*16 + b);
            src += 2;
        }
    }
    return true;
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

bool greaterThan(struct timespec *a, struct timespec *b)
{
    return a->tv_sec > b->tv_sec || (a->tv_sec == b->tv_sec && a->tv_nsec > b->tv_nsec);
}

bool isInTheFuture(const struct timespec *tm)
{
    // What happens with summer and winter time changes?
    return tm->tv_sec > start_time_.tv_sec ||
        (tm->tv_sec == start_time_.tv_sec && tm->tv_nsec > start_time_.tv_nsec);
}

string timeAgo(const struct timespec *tm)
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

        a = clockGetUnixTimeSeconds();
        b = clockGetTimeMicroSeconds();
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

        uint64_t c = clockGetTimeMicroSeconds();
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

RC compress_memory(char *in, size_t len, vector<char> *to)
{
    RC rc = RC::OK;
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

    int rcd = deflateInit2_(&strm, Z_BEST_COMPRESSION, Z_DEFLATED, MAX_WBITS + 16, MAX_MEM_LEVEL,
                           Z_DEFAULT_STRATEGY, ZLIB_VERSION, (int)sizeof(z_stream));
    assert(rcd == Z_OK);
    if (rcd != Z_OK) rc = RC::ERR;

    rcd = deflateSetHeader(&strm, &head);
    assert(rcd == Z_OK);
    if (rcd != Z_OK) rc = RC::ERR;

    while (strm.avail_in != 0)
    {
        int res = deflate(&strm, Z_NO_FLUSH);
        assert(res == Z_OK);
        if (rcd != Z_OK) rc = RC::ERR;
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
    if (deflate_res != Z_STREAM_END) rc = RC::ERR;

    to->insert(to->end(), chunk, chunk+CHUNK_SIZE-strm.avail_out);
    deflateEnd(&strm);
    return rc;
}

RC gzipit(string *from, vector<char> *to)
{
    return compress_memory(&(*from)[0], from->length(), to);
}

RC decompress_memory(char *in, size_t len, std::vector<char> *to)
{
    RC rc = RC::OK;
    char chunk[CHUNK_SIZE];

    z_stream strm;
    strm.zalloc = 0;
    strm.zfree = 0;
    strm.next_in = (unsigned char*)in;
    strm.avail_in = len;
    strm.next_out = (unsigned char*)chunk;
    strm.avail_out = CHUNK_SIZE;

    int rci = inflateInit2(&strm, MAX_WBITS + 16);
    if (rci != Z_OK) {
        return RC::ERR;
    }

    do {
        strm.avail_out = CHUNK_SIZE;
        strm.next_out = (unsigned char*)chunk;
        rci = inflate(&strm, Z_NO_FLUSH);
        if (rci == Z_STREAM_ERROR) rc = RC::ERR;
        size_t have = CHUNK_SIZE-strm.avail_out;
        to->insert(to->end(), chunk, chunk+have);
    } while (strm.avail_out == 0);

    //assert(rci == Z_STREAM_END);
    inflateEnd(&strm);
    return rc;
}

RC gunzipit(vector<char> *from, vector<char> *to)
{
    return decompress_memory(&(*from)[0], from->size(), to);
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

void printContents(std::map<Path*,FileStat> &contents)
{
    for (auto& p : contents) {
        printf("%s\n", p.first->c_str());
    }
}

bool digitsOnly(char *p, size_t len, string *s) {
    while (len-- > 0) {
        char c = *p++;
        if (!c) return false;
        if (!isdigit(c)) return false;
        s->push_back(c);
    }
    return true;
}

bool digitsDotsAndMinusOnly(char *p, size_t len, string *s) {
    while (len-- > 0) {
        char c = *p++;
        if (!c) return false;
        if (!isdigit(c) &&
            c != '.' &&
            c != '-') return false;
        s->push_back(c);
    }
    return true;
}

bool hexDigitsOnly(char *p, size_t len, string *s) {
    while (len-- > 0) {
        char c = *p++;
        if (!c) return false;
        bool is_hex = isdigit(c) ||
            (c >= 'A' && c <= 'F') ||
            (c >= 'a' && c <= 'f');
        if (!is_hex) return false;
        s->push_back(c);
    }
    return true;
}

string timeToString(uint64_t t)
{
    char buf[256];
    memset(buf, 0, sizeof(buf));
    time_t pp = t/(1000*1000*1000);
#ifdef PLATFORM_WINAPI
    struct tm *tid = localtime(&pp);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tid);
#else
    struct tm tid;
    localtime_r(&pp, &tid);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tid);
#endif
    return buf;
}

string timeToString(time_t pp)
{
    char buf[256];
    memset(buf, 0, sizeof(buf));
#ifdef PLATFORM_WINAPI
    struct tm *tid = localtime(&pp);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", tid);
#else
    struct tm tid;
    localtime_r(&pp, &tid);
    strftime(buf, sizeof(buf), "%Y-%m-%d %H:%M:%S", &tid);
#endif
    return buf;
}

RC parseDateTime(string dt, time_t *out)
{
    struct tm tp {};
    // 2019-03-20 15:32:12
    int n = sscanf(dt.c_str(), "%d-%d-%d %d:%d:%d",
                   &tp.tm_year, &tp.tm_mon, &tp.tm_mday,
                   &tp.tm_hour, &tp.tm_min, &tp.tm_sec);
    if (n<3) {
        return RC::ERR;
    }
    tp.tm_year -= 1900;
    tp.tm_mon -= 1;

    *out = mktime(&tp);
    return RC::OK;
}

RC parseYYYYMMDDhhmmss(string dt, struct tm *tp)
{
    // 2019-03-20 15:32:12
    int n = sscanf(dt.c_str(), "%04d%02d%02d%02d%02d%02d",
                   &tp->tm_year, &tp->tm_mon, &tp->tm_mday,
                   &tp->tm_hour, &tp->tm_min, &tp->tm_sec);
    if (n<3) {
        return RC::ERR;
    }
    tp->tm_year -= 1900;
    tp->tm_mon -= 1;

    return RC::OK;
}

RC parseDateTimeUTCNanos(string dt, time_t *tv_sec, long *tv_nsec)
{
    struct tm tp {};
    int nanos {};

    // 2018-12-02T14:38:53.000000Z
    int n = sscanf(dt.c_str(), "%d-%d-%dT%d:%d:%d.%d",
                   &tp.tm_year, &tp.tm_mon, &tp.tm_mday,
                   &tp.tm_hour, &tp.tm_min, &tp.tm_sec,
                   &nanos);
    if (n<6) {
        return RC::ERR;
    }
    tp.tm_year -= 1900;
    tp.tm_mon -= 1;

    // Need to adjust for current timezone since input is UTC.
    *tv_sec = mktime(&tp) - timezone;
    if (*tv_sec == -1)
    {
        // Oups, not a valid date!
        return RC::ERR;
    }
    *tv_nsec = nanos;
    return RC::OK;
}

bool startsWith(std::string s, std::string prefix)
{
    if (prefix.length() == 0) return true;
    if (s.length() < prefix.length()) return false;
    return !strncmp(s.c_str(), prefix.c_str(), prefix.length());
}

long upToNearestMicros(long nsec)
{
    long us = nsec / 1000;
    long ns = us*1000;
    return ns;
    // nsec was an even multiple of micro seconds.
    //if (ns == nsec) return ns;
    // nsec was truncated down. Now bump to the next
    // micro second.
    //return ns+1000;
}

size_t upToBlockSize(size_t s)
{
    size_t m = s % 512;
    if (m == 0) return s;
    s = s + (512-m);
    assert((s % 512) == 0);
    return s;
}

size_t downToBlockSize(size_t s)
{
    size_t m = s % 512;
    if (m == 0) return s;
    s = s -m;
    assert((s % 512) == 0);
    return s;
}

size_t roundToThousandMultiple(size_t from)
{
    size_t modv = 0;
    size_t to = from;
    if (from > 1000*1000)
    {
        modv = 1000*1000;
    }
    else if (from > 100*1000)
    {
        modv = 100*1000;
    }
    else if (from > 10*1000)
    {
        modv = 10*1000;
    }
    else
    {
        modv = 1000;
    }
    size_t mod = from % modv;
    // Round up to nearest 1K 10K 100K or 1M boundary.
    if (mod != 0)
    {
        to = from + (modv-mod);
        mod = to % modv;
        assert(mod == 0);
    }
    return to;
}

string makeSafeDirectory(string &s)
{
    string r;
    char buf[4];

    for (unsigned char c : s)
    {
        if (c == '/')
        {
            r.append("_", 1);
        }
        else if (c == ' ')
        {
            r.append("~", 1);
        }
        // Avoid NTFS forbidden chars: * . " / \ [ ] : ; | ,
        // So that a beak archive can be stored on Windows, even though
        // It might have problems with being extracted exactly the same.
        else if (c < 32 ||
                 c == 127 ||
                 c == '<' ||
                 c == '>' ||
                 c == ':' ||
                 c == '"' ||
                 c == '\\' ||
                 c == '|' ||
                 c == '?' ||
                 c == '*')
        {
            memset(buf, 0, 4);
            snprintf(buf, 4, "%%%02X", ((unsigned int) c) & 255);
            r.append(buf);
        }
        else
        {
            // Hopefully nice and clean UTF8 gets stored here....
            // But that really depends on what is stored in the file system.
            r.append((char*)&c, 1);
        }
    }

    return r;
}

Path *makeSafePath(Path *p, bool original, bool hash_only)
{
    if (original)
    {
        assert(!hash_only);
        return p;
    }

    string org = p->str();
    string safe = makeSafeDirectory(org);

    // This number comes from storing rclone crypt archives on linux.
    if (safe.length() <= 143)
    {
        return Path::lookup(safe);
    }

    // Ouch, the safe dir cannot be stored.
    // Replace it with a hash prefixed with as much
    // as will fit from the safe path.
    string olen = to_string(org.length());

    SHA256_CTX sha256ctx;
    vector<char> sha256_hash;

    SHA256_Init(&sha256ctx);
    SHA256_Update(&sha256ctx, org.c_str(), org.length());
    sha256_hash.resize(SHA256_DIGEST_LENGTH);
    SHA256_Final((unsigned char*)&sha256_hash[0], &sha256ctx);
    string hash = toHex(sha256_hash)+"L"+olen;

    if (hash_only)
    {
        return Path::lookup(hash);
    }

    int len = 143-hash.length()-2;
    string left = safe.substr(0, len); // Pick starting chars
    string path = left+"_"+hash;
    return Path::lookup(path);
}

bool isDate(const char *y, const char *m, const char *d)
{
    assert(y != NULL);
    assert(m != NULL);
    assert(d != NULL);

    int year = atoi(y);
    int month = atoi(m);
    int day = atoi(d);

    if (year < 1900 || year > 2222) return false;
    if (month < 1 || month > 12) return false;
    if (day < 1 || day > 31) return false;
    return true;
}
