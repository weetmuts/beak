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

#include<openssl/sha.h>

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
    addVolumeHeader();
}

void TarFile::addEntryLast(TarEntry *entry) {
    if (entry->sb.st_mtim.tv_sec > mtim.tv_sec ||
        (entry->sb.st_mtim.tv_sec == mtim.tv_sec && entry->sb.st_mtim.tv_nsec > mtim.tv_nsec)) {
        memcpy(&mtim, &entry->sb.st_mtim, sizeof(mtim));
    }
    
    entry->tar_file = this;
    entry->tar_offset = tar_offset;
    contents[tar_offset] = entry;
    offsets.push_back(tar_offset);
    debug("    %s    Added %s at %zu with blocked size %zu\n", name.c_str(),
          entry->path.c_str(), tar_offset, entry->blocked_size);
    tar_offset += entry->blocked_size;
}

void TarFile::addEntryFirst(TarEntry *entry) {
    if (entry->sb.st_mtim.tv_sec > mtim.tv_sec ||
        (entry->sb.st_mtim.tv_sec == mtim.tv_sec && entry->sb.st_mtim.tv_nsec > mtim.tv_nsec)) {
        memcpy(&mtim, &entry->sb.st_mtim, sizeof(mtim));
    }
    
    entry->tar_file = this;
    map<size_t,TarEntry*> newc;
    vector<size_t> newo;
    size_t start = 0;

    //fprintf(stderr, "%s insert first entry size %ju\n", name.c_str(), entry->blocked_size);
    if (volume_header) {
        newc[0] = volume_header;
        newo.push_back(0);
        start = volume_header->blocked_size;
        newc[start] = entry;
        newo.push_back(start);
        entry->tar_offset = start;
    } else {
        newc[0] = entry;
        newo.push_back(0);
    }
    for (auto & a : contents) {
        if (a.second != volume_header) {
            size_t o = a.first+entry->blocked_size;
            newc[o] = a.second;
            newo.push_back(o);
            a.second->tar_offset = o;
            //fprintf(stderr, "%s Moving %s from %ju to offset %ju\n", name.c_str(), a.second->name.c_str(), a.first, o);
        } else {
            //fprintf(stderr, "%s Not moving %s from %ju\n", name.c_str(), a.second->name.c_str(), a.first);
        }
    }
    contents = newc;
    offsets = newo;

    debug("    %s    Added FIRST %s at %zu with blocked size %zu\n", name.c_str(),
          entry->path.c_str(), tar_offset, entry->blocked_size);
    tar_offset += entry->blocked_size;

    if (tar_contents == SINGLE_LARGE_FILE_TAR) {
        assert(hash == entry->tarpath_hash);
    }
}

void TarFile::addVolumeHeader() {
    struct stat sb;
    memset(&sb, 0, sizeof(sb));
    TarEntry *header = new TarEntry("", &sb, "", true);
    memcpy(header->tar->th_buf.name, "tarredfs", 9);
    header->name="tarredfs";
    header->tar->th_buf.typeflag = GNU_VOLHDR_TYPE;
    addEntryLast(header);
    volume_header = header;
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

void TarFile::calculateSHA256Hash() {
    unsigned char hash[SHA256_DIGEST_LENGTH];
    SHA256_CTX sha256;
    SHA256_Init(&sha256);

    // SHA256 all headers, including the first Volume label header with empty name and empty checksum.
    for (auto & a : contents) {
        size_t len = a.second->header_size;
        assert(len<=512*5);
        char buf[len];
        TarEntry *te = a.second;
        te->copy(buf,len,a.first);
        // Update the hash with the exact header bits.
        SHA256_Update(&sha256, buf, len);

        // Update the hash with seconds and nanoseconds.
        char secs_and_nanos[32];
        memset(secs_and_nanos, 0, sizeof(secs_and_nanos));
        snprintf(secs_and_nanos, 32, "%ju.%ju", te->sb.st_mtim.tv_sec, te->sb.st_mtim.tv_nsec);
        SHA256_Update(&sha256, secs_and_nanos, strlen(secs_and_nanos));
    }
    SHA256_Final(hash, &sha256);
    // Copy the binary hash into the volume header name.
    memcpy(volume_header->tar->th_buf.name+9, hash, SHA256_DIGEST_LENGTH);
    // Calculate the tar checksum for the volume header.
    th_finish(volume_header->tar);
}


/*
Katrin Petterson

Eugeniavägen 23
plan 12, gå mot de gula golven
8.15
9.30 - 10

*/
