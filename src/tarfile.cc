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

#include "tarfile.h"

#include <openssl/sha.h>
#include <string.h>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <functional>
#include <iterator>
#include <stdlib.h>

#include "log.h"
#include "tarentry.h"
#include "util.h"

using namespace std;

ComponentId TARFILE = registerLogComponent("tarfile");
ComponentId HASHING = registerLogComponent("hashing");

TarFile::TarFile(TarContents tc)
{
    size_ = 0;
    tar_contents_ = tc;
    memset(&mtim_, 0, sizeof(mtim_));
    // This is a temporary name! It will be overwritten when the hash is finalized!
    name_ = "";
    disk_update = NoUpdate;
    num_parts_ = 1;
}

void TarFile::addEntryLast(TarEntry *entry)
{
    entry->updateMtim(&mtim_);

    entry->registerTarFile(this, current_tar_offset_);
    contents_[current_tar_offset_] = entry;
    offsets.push_back(current_tar_offset_);
    debug(TARFILE, "    %s    Added %s at %zu\n", name_.c_str(),
          entry->path()->c_str(), current_tar_offset_);
    current_tar_offset_ += entry->blockedSize();
}

void TarFile::addEntryFirst(TarEntry *entry)
{
    entry->updateMtim(&mtim_);

    entry->registerTarFile(this, 0);
    map<size_t, TarEntry*> newc;
    vector<size_t> newo;

    newc[0] = entry;
    newo.push_back(0);
    entry->registerTarFile(this, 0);

    for (auto & a : contents_)
    {
        size_t o = a.first + entry->blockedSize();
        newc[o] = a.second;
        newo.push_back(o);
        a.second->registerTarFile(this, o);
    }
    contents_ = newc;
    offsets = newo;

    debug(TARFILE, "    %s    Added FIRST %s at %zu with blocked size %zu\n",
          name_.c_str(), entry->path()->c_str(), current_tar_offset_,
          entry->blockedSize());
    current_tar_offset_ += entry->blockedSize();
}

pair<TarEntry*, size_t> TarFile::findTarEntry(size_t offset)
{
    if (offset > size_)
    {
        return pair<TarEntry*, size_t>(NULL, 0);
    }
    debug(TARFILE, "Looking for offset %zu\n", offset);
    size_t o = 0;

    vector<size_t>::iterator i = lower_bound(offsets.begin(), offsets.end(),
                                             offset, less_equal<size_t>());

    if (i == offsets.end())
    {
        o = *offsets.rbegin();
    }
    else
    {
        i--;
        o = *i;
    }
    TarEntry *te = contents_[o];

    debug(TARFILE, "Found it %s\n", te->path()->c_str());
    return pair<TarEntry*, size_t>(te, o);
}

void TarFile::calculateHash()
{
    calculateSHA256Hash();
}

void TarFile::calculateHash(vector<pair<TarFile*,TarEntry*>> &tars, string &content)
{
    calculateSHA256Hash(tars, content);
}

vector<char> &TarFile::hash() {
    return sha256_hash_;
}

void TarFile::calculateSHA256Hash()
{
    SHA256_CTX sha256ctx;
    SHA256_Init(&sha256ctx);

    for (auto & a : contents_)
    {
        TarEntry *te = a.second;
        SHA256_Update(&sha256ctx, &te->hash()[0], te->hash().size());
    }
    sha256_hash_.resize(SHA256_DIGEST_LENGTH);
    SHA256_Final((unsigned char*)&sha256_hash_[0], &sha256ctx);
}

void TarFile::calculateSHA256Hash(vector<pair<TarFile*,TarEntry*>> &tars, string &content)
{
    SHA256_CTX sha256ctx;
    SHA256_Init(&sha256ctx);

    // SHA256 all other tar and gz file hashes! This is the hash of this state!
    for (auto & p : tars)
    {
        TarFile *tf = p.first;
        if (tf == this) continue;

        SHA256_Update(&sha256ctx, &tf->hash()[0], tf->hash().size());
    }

    // SHA256 the detailed file listing too!
    SHA256_Update(&sha256ctx, &content[0], content.length());

    sha256_hash_.resize(SHA256_DIGEST_LENGTH);
    SHA256_Final((unsigned char*)&sha256_hash_[0], &sha256ctx);
}

void TarFile::updateMtim(struct timespec *mtim) {
    if (isInTheFuture(&mtim_)) {
        fprintf(stderr, "Virtual tarfile %s has a future timestamp! Ignoring the timstamp.\n",
                "PATHHERE");
    } else {
        if (mtim_.tv_sec > mtim->tv_sec ||
            (mtim_.tv_sec == mtim->tv_sec && mtim_.tv_nsec > mtim->tv_nsec)) {
            memcpy(mtim, &mtim_, sizeof(*mtim));
        }
    }
}

TarFileName::TarFileName(TarFile *tf, uint partnr)
{
    type = tf->type();
    version = 1;
    sec = tf->mtim()->tv_sec;
    nsec = tf->mtim()->tv_nsec;
    size = tf->size(partnr);
    header_hash = toHex(tf->hash());
    part_nr = partnr;
    num_parts = tf->numParts();
}

bool TarFileName::isIndexFile(Path *p)
{
    // Example file name:
    // foo/bar/dir/z01_(001501080787).(579054757)_(0)_(3b5e4ec7fe38d0f9846947207a0ea44c)_(0).gz

    size_t len = p->name()->str().length();
    if (len < 20) return false;
    const char *s = p->name()->c_str();

    return 0==strncmp(s, "z01_", 3) && 0==strncmp(s+len-3, ".gz", 3);
}

bool TarFileName::parseFileName(string &name, string *dir)
{
    bool k;
    // Example file name:
    // foo/bar/dir/(l)01_(001501080787).(579054757)_(1119232)_(3b5e4ec7fe38d0f9846947207a0ea44c)_(0fe).(tar)

    if (name.size() == 0) return false;

    size_t p0 = name.rfind('/');
    if (p0 == string::npos) { p0=0; } else { p0++; }

    if (dir) {
        *dir = name.substr(0, p0);
    }
    k = typeFromChar(name[p0], &type);
    if (!k) return false;

    size_t p1 = name.find('_', p0); if (p1 == string::npos) return false;
    size_t p2 = name.find('.', p1+1); if (p2 == string::npos) return false;
    size_t p3 = name.find('_', p2+1); if (p3 == string::npos) return false;
    size_t p4 = name.find('_', p3+1); if (p4 == string::npos) return false;
    size_t p5 = name.find('_', p4+1); if (p5 == string::npos) return false;
    size_t p6 = name.find('.', p5+1); if (p6 == string::npos) return false;

    string versions;
    k = digitsOnly(&name[p0+1], p1-p0-1, &versions);
    if (!k) return false;
    version = atoi(versions.c_str());

    string secss;
    k = digitsOnly(&name[p1+1], p2-p1-1, &secss);
    if (!k) return false;
    sec = atol(secss.c_str());

    string nsecss;
    k = digitsOnly(&name[p2+1], p3-p2-1, &nsecss);
    if (!k) return false;
    nsec = atol(nsecss.c_str());

    string sizes;
    k = digitsOnly(&name[p3+1], p4-p3-1, &sizes);
    if (!k) return false;
    size = atol(sizes.c_str());

    k = hexDigitsOnly(&name[p4+1], p5-p4-1, &header_hash);
    if (!k) return false;

    string partnrs;
    k = hexDigitsOnly(&name[p5+1], p6-p5-1, &partnrs);
    if (!k) return false;
    part_nr = strtol(partnrs.c_str(), NULL, 16);

    string suffix = name.substr(p6+1);
    if (suffixtype(type) != suffix) {
        return false;
    }

    return true;
}

void TarFileName::writeTarFileNameIntoBuffer(char *buf, size_t buf_len, Path *dir)
{
    // dirprefix/(l)01_(001501080787).(579054757)_(1119232)_(3b5e4ec7fe38d0f9846947207a0ea44c)_(07).(tar)
    char sizes[32];
    memset(sizes, 0, sizeof(sizes));
    snprintf(sizes, 32, "%zu", size);

    char secs_and_nanos[32];
    memset(secs_and_nanos, 0, sizeof(secs_and_nanos));
    snprintf(secs_and_nanos, 32, "%012" PRINTF_TIME_T "u.%09lu", sec, nsec);

    string partnr = toHex(part_nr, num_parts);

    const char *suffix = suffixtype(type);

    if (dir == NULL) {
        snprintf(buf, buf_len, "%c01_%s_%s_%s_%s.%s",
                 TarFileName::chartype(type),
                 secs_and_nanos,
                 sizes,
                 header_hash.c_str(),
                 partnr.c_str(),
                 suffix);
    } else {
        snprintf(buf, buf_len, "%s/%c01_%s_%s_%s_%s.%s",
                 dir->c_str(),
                 TarFileName::chartype(type),
                 secs_and_nanos,
                 sizes,
                 header_hash.c_str(),
                 partnr.c_str(),
                 suffix);
    }

    //path_ = in_directory_->path()->appendName(Atom::lookup(name_));
}

string TarFileName::asStringWithDir(Path *dir) {
    char buf[1024];
    writeTarFileNameIntoBuffer(buf, sizeof(buf), dir);
    return buf;
}

Path *TarFileName::asPathWithDir(Path *dir)
{
    char buf[1024];
    writeTarFileNameIntoBuffer(buf, sizeof(buf), dir);
    return Path::lookup(buf);
}

size_t TarFile::copy(char *buf, size_t bufsiz, off_t offset, FileSystem *fs, uint partnr)
{
    size_t org_size = bufsiz;

    if (offset < 0) return 0;
    if ((size_t)offset >= size(partnr)) return 0;

    while (bufsiz>0) {
        pair<TarEntry*,size_t> r = findTarEntry(offset);
        TarEntry *te = r.first;
        size_t tar_offset = r.second;
        assert(te != NULL);
        size_t l =  te->copy(buf, bufsiz, offset - tar_offset, fs);
        debug(TARFILE, "copy size=%ju result=%ju\n", bufsiz, l);
        bufsiz -= l;
        buf += l;
        offset += l;
        if (l==0) break;
    }

    /*
    if (offset >= (ssize_t)(size()-T_BLOCKSIZE*2)) {
        // Last two zero pages?
        size_t l = T_BLOCKSIZE;
        if (bufsiz < l) {
            l = bufsiz;
        }
        memset(buf,0,l);
        bufsiz -= l;
        debug(TARFILE, "copy clearing last pages.");
    }
    */
    return org_size-bufsiz;
}

bool TarFile::createFile(Path *file, FileStat *stat, uint partnr,
                         FileSystem *src_fs, FileSystem *dst_fs, size_t off,
                         function<void(size_t)> update_progress)
{
    dst_fs->createFile(file, stat, [this,file,src_fs,off,update_progress,partnr] (off_t offset, char *buffer, size_t len) {
            debug(TARFILE,"Write %ju bytes to file %s\n", len, file->c_str());
            size_t n = copy(buffer, len, off+offset, src_fs, partnr);
            debug(TARFILE, "Wrote %ju bytes from %ju to %ju.\n", n, off+offset, offset);
            update_progress(n);
            return n;
        });
    return true;
}

void TarFile::fixSize(size_t split_size)
{
    size_ = current_tar_offset_;
    if (size_ > split_size) {
        num_parts_ = 1 + (size_ / split_size);
        part_size_ = split_size;
        debug(TARFILE, "Splitting file into %u parts.\n", num_parts_);
    } else {
        num_parts_ = 1;
        part_size_ = size_;
    }
}
