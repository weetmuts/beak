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

#include "log.h"
#include "tarentry.h"
#include "util.h"

ComponentId TARFILE = registerLogComponent("tarfile");
ComponentId HASHING = registerLogComponent("hashing");

TarFile::TarFile(TarEntry *d, TarContents tc, int n)
{
    size_ = 0;
    path_ = Path::lookupRoot();
    in_directory_ = d;
    tar_contents = tc;
    memset(&mtim_, 0, sizeof(mtim_));
    // This is a temporary name! It will be overwritten when the hash is finalized!
    name_ = "";
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
    debug(TARFILE, "tarfile", "Looking for offset %zu\n", offset);
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

void TarFile::calculateHash(vector<TarFile*> &tars, string &content)
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

void TarFile::calculateSHA256Hash(vector<TarFile*> &tars, string &content)
{
    SHA256_CTX sha256ctx;
    SHA256_Init(&sha256ctx);
    
    // SHA256 all other tar and gz file hashes! This is the hash of this state!
    for (auto & tf : tars)
    {
        if (tf == this) continue;
        
        SHA256_Update(&sha256ctx, &tf->hash()[0], tf->hash().size());
    }

    // SHA256 the detailed file listing too!
    SHA256_Update(&sha256ctx, &content[0], content.length());
    
    sha256_hash_.resize(SHA256_DIGEST_LENGTH);
    SHA256_Final((unsigned char*)&sha256_hash_[0], &sha256ctx);
}

void TarFile::fixName() {        
        char sizes[32];
        memset(sizes, 0, sizeof(sizes));
        snprintf(sizes, 32, "%zu", size());
        
        char secs_and_nanos[32];
        memset(secs_and_nanos, 0, sizeof(secs_and_nanos));
        snprintf(secs_and_nanos, 32, "%012ju.%09ju", mtim()->tv_sec, mtim()->tv_nsec);
        
	char buffer[256];
        char gztype[] = "gz";
        char tartype[] = "tar";
        char *type = tartype;
        if (chartype() == 'z') {
            type = gztype;
        }
	snprintf(buffer, sizeof(buffer), "%c01_%s_%s_%s_0.%s",
                 chartype(), secs_and_nanos, sizes, toHex(hash()).c_str(), type);
	name_ = buffer;
        path_ = in_directory_->path()->appendName(Atom::lookup(name_));
            
        debug(HASHING,"Fix name of tarfile to %s\n\n", name_.c_str());
}

void TarFile::setName(string s) {
    name_ = s;
    path_ = in_directory_->path()->appendName(Atom::lookup(name_));
}

string TarFile::line(Path *p)
{
    string s;

    s.append(p->str());
    s.append("/");
    s.append(name());
    s.append(separator_string);
    s.append(std::to_string(size()));

    char secs_and_nanos[32];
    memset(secs_and_nanos, 0, sizeof(secs_and_nanos));
    snprintf(secs_and_nanos, 32, "%012ju.%09ju", mtim()->tv_sec, mtim()->tv_nsec);
    s.append(separator_string);
    s.append(secs_and_nanos);
    
    char datetime[20];
    memset(datetime, 0, sizeof(datetime));
    strftime(datetime, 20, "%Y-%m-%d %H:%M.%S", localtime(&mtim()->tv_sec));
    s.append(separator_string);
    s.append(datetime);
    s.append("\n");
    s.append(separator_string); 
    
    return s;
}

void TarFile::updateMtim(struct timespec *mtim) {
    if (isInTheFuture(&mtim_)) {
        fprintf(stderr, "Virtual tarfile %s has a future timestamp! Ignoring the timstamp.\n", path()->c_str());
    } else {    
        if (mtim_.tv_sec > mtim->tv_sec ||
            (mtim_.tv_sec == mtim->tv_sec && mtim_.tv_nsec > mtim->tv_nsec)) {
            memcpy(mtim, &mtim_, sizeof(*mtim));
        }
    }
}

static bool digitsOnly(char *p, size_t len, string *s) {
    while (len-- > 0) {
        char c = *p++;
        if (!c) return false;
        if (!isdigit(c)) return false;
        s->push_back(c);
    }
    return true;
}

static bool hexDigitsOnly(char *p, size_t len, string *s) {
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

bool TarFile::parseFileName(string &name, TarFileName *c)
{
    bool k;
    // Example file name:
    // (r)01_(001501080787).(579054757)_(1119232)_(3b5e4ec7fe38d0f9846947207a0ea44c)_(0).(tar)
    
    if (name.size() == 0) return false;
    
    k = typeFromChar(name[0], &c->type);
    if (!k) return false;

    size_t p1 = name.find('_'); if (p1 == string::npos) return false;
    size_t p2 = name.find('.', p1+1); if (p2 == string::npos) return false;
    size_t p3 = name.find('_', p2+1); if (p3 == string::npos) return false;
    size_t p4 = name.find('_', p3+1); if (p4 == string::npos) return false;
    size_t p5 = name.find('_', p4+1); if (p5 == string::npos) return false;
    size_t p6 = name.find('.', p5+1); if (p6 == string::npos) return false;

    string version;
    k = digitsOnly(&name[1], p1-1, &version);
    if (!k) return false;
    c->version = atoi(version.c_str());

    string secs;
    k = digitsOnly(&name[p1+1], p2-p1-1, &secs);
    if (!k) return false;
    c->secs = atol(secs.c_str());

    string nsecs;
    k = digitsOnly(&name[p2+1], p3-p2-1, &nsecs);
    if (!k) return false;
    c->nsecs = atol(nsecs.c_str());                    

    string size;
    k = digitsOnly(&name[p3+1], p4-p3-1, &nsecs);
    if (!k) return false;    
    c->size = atol(size.c_str());                    

    k = hexDigitsOnly(&name[p4+1], p5-p4-1, &c->header_hash);
    if (!k) return false;

    k = hexDigitsOnly(&name[p5+1], p6-p5-1, &c->content_hash);
    if (!k) return false;

    c->suffix = name.substr(p6+1);
    return true;
}
