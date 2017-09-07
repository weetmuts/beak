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
#include <zlib.h>

#include "log.h"

using namespace std;

static ComponentId UTIL = registerLogComponent("util");
static ComponentId TMP = registerLogComponent("tmp");

#define KB 1024ul

string ownergroupString(uid_t uid, gid_t gid)
{
    return "";
}

void captureStartTime() {
    //clock_gettime(CLOCK_REALTIME, &start_time_);
}

#pragma pack(push, 1)

struct GZipHeader {
  char magic_header[2]; // 0x1f 0x8b
  char compression_method; 
  // 0: store (copied)
  //  1: compress
  //  2: pack
  //  3: lzh
  //  4..7: reserved
  //  8: deflate
  char flags;
  // bit 0 set: file probably ascii text
  // bit 1 set: continuation of multi-part gzip file, part number present
  // bit 2 set: extra field present
  // bit 3 set: original file name present
  // bit 4 set: file comment present
  // bit 5 set: file is encrypted, encryption header present
  // bit 6,7:   reserved
  char mtim[4]; // file modification time in Unix format
  char extra_flags; // extra flags (depend on compression method)
  char os_type; // 3 = Unix
};

#pragma pack(pop)

int gzipit(string *from, vector<unsigned char> *to)
{
    /*
    int fd = syscall(SYS_memfd_create, "tobezipped", 0);
    int fdd = dup(fd);
    gzFile gzf = gzdopen(fdd, "w");
    gzwrite(gzf, from->c_str(), from->length());
    gzclose(gzf);
    
    size_t len = lseek(fd, 0, SEEK_END);
    //assert(from->length()  == 0 || len < 10+8+2*from->length()); // The gzip header is 10, crc32+isize is 8
    lseek(fd, 0, SEEK_SET);
    
    to->resize(len);
    read(fd, &(*to)[0], len);
    close(fd);
    */
    return OK;
}

int gunzipit(vector<char> *from, vector<char> *to)
{
    /*
    int fd = syscall(SYS_memfd_create, "tobunzipped", 0);
    write(fd, &(*from)[0], from->size());

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
    */
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




dev_t MakeDev(int maj, int min)
{
    return 0;
}

int MajorDev(dev_t d)
{
    return 0;
}

int MinorDev(dev_t d)
{
    return 0;
}
