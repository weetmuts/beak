/*  
    Copyright (C) 2016 Fredrik Öhrström

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

#include<assert.h>

#include"tarentry.h"

#include"log.h"
#include"util.h"

#include<errno.h>

#include<fcntl.h>
#include<ftw.h>
#include<fuse.h>

#include<limits.h>

#include<pthread.h>

#include<regex.h>

#include<stddef.h>
#include<stdint.h>
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<syslog.h>

#include<time.h>
#include<sys/timeb.h>

#include<unistd.h>

#include<algorithm>
#include<functional>
#include<map>
#include<set>
#include<string>
#include<sstream>
#include<vector>

#include<iostream>

using namespace std;

bool sanityCheck(const char *x, const char *y);

TarEntry::TarEntry(string p, const struct stat *b, string root_dir, bool header) : path(p), sb(*b) {
    dir_tar_in_use = false;
    children_size = 0;
    chunked_size = 0;
    parent = NULL;
    is_chunk_point = false;
    num_long_path_blocks = 0;
    num_long_link_blocks = 0;
    num_header_blocks = 1;
    assert(path.length() == 0 || path[0] == '/' || header);
    depth = std::count(path.begin(), path.end(), '/');
    abspath = root_dir+path;
    if (path.length() > 0) {
        tarpath = path.substr(1);
    }
    
    {
        size_t x = abspath.find_last_of('/');
        if (x>=0) {
            name = abspath.substr(x+1);
        } else {
            name = abspath;
        }
    }

    if(S_ISDIR(b->st_mode)) {
        path += '/';
    }
    
    // Allocate the TAR object here. It is kept alive forever, until unmount.
    tar_fdopen(&tar, 0, "", NULL, 0, O_CREAT, 0);
    tar->options |= TAR_GNU;
    int rc = th_set_from_stat(tar, &sb);
    if (rc & TH_COULD_NOT_SET_MTIME) {
        error("Could not set last modified time in tar for file: %s\n", abspath.c_str());
    }    
    if (rc & TH_COULD_NOT_SET_SIZE) {
        error("Could not set size in tar for file: %s\n", abspath.c_str());
    }    
    th_set_path(tar, tarpath.c_str());
    
    if (TH_ISSYM(tar))
    {
        char destination[PATH_MAX];
        ssize_t l = readlink(abspath.c_str(), destination, sizeof(destination));
        if (l < 0) {
            error("Could not read link >%s< in underlying filesystem err %d\n", abspath.c_str(), errno);
            
            return;
        }
        if (l >= PATH_MAX) {
            l = PATH_MAX - 1;
        }
        destination[l] = '\0';
        link = destination;
        th_set_link(tar, destination);
        debug("Found link from %s to %s\n", abspath.c_str(), destination);
        
        if (tar->th_buf.gnu_longlink != NULL) {
            // We needed to use gnu long links, aka an extra header block 
            // plus at least one block for the file name. A link target path longer than 512
            // bytes will need a third block etc
            num_long_link_blocks = 2 + link.length()/T_BLOCKSIZE; 
            num_header_blocks += num_long_link_blocks;
            debug("Added %ju blocks for long link header for %s\n", num_long_link_blocks, destination);            
        }            
    }
    
    th_finish(tar);
    
    if (tar->th_buf.gnu_longname != NULL) {
        // We needed to use gnu long names, aka an extra header block 
        // plus at least one block for the file name. A path longer than 512
        // bytes will need a third block etc
        num_long_path_blocks = 2 + tarpath.length()/T_BLOCKSIZE; 
        num_header_blocks += num_long_path_blocks;
        debug("Added %ju blocks for long path header for %s\n", num_long_path_blocks, path.c_str());            
    }
    
    header_size = size = num_header_blocks*T_BLOCKSIZE;
    if (!TH_ISDIR(tar) && !TH_ISSYM(tar) && !TH_ISFIFO(tar) && !TH_ISCHR(tar) && !TH_ISBLK(tar)) {
        // Directories, symbolic links and fifos have no content in the tar.
        // Only add the size from actual files with content here.
        size += sb.st_size;
    }
    disk_size = sb.st_size;
    // Round size to nearest 512 byte boundary
    children_size = blocked_size = (size%T_BLOCKSIZE==0)?size:(size+T_BLOCKSIZE-(size%T_BLOCKSIZE));
    
    assert(header_size >= T_BLOCKSIZE &&  size >= header_size && blocked_size >= size);
    assert((!TH_ISDIR(tar) && !TH_ISSYM(tar) && !TH_ISFIFO(tar) && !TH_ISCHR(tar) && !TH_ISBLK(tar)) || size == header_size);
    assert(TH_ISDIR(tar) || TH_ISSYM(tar) || TH_ISFIFO(tar) || TH_ISCHR(tar) || TH_ISBLK(tar) || th_get_size(tar) == (size_t)sb.st_size);

    string null("\0",1);
    if (!header) {
        stringstream ss;
        ss << permissionString(sb.st_mode) << null << sb.st_uid << "/" << sb.st_gid;
        tv_line_left = ss.str();
        
        ss.str("");
        if (TH_ISSYM(tar)) {
            ss << 0;
        } else {
            ss << sb.st_size;
        }
        tv_line_size = ss.str();
        
        ss.str("");
        char datetime[17];
        memset(datetime, 0, sizeof(datetime));
        strftime(datetime, 17, "%Y-%m-%d %H:%M.%S", localtime(&sb.st_mtime));
        ss << datetime;
        ss << null;
        char secs_and_nanos[32];
        memset(secs_and_nanos, 0, sizeof(secs_and_nanos));
        snprintf(secs_and_nanos, 32, "%ju.%ju", sb.st_mtim.tv_sec, sb.st_mtim.tv_nsec);
        ss << secs_and_nanos;
        tv_line_right = ss.str();
    }
    debug("Entry %s added\n", path.c_str());
}
    
void TarEntry::removePrefix(size_t len) {        
    tarpath = path.substr(len);
    th_set_path(tar, tarpath.c_str());
    th_finish(tar);
    tarpath_hash = hashString(tarpath);
    assert(sanityCheck(tarpath.c_str(), th_get_pathname(tar)));
}

size_t TarEntry::copy(char *buf, size_t size, size_t from) {
    size_t copied = 0;
    debug("Copying max %zu from %zu\n", size, from);
    if (size > 0 && from < header_size) {
        char tmp[header_size];
        memset(tmp, 0, header_size);
        int p = 0;
        
        if (num_long_link_blocks > 0) {
            char tmp_type = tar->th_buf.typeflag;
            size_t tmp_size = th_get_size(tar);
            
            // Re-use the proper header! Just change the type and size!
            tar->th_buf.typeflag = GNU_LONGLINK_TYPE;
            assert(link.length() == strlen(tar->th_buf.gnu_longlink));
            th_set_size(tar, link.length());
            th_finish(tar);
            
            memcpy(tmp+p, &tar->th_buf, T_BLOCKSIZE);
            
            // Reset the header!
            tar->th_buf.typeflag = tmp_type;
            th_set_size(tar, tmp_size);                
            th_finish(tar);
            
            memcpy(tmp+p+T_BLOCKSIZE, link.c_str(), link.length());
            p += num_long_link_blocks*T_BLOCKSIZE;
            debug("Wrote long link header for %s\n", link.c_str());
        }
        
        if (num_long_path_blocks > 0) {
            char tmp_type = tar->th_buf.typeflag;
            size_t tmp_size = th_get_size(tar);
            
            // Re-use the proper header! Just change the type and size!
            tar->th_buf.typeflag = GNU_LONGNAME_TYPE;
            // Why can gnu_longname suddenly by 0?
            // Its because we remove the prefix of the path when finishing up the tars!
            // It was long, but now its short. Alas, we do not reshuffle the offset in the tar, yet.
            // So store a short name in the longname. 
            assert(tar->th_buf.gnu_longname == 0 || tarpath.length() == strlen(tar->th_buf.gnu_longname));
            th_set_size(tar, tarpath.length());
            th_finish(tar);
            
            memcpy(tmp+p, &tar->th_buf, T_BLOCKSIZE);
            
            // Reset the header!
            tar->th_buf.typeflag = tmp_type;
            th_set_size(tar, tmp_size);                
            th_finish(tar);
            
            memcpy(tmp+p+T_BLOCKSIZE, tarpath.c_str(), tarpath.length());
            p += num_long_path_blocks*T_BLOCKSIZE;
            debug("Wrote long path header for %s\n", path.c_str());
        }
        
        memcpy(tmp+p, &tar->th_buf, T_BLOCKSIZE);
        
        // Copy the header out
        size_t len = header_size-from;
        if (len > size) {
            len = size;
        }
        debug("    header out from %s %zu size=%zu\n", path.c_str(), from, len);            
        assert(from+len <= header_size);
        memcpy(buf, tmp+from, len);
        size -= len;
        buf += len;
        copied += len;
        from += len;
    }
    
    if (size > 0 && copied < blocked_size && from >= header_size && from < blocked_size) {
        if (virtual_file) {
            debug("reading from tar_list size=%ju copied=%ju blocked_size=%ju from=%ju header_size=%ju\n",
                  size, copied, blocked_size, from, header_size);
            size_t off = from - header_size;
            size_t len = content.length()-off;
            if (len > size) {
                len = size;
            }
            memcpy(buf, content.c_str()+off, len);
            size -= len;
            buf += len;
            copied += len;
        } else {
            debug("reading from file size=%ju copied=%ju blocked_size=%ju from=%ju header_size=%ju\n",
                  size, copied, blocked_size, from, header_size);
            // Read from file
            int fd = open(abspath.c_str(), O_RDONLY);
            if (fd==-1) {
                failure("Could not open file >%s< in underlying filesystem err %d", path.c_str(), errno);
                return 0;
            }
            debug("    contents out from %s %zu size=%zu\n", path.c_str(), from-header_size, size);
            ssize_t l = pread(fd, buf, size, from-header_size);
            if (l==-1) {
                failure("Could not read from file >%s< in underlying filesystem err %d", path.c_str(), errno);
                return 0;
            }
            close(fd);
            size -= l;
            buf += l;
            copied += l;
        }
    }
    // Round up to next 512 byte boundary.
    size_t remainder = (copied%T_BLOCKSIZE == 0) ? 0 : T_BLOCKSIZE-copied%T_BLOCKSIZE;
    if (remainder > size) {
        remainder = size;
    }
    memset(buf, 0, remainder);
    copied += remainder;
    debug("Copied %zu bytes\n", copied);
    return copied;
}

const bool TarEntry::isDir() {
    return TH_ISDIR(tar);
}

bool sanityCheck(const char *x, const char *y) {            
    if (strcmp(x,y)) {
        if (x[0] == 0 && y[0] == '.' && y[1] == 0) {
            // OK
            return true;
        } else {
            // Something differs ok or not?
            size_t yl = strlen(y);
            if (x[0] == '/' && y[0] != '/') {
                // Skip initial root / that is never stored in tar.
                x++;
            }
            if (yl-1 == strlen(x) && y[yl-1] == '/' && x[yl-1] == 0) {
                // Skip final / after dirs in tar file
                yl--;
            }            
            if (strncmp(x,y,yl)) {
                error("Internal error, these should be equal!\n>%s<\n>%s<\nlen %zu\n ", x, y, yl);
                return false;
            }
        }
    }
    return true;
}

void TarEntry::setContent(string c) {
    content = c;
    virtual_file = true;
}
