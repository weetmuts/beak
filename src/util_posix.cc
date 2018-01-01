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

#include "util.h"

#include <stddef.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <linux/memfd.h>
#include <sys/syscall.h>
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
#include <utility>
#include <unistd.h>
#include <fcntl.h>
#include <zlib.h>

#include "log.h"

#define KB 1024ul

using namespace std;

static ComponentId UTIL = registerLogComponent("util");

extern struct timespec start_time_; // Inside util.cc

// Seconds since 1970-01-01 Z timezone.
uint64_t clockGetUnixTime()
{
    return time(NULL);
}

// Return microseconds
uint64_t clockGetTime()
{
    timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t) ts.tv_sec * 1000000LL + (uint64_t) ts.tv_nsec / 1000LL;
}

void captureStartTime() {
    clock_gettime(CLOCK_REALTIME, &start_time_);
}

int gzipit(string *from, vector<unsigned char> *to)
{
    int fd = syscall(SYS_memfd_create, "tobezipped", 0);
    int fdd = dup(fd);
    gzFile gzf = gzdopen(fdd, "w");
    gzwrite(gzf, from->c_str(), from->length());
    gzclose(gzf);

    size_t len = lseek(fd, 0, SEEK_END);
    //assert(from->length()  == 0 || len < 10+8+2*from->length()); // The gzip header is 10, crc32+isize is 8
    lseek(fd, 0, SEEK_SET);

    to->resize(len);
    size_t s = read(fd, &(*to)[0], len);
    assert(s == len);
    close(fd);

    return OK;
}

int gunzipit(vector<char> *from, vector<char> *to)
{
    int fd = syscall(SYS_memfd_create, "tobunzipped", 0);
    size_t s = write(fd, &(*from)[0], from->size());
    assert(s == from->size());
    lseek(fd, 0, SEEK_SET);
    int fdd = dup(fd);
    char buf[4096];
    int n = 0;
    gzFile gzf = gzdopen(fdd, "r");
    do {
        n = gzread(gzf, buf, sizeof(buf));
        if (n == -1) break;
        to->insert(to->end(), buf, buf+n);
    } while (n==sizeof(buf));
    gzclose(gzf);
    close(fd);

    return OK;
}

/*
  const unsigned char *cstr = reinterpret_cast<const unsigned char*>(from->c_str());
  size_t cstrlen = strlen(from->c_str());

  unsigned long bufsize = compressBound(cstrlen);
  unsigned char *buf = new unsigned char[bufsize];
  int rc = compress2(buf, &bufsize, cstr, cstrlen,1);

  printf("%d >%*s<\n", (int)cstrlen, (int)cstrlen, cstr);

  assert(rc == Z_OK);

  struct GZipHeader header;

  assert(sizeof(GZipHeader)==10);
  memset(&header, 0, sizeof(GZipHeader));
  header.magic_header[0] = 0x1f;
  header.magic_header[1] = 0x8b;
  header.compression_method = 8;
  header.os_type = 3;

  to->clear();
  uint32_t isize  = (uint32_t)cstrlen;
  to->resize(bufsize+sizeof(GZipHeader)+sizeof(isize));

  memcpy(&(*to)[0],&header, sizeof(GZipHeader));
  memcpy(&(*to)[0]+sizeof(GZipHeader),buf, bufsize);

  toLittleEndian(&isize);
  memcpy(&(*to)[0]+sizeof(GZipHeader)+bufsize, &isize, sizeof(isize));

  delete [] buf;
  return OK;
}

*/
