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

#ifndef UTIL_H
#define UTIL_H

#include"always.h"
#include"configuration.h"

#include<deque>
#include<limits>
#include<locale>
#include<memory.h>
#include<stddef.h>
#include<string>
#include<sys/types.h>
#include<sys/stat.h>
#include<vector>

void strprintf(std::string &s, const char* fmt, ...);

bool greaterThan(struct timespec *a, struct timespec *b);

// If s is not a multiple of 512 byte blocks, then round s up.
size_t upToBlockSize(size_t s);
// If s is not a multiple of 512 byte blocks, then round s down.
size_t downToBlockSize(size_t s);
// Size dependent rounding.
size_t roundToThousandMultiple(size_t s);
std::string humanReadable(size_t s);
std::string humanReadableTwoDecimals(size_t s);
std::string humanReadableTimeTwoDecimals(uint64_t micros);
std::string humanReadableTime(int seconds, bool show_seconds);
std::string toHex(size_t value, size_t max_value);
size_t roundoffHumanReadable(size_t s);
RC parseHumanReadable(std::string s, size_t *out);
std::string keepDigits(std::string &s);
bool parseTimeZoneOffset(std::string o, time_t *out);
RC parseDateTime(std::string dt, time_t *tv_sec);
RC parseDateTimeUTCNanos(std::string dt, time_t *tv_sec, long *tv_nsec);
bool parseLengthOfTime(std::string s, time_t *out);
std::string getLengthOfTime(time_t t);
time_t getTimeZoneOffset();
std::string getTimeZoneOffsetAsString(time_t t);
std::string timeToString(uint64_t t);
std::string timeToString(time_t t);
size_t basepos(std::string& s);
std::wstring to_wstring(std::string const& s);
std::string wto_string(std::wstring const& s);
std::string tolowercase(std::string const& s);
std::locale const *getLocale();
uint32_t hashString(std::string a);
void eraseArg(int i, int *argc, char **argv);
// Eat characters from the vector v, iterating using i, until the end char c is found.
// If end char == -1, then do not expect any end char, get all until eof.
// If the end char is not found, return error.
// If the maximum length is reached without finding the end char, return error.
std::string eatTo(std::vector<char> &v, std::vector<char>::iterator &i, int c, size_t max, bool *eof, bool *err);
// Eat whitespace (space and tab, not end of lines).
void eatWhitespace(std::vector<char> &v, std::vector<char>::iterator &i, bool *eof);
// First eat whitespace, then start eating until c is found or eof. The found string is trimmed from beginning and ending whitespace.
std::string eatToSkipWhitespace(std::vector<char> &v, std::vector<char>::iterator &i, int c, size_t max, bool *eof, bool *err);
// Remove leading and trailing white space
void trimWhitespace(std::string *s);
// Translate binary buffer with printable strings to ascii
// with non-printabled escaped as such: \xC0 \xFF \xEE
std::string toHexAndText(const char *b, size_t len, int line_length);
std::string toHexAndText(std::vector<char> &b, int line_length);
std::string toHex(const char *b, size_t len);
std::string toHex(std::vector<char> &b);
void hex2bin(std::string s, std::vector<char> *target);
void fixEndian(uint64_t *t);
bool isInTheFuture(const struct timespec *tm);
std::string timeAgo(const struct timespec *tm);

// Seconds since 1970-01-01 UTC
uint64_t clockGetUnixTimeSeconds();
// Nanoseconds since 1970-01-01 UTC
uint64_t clockGetUnixTimeNanoSeconds();
// Microseconds since the computer was started.
uint64_t clockGetTimeMicroSeconds();
void captureStartTime();
RC gzipit(std::string *from, std::vector<char> *to);
RC gunzipit(std::vector<char> *from, std::vector<char> *to);
std::string randomUpperCaseCharacterString(int len);

#define lookupKeyword(key_in,Type,TypeNames,key_out,ok) \
{ ok = false; \
    for (unsigned int i=0; i<(sizeof(TypeNames)/sizeof(char*)); ++i) \
    {  \
        if (key_in == TypeNames[i]) { key_out = (Type)i; ok = true; } \
    } \
} \


// Convert a deep path like alfa/beta/gamma to a single directory alfa%2Fbeta%2Fgamma_HASH
Path *makeSafePath(Path *p, bool original, bool hash_only);

void printContents(std::map<Path*,FileStat> &contents);

// Extract the leading digits from buf and store into s.
bool digitsOnly(char *buf, size_t len, std::string *s);

// Extract the leading hex digits from buf and store into s.
bool hexDigitsOnly(char *buf, size_t len, std::string *s);

bool startsWith(std::string s, std::string prefix);

long upToNearestMicros(long nsec);

// Check that y is four digits, m is two digits, 1-12, and d is 1-31 or -30 or -28 or -29.
bool isDate(const char *y, const char *m, const char *d);

#endif
