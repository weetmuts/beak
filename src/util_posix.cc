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

#include <grp.h>
#include <pwd.h>
#include <stddef.h>
#include <string.h>
#include <sys/stat.h>
#include <linux/memfd.h>
#include <linux/kdev_t.h>
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
#include <sstream>
#include <utility>
#include <zlib.h>

#include "log.h"

#define KB 1024ul

using namespace std;

static ComponentId UTIL = registerLogComponent("util");
static ComponentId TMP = registerLogComponent("tmp");

extern struct timespec start_time_; // Inside util.cc

string ownergroupString(uid_t uid, gid_t gid)
{
    struct passwd pwd;
    struct passwd *result;
    char buf[16000];
    stringstream ss;
    
    int rc = getpwuid_r(uid, &pwd, buf, sizeof(buf), &result);
    if (result == NULL)
    {
        if (rc == 0)
        {
            ss << uid;
        }
        else
        {
            errno = rc;
            error(UTIL, "Internal error getpwuid_r %d", errno);
        }
    }
    else
    {
        ss << pwd.pw_name;
    }
    ss << "/";
    
    struct group grp;
    struct group *gresult;
    
    rc = getgrgid_r(gid, &grp, buf, sizeof(buf), &gresult);
    if (gresult == NULL)
    {
        if (rc == 0)
        {
            ss << gid;
        }
        else
        {
            errno = rc;
            error(UTIL, "Internal error getgrgid_r %d", errno);
        }
    }
    else
    {
        ss << grp.gr_name;
    }
    
    return ss.str();
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
    read(fd, &(*to)[0], len);
    close(fd);

    return OK;
}

int gunzipit(vector<char> *from, vector<char> *to)
{
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

string permissionString(mode_t m)
{
    stringstream ss;
    
    if (S_ISDIR(m))
        ss << "d";
    else if (S_ISLNK(m))
        ss << "l";
    else if (S_ISCHR(m))
        ss << "c";
    else if (S_ISBLK(m))
        ss << "b";
    else if (S_ISFIFO(m))
        ss << "p";
    else if (S_ISSOCK(m))
        ss << "s";
    else
    {
        assert(S_ISREG(m));
        ss << "-";
    }
    if (m & S_IRUSR)
        ss << "r";
    else
        ss << "-";
    if (m & S_IWUSR)
        ss << "w";
    else
        ss << "-";
    if (m & S_ISUID) {
        ss << "s";
    } else {                        
        if (m & S_IXUSR)
            ss << "x";
        else
            ss << "-";
    }
    if (m & S_IRGRP)
        ss << "r";
    else
        ss << "-";
    if (m & S_IWGRP)
        ss << "w";
    else
        ss << "-";
    if (m & S_ISGID) {
        ss << "s";
    } else {                        
        if (m & S_IXGRP)
            ss << "x";
        else
            ss << "-";
    }
    if (m & S_IROTH)
        ss << "r";
    else
        ss << "-";
    if (m & S_IWOTH)
        ss << "w";
    else
        ss << "-";
    if (m & S_ISVTX) {
        ss << "t";
    } else {
        if (m & S_IXOTH)
            ss << "x";
        else
            ss << "-";
    }
    return ss.str();
}

mode_t stringToPermission(string s)
{
    mode_t rc = 0;
    
    if (s[0] == 'd')
        rc |= S_IFDIR;
    else if (s[0] == 'l')
        rc |= S_IFLNK;
    else if (s[0] == 'c')
        rc |= S_IFCHR;
    else if (s[0] == 'b')
        rc |= S_IFBLK;
    else if (s[0] == 'p')
        rc |= S_IFIFO;
    else if (s[0] == 's')
        rc |= S_IFSOCK;
    else if (s[0] == '-')
        rc |= S_IFREG;
    else
        goto err;
    
    if (s[1] == 'r')
        rc |= S_IRUSR;
    else if (s[1] != '-')
        goto err;
    if (s[2] == 'w')
        rc |= S_IWUSR;
    else if (s[2] != '-')
        goto err;

    if (s[3] == 'x')
        rc |= S_IXUSR;
    else
    if (s[3] == 's') {
        rc |= S_IXUSR;
        rc |= S_ISUID;
    } 
    else if (s[3] != '-')
        goto err;
    
    if (s[4] == 'r')
        rc |= S_IRGRP;
    else if (s[4] != '-')
        goto err;
    if (s[5] == 'w')
        rc |= S_IWGRP;
    else if (s[5] != '-')
        goto err;
    if (s[6] == 'x')
        rc |= S_IXGRP;
    else
    if (s[6] == 's') {
        rc |= S_IXGRP;
        rc |= S_ISGID;
    } 
    else if (s[6] != '-')
        goto err;
    
    if (s[7] == 'r')
        rc |= S_IROTH;
    else if (s[7] != '-')
        goto err;
    if (s[8] == 'w')
        rc |= S_IWOTH;
    else if (s[8] != '-')
        goto err;
    if (s[9] == 'x')
        rc |= S_IXOTH;
    else
    if (s[9] == 't') {
        rc |= S_IXOTH;
        rc |= S_ISVTX;
    }
    else if (s[9] != '-')
        goto err;
    
    return rc;
    
    err:

    return 0;
}


dev_t MakeDev(int maj, int min)
{
    return MKDEV(maj, min);
}

int MajorDev(dev_t d)
{
    return MAJOR(d);
}

int MinorDev(dev_t d)
{
    return MINOR(d);
}
