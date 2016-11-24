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

#include"log.h"
#include"tarentry.h"

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
#include<vector>

TarFile::TarFile(TarContents tc, int n, bool dirs) {
    tar_contents = tc;
    memset(&mtim, 0, sizeof(mtim));        
    char c;
    assert((dirs && tar_contents == DIR_TAR) || (!dirs && tar_contents != DIR_TAR));
    if (tar_contents == DIR_TAR) {
        // Use taz0123.tar instead of tar0123.tar to have
        // the directory tars at the end in the alphabetic list.
        c = 'z';
    } else if (tar_contents == SMALL_FILES_TAR) {
        c = 'r';
    } else if (tar_contents == MEDIUM_FILES_TAR) {
        c = 'm';
    } else {
        // For large files, the n is a hash of the file name.
        // Normally a single tar stores a single file.
        // But, more than one large file might end up in the same tar, if the 32 bit hash collide,
        // that is ok.
        c = 'l';
        hash = n;
    }
    char buffer[256];
    snprintf(buffer, sizeof(buffer), "ta%c%08x.tar", c, n);
    name = buffer;
}

void TarFile::addEntry(TarEntry *entry) {
    if (entry->sb.st_mtim.tv_sec > mtim.tv_sec ||
        (entry->sb.st_mtim.tv_sec == mtim.tv_sec && entry->sb.st_mtim.tv_nsec > mtim.tv_nsec)) {
        memcpy(&mtim, &entry->sb.st_mtim, sizeof(mtim));
    }
    
    entry->tar_file = this;
    contents[tar_offset] = entry;
    offsets.push_back(tar_offset);
    debug("    %s    Added %s at %zu with blocked size %zu\n", name.c_str(),
          entry->path.c_str(), tar_offset, entry->blocked_size);
    tar_offset += entry->blocked_size;

    if (tar_contents == SINGLE_LARGE_FILE_TAR) {
        assert(hash == entry->tarpath_hash);
    }
}

pair<TarEntry*,size_t> TarFile::findTarEntry(size_t offset) {
    if (offset > size) {
        return pair<TarEntry*,size_t>(NULL,0);
    }
    debug("Looking for offset %zu\n", offset);
    size_t o = 0;
    
    vector<size_t>::iterator i =
        lower_bound(offsets.begin(),offsets.end(),offset,less_equal<size_t>());
    
    if (i == offsets.end()) {
        o = *offsets.rbegin();
    } else {
        i--;
        o = *i;
    }
    TarEntry *te = contents[o];
    
    debug("Found it %s\n", te->path.c_str());
    return pair<TarEntry*,size_t>(te,o);
}

