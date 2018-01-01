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

std::locale const user_locale(LOCALE_STRING);

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
    std::vector<char> sha256_hash_;
    std::vector<char> pool_;

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
