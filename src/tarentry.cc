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

#include "tar.h"
#include "tarentry.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <unistd.h>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <openssl/sha.h>
#include <zlib.h>

#include "tarfile.h"
#include "log.h"
#include "util.h"

using namespace std;

static ComponentId TARENTRY = registerLogComponent("tarentry");
static ComponentId HARDLINKS = registerLogComponent("hardlinks");

bool sanityCheck(const char *x, const char *y);

TarEntry::TarEntry(size_t size, TarHeaderStyle ths)
{
    abspath_ = Path::lookupRoot();
    path_ = Path::lookupRoot();
    tar_header_style_ = ths;
    memset(&fs_, 0, sizeof(fs_));
    fs_.st_size = size;
    is_hard_linked_ = false;

    link_ = NULL;
    taz_file_in_use_ = false;
    children_size_ = 0;
    parent_ = NULL;
    is_tar_storage_dir_ = false;
    tarpath_ = path_;
    name_ = Atom::lookup("");

    header_size_ = 0;

    // Round size to nearest 512 byte boundary
    children_size_ = blocked_size_ = (size%T_BLOCKSIZE==0)?size:(size+T_BLOCKSIZE-(size%T_BLOCKSIZE));

    debug(TARENTRY, "index file entry added size %ju blocked size %ju!\n", fs_.st_size, blocked_size_);
}

TarEntry::TarEntry(Path *ap, Path *p, FileStat *st, TarHeaderStyle ths, bool should_content_split) : fs_(*st)
{
    tar_header_style_ = ths;
    abspath_ = ap;
    path_ = p;
    is_hard_linked_ = false;
    link_ = NULL;
    taz_file_in_use_ = false;
    children_size_ = 0;
    parent_ = NULL;
    is_tar_storage_dir_ = false;
    tarpath_ = path_;
    name_ = p->name();
    should_content_split_ = should_content_split;

    if (isSymbolicLink())
    {
        char destination[PATH_MAX];
        ssize_t l = readlink(abspath_->c_str(), destination, sizeof(destination));
        if (l < 0) {
            error(TARENTRY, "Could not read link >%s< in underlying filesystem err %d\n",
		  abspath_->c_str(), errno);
            return;
        }
        if (l >= PATH_MAX) {
            l = PATH_MAX - 1;
        }
        destination[l] = '\0';
        link_ = Path::lookup(destination);
        debug(TARENTRY, "found link from %s to %s\n", abspath_->c_str(), destination);
    }

    updateSizes();

    if (tar_header_style_ != TarHeaderStyle::None) {

    }

    debug(TARENTRY, "entry %s added size %ju blocked size %ju %s\n", path_->c_str(), fs_.st_size, blocked_size_,
          should_content_split?"CSPLIT":"");
}

void TarEntry::calculateTarpath(Path *storage_dir) {
    size_t old_header_size = header_size_;
    tarpath_ = path_->subpath(storage_dir->depth());
    tarpath_hash_ = hashString(tarpath_->str());

    updateSizes();
    if (header_size_ < old_header_size) {
        debug(TARENTRY,"avoided long path block!\n");
    }
}

void TarEntry::createSmallTar(int i) {
    small_tars_[i] = new TarFile(SMALL_FILES_TAR);
    tars_.push_back(small_tars_[i]);
}
void TarEntry::createMediumTar(int i) {
    medium_tars_[i] = new TarFile(MEDIUM_FILES_TAR);
    tars_.push_back(medium_tars_[i]);
}
void TarEntry::createLargeTar(uint32_t hash) {
    large_tars_[hash] = new TarFile(SINGLE_LARGE_FILE_TAR);
    tars_.push_back(large_tars_[hash]);
}

size_t TarEntry::copy(char *buf, size_t size, size_t from, FileSystem *fs)
{
    size_t copied = 0;
    debug(TARENTRY, "copying from %s\n", name_->c_str());

    if (size > 0 && from < header_size_)
    {
        debug(TARENTRY, "copying max %zu from %zu, now inside header (header size=%ju)\n", size, from,
              header_size_);

        char tmp[header_size_];
        memset(tmp, 0, header_size_);
        int p = 0;

        TarHeader th(&fs_, tarpath_, link_, is_hard_linked_, tar_header_style_ == TarHeaderStyle::Full);

        if (th.numLongLinkBlocks() > 0)
        {
            TarHeader llh;
            llh.setLongLinkType(&th);
            llh.setSize(link_->c_str_len());
            llh.calculateChecksum();

            memcpy(tmp+p, llh.buf(), T_BLOCKSIZE);
            memcpy(tmp+p+T_BLOCKSIZE, link_->c_str(), link_->c_str_len());
            p += th.numLongLinkBlocks()*T_BLOCKSIZE;
            debug(TARENTRY, "wrote long link header for %s\n", link_->c_str());
        }

        if (th.numLongPathBlocks() > 0)
        {
            TarHeader lph;
            lph.setLongPathType(&th);
            lph.setSize(tarpath_->c_str_len()+1);
            lph.calculateChecksum();

            memcpy(tmp+p, lph.buf(), T_BLOCKSIZE);
            memcpy(tmp+p+T_BLOCKSIZE, tarpath_->c_str(), tarpath_->c_str_len());
            p += th.numLongPathBlocks()*T_BLOCKSIZE;
            debug(TARENTRY, "wrote long path header for %s\n", tarpath_->c_str());
        }

        memcpy(tmp+p, th.buf(), T_BLOCKSIZE);

        // Copy the header out
        size_t len = header_size_-from;
        if (len > size) {
            len = size;
        }
        debug(TARENTRY, "header out from %s %zu size=%zu\n", path_->c_str(), from, len);
        assert(from+len <= header_size_);
        memcpy(buf, tmp+from, len);
        size -= len;
        buf += len;
        copied += len;
        from += len;
    }

    if (size > 0 && copied < blocked_size_ && from >= header_size_ && from < blocked_size_) {
        debug(TARENTRY, "copying max %zu from %zu from content %s\n"
	      "with blocked_size=%zu header_size=%zu hard?=%d\n", size, from, tarpath_->c_str(), blocked_size_, header_size_,
	    is_hard_linked_);
        if (virtual_file_) {
            debug(TARENTRY, "reading from virtual file size=%ju copied=%ju blocked_size=%ju from=%ju header_size=%ju\n",
                  size, copied, blocked_size_, from, header_size_);
            size_t off = from - header_size_;
            size_t len = content.size()-off;
            if (len > size) {
                len = size;
            }
            memcpy(buf, &content[0]+off, len);
            size -= len;
            buf += len;
            copied += len;
        } else {
            debug(TARENTRY, "reading from file size=%ju copied=%ju blocked_size=%ju from=%ju header_size=%ju\n",
                  size, copied, blocked_size_, from, header_size_);
            debug(TARENTRY, "        contents out from %s %zu size=%zu\n", path_->c_str(), from-header_size_, size);
            ssize_t l = fs->pread(abspath_, buf, size, from-header_size_);
            if (l==-1) {
                failure(TARENTRY, "Could not open file \"%s\"\n", abspath_->c_str());
            }
            assert(l>0);
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
    debug(TARENTRY, "copied %zu bytes\n", copied);
    return copied;
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
                error(TARENTRY, "Internal error, these should be equal!\n>%s<\n>%s<\nlen %zu\n ", x, y, yl);
                return false;
            }
        }
    }
    return true;
}

void TarEntry::setContent(vector<char> &c) {
    content = c;
    virtual_file_ = true;
    assert((size_t)fs_.st_size == c.size());
}

void TarEntry::updateSizes()
{
    size_t size = header_size_ = TarHeader::calculateHeaderSize(tarpath_, link_, is_hard_linked_);

    if (tar_header_style_ == TarHeaderStyle::None) {
        size = header_size_ = 0;
    }
    if (isRegularFile() && !is_hard_linked_) {
        // Directories, symbolic links and fifos have no content in the tar.
        // Only add the size from actual files with content here.
        size += fs_.st_size;
    }
    // Round size to nearest 512 byte boundary
    children_size_ = blocked_size_ = (size%T_BLOCKSIZE==0)?size:(size+T_BLOCKSIZE-(size%T_BLOCKSIZE));

    assert(size >= header_size_ && blocked_size_ >= size);
}

void TarEntry::rewriteIntoHardLink(TarEntry *target) {
    link_ = target->path_;
    is_hard_linked_ = true;
    updateSizes();
    assert(link_->c_str()[0] == '/');
}

bool TarEntry::calculateHardLink(Path *storage_dir)
{
    Path *new_link = link_->subpath(storage_dir->depth());
    debug(HARDLINKS, "removed prefix from >%s< to >%s<\n", link_->c_str(), new_link->c_str());
    link_ = new_link;
    updateSizes();
    return true;
}

void TarEntry::moveEntryToNewParent(TarEntry *entry, TarEntry *parent) {
    auto pos = find(entries_.begin(), entries_.end(), entry);
    if (pos == entries_.end()) {
        error(TARENTRY, "Could not move entry!");
    }
    entries_.erase(pos);
    parent->entries_.insert(parent->entries_.end(), entry);
}

void TarEntry::copyEntryToNewParent(TarEntry *entry, TarEntry *parent) {
    TarEntry *copy = new TarEntry(*entry);
    parent->entries_.insert(parent->entries_.end(), copy);
}

/**
 * Update the mtim argument with this entry's mtim, if this entry is younger.
 */
void TarEntry::updateMtim(struct timespec *mtim) {
    if (isInTheFuture(&fs_.st_mtim)) {
        warning(TARENTRY, "Entry %s has a future timestamp! Ignoring the timstamp.\n", path()->c_str());
    } else {
        if (fs_.st_mtim.tv_sec > mtim->tv_sec ||
            (fs_.st_mtim.tv_sec == mtim->tv_sec && fs_.st_mtim.tv_nsec > mtim->tv_nsec)) {
            memcpy(mtim, &fs_.st_mtim, sizeof(*mtim));
        }
    }
}

void TarEntry::registerTarFile(TarFile *tf, size_t o) {
    tar_file_ = tf;
    tar_offset_ = o;
}

void TarEntry::registerTazFile() {
    taz_file_ = new TarFile(DIR_TAR);
    tars_.push_back(taz_file_);
}

void TarEntry::registerGzFile() {
    gz_file_ = new TarFile(REG_FILE);
    tars_.push_back(gz_file_);
}

void TarEntry::registerParent(TarEntry *p) {
    parent_ = p;
}

void TarEntry::secsAndNanos(char *buf, size_t len)
{
    memset(buf, 0, len);
    snprintf(buf, len, "%012" PRINTF_TIME_T "u.%09lu", fs_.st_mtim.tv_sec, fs_.st_mtim.tv_nsec);
}

void TarEntry::addChildrenSize(size_t s)
{
    children_size_ += s;
}

void TarEntry::addDir(TarEntry *dir) {
    dirs_.push_back(dir);
}

void TarEntry::addEntry(TarEntry *te) {
    entries_.push_back(te);
    te->storage_dir_ = this;
}

void TarEntry::sortEntries() {
    sort(entries_.begin(), entries_.end(),
              [](TarEntry *a, TarEntry *b)->bool {
                  return TarSort::lessthan(a->path(), b->path());
              });
}

void TarEntry::calculateHash() {
    calculateSHA256Hash();
}

vector<char> &TarEntry::metaHash() {
    return meta_sha256_hash_;
}

void TarEntry::calculateSHA256Hash()
{
    SHA256_CTX sha256ctx;
    SHA256_Init(&sha256ctx);

    // Hash the file name and its path within the tar.
    SHA256_Update(&sha256ctx, tarpath_->c_str(), tarpath_->c_str_len());

    // Hash the file size.
    off_t filesize;
    if (isRegularFile()) {
        filesize = fs_.st_size;
    } else {
        filesize = 0;
    }
    SHA256_Update(&sha256ctx, &filesize, sizeof(filesize));

    // Hash the last modification time in seconds and nanoseconds.
    time_t secs  = fs_.st_mtim.tv_sec;
    long   nanos = fs_.st_mtim.tv_nsec;

    SHA256_Update(&sha256ctx, &secs, sizeof(secs));
    SHA256_Update(&sha256ctx, &nanos, sizeof(nanos));

    meta_sha256_hash_.resize(SHA256_DIGEST_LENGTH);
    SHA256_Final((unsigned char*)&meta_sha256_hash_[0], &sha256ctx);
}

string cookColumns()
{
    int i = 0;
    string s;

    s += "permissions "; i++;
    s += "uid/gid "; i++;
    s += "size "; i++;
    s += "ctime "; i++;
    s += "path "; i++;
    s += "link "; i++;
    s += "tarprefix "; i++;
    s += "offset "; i++;
    s += "multipart(num,partoffset,size,last_size) "; i++; // eg 2,512,65536,238
    s += "path_size_ctime_hash "; i++;

    return to_string(i)+": "+s;
}

void cookEntry(string *listing, TarEntry *entry) {

    // -r-------- fredrik/fredrik 745 1970-01-01 01:00 testing
    // drwxrwxr-x fredrik/fredrik   0 2016-11-25 00:52 autoconf/
    // -r-------- fredrik/fredrik   0 2016-11-25 11:23 libtar.so -> libtar.so.0.1
    listing->append(permissionString(&entry->fs_));
    listing->append(separator_string);
    listing->append(to_string(entry->fs_.st_uid));
    listing->append("/");
    listing->append(to_string(entry->fs_.st_gid));
    listing->append(separator_string);

    if (entry->isRegularFile()) {
        listing->append(to_string(entry->fs_.st_size));
    } else if (entry->isCharacterDevice() || entry->isBlockDevice()) {
        listing->append(to_string(MajorDev(entry->fs_.st_rdev)) + "," + to_string(MinorDev(entry->fs_.st_rdev)));
    } else {
        listing->append("0");
    }
    listing->append(separator_string);

    /*
    char datetime[20];
    memset(datetime, 0, sizeof(datetime));
    strftime(datetime, 20, "%Y-%m-%d %H:%M.%S", localtime(&entry->fs_.st_mtim.tv_sec));
    listing->append(datetime);
    listing->append(separator_string);
    */
    char secs_and_nanos[32];
    memset(secs_and_nanos, 0, sizeof(secs_and_nanos));
    snprintf(secs_and_nanos, 32, "%012" PRINTF_TIME_T "u.%09lu",
             entry->fs_.st_mtim.tv_sec, entry->fs_.st_mtim.tv_nsec);
    listing->append(secs_and_nanos);
    listing->append(separator_string);

    /*
      memset(secs_and_nanos, 0, sizeof(secs_and_nanos));
      snprintf(secs_and_nanos, 32, "%012ju.%09ju", fs_.st_atim.tv_sec, fs_.st_atim.tv_nsec);
      s.append(secs_and_nanos);
      s.append(separator_string);

      memset(secs_and_nanos, 0, sizeof(secs_and_nanos));
      snprintf(secs_and_nanos, 32, "%012ju.%09ju", fs_.st_ctim.tv_sec, fs_.st_ctim.tv_nsec);
      s.append(secs_and_nanos);
    */

    listing->append(entry->tarpath()->str());
    listing->append(separator_string);
    if (entry->link() != NULL) {
        if (entry->isSymbolicLink()) {
            listing->append(" -> ");
        } else {
            listing->append(" link to ");
        }
        listing->append(entry->link()->str());
    }
    listing->append(separator_string);
    char filename[256];
    TarFileName tfn(entry->tarFile(), 0);
    tfn.writeTarFileNameIntoBuffer(filename, sizeof(filename), NULL);
    listing->append(filename);
    listing->append(separator_string);
    listing->append(to_string(entry->tarOffset()+entry->headerSize()));
    listing->append(separator_string);

    if (entry->tarFile()->numParts() == 1)
    {
        listing->append("1");
    }
    else
    {
        // Multipart num,offset,size,last_size
        char nps[256];
        TarFile *tf = entry->tarFile();
        uint np = tf->numParts();
        snprintf(nps, sizeof(nps), "%u,%zu,%zu,%zu", np, tf->partHeaderSize(), tf->size(0), tf->size(np-1));
        listing->append(nps);
    }
    listing->append(separator_string);

    listing->append(toHex(entry->metaHash()));
    listing->append("\n");
    listing->append(separator_string);
}

bool eatEntry(int beak_version, vector<char> &v, vector<char>::iterator &i, Path *dir_to_prepend,
              FileStat *fs, size_t *offset, string *tar, Path **path,
              string *link, bool *is_sym_link, bool *is_hard_link,
              uint *num_parts, size_t *part_offset, size_t *part_size, size_t *last_part_size,
              bool *eof, bool *err)
{
    string permission = eatTo(v, i, separator, 32, eof, err);
    if (*err || *eof) return false;

    fs->st_mode = stringToPermission(permission);
    if (fs->st_mode == 0) {
        *err = true;
        return false;
    }

    string uidgid = eatTo(v, i, separator, 32, eof, err);
    if (*err || *eof) return false;
    string uid = uidgid.substr(0,uidgid.find('/'));
    string gid = uidgid.substr(uidgid.find('/')+1);
    fs->st_uid = atoi(uid.c_str());
    fs->st_gid = atoi(gid.c_str());
    string si = eatTo(v, i, separator, 32, eof, err);
    if (*err || *eof) return false;
    if (fs->isCharacterDevice() || fs->isBlockDevice()) {
        string maj = si.substr(0,si.find(','));
        string min = si.substr(si.find(',')+1);
        fs->st_rdev = MakeDev(atoi(maj.c_str()), atoi(min.c_str()));
    } else {
        fs->st_size = atol(si.c_str());
    }

/*    string datetime = eatTo(v, i, separator, 32, eof, err);
      if (*err || *eof) return false;*/

    string secs_and_nanos = eatTo(v, i, separator, 64, eof, err);
    if (*err || *eof) return false;

    // Extract modify time, secs and nanos from string.
    {
        vector<char> sn(secs_and_nanos.begin(), secs_and_nanos.end());
        auto j = sn.begin();
        string se = eatTo(sn, j, '.', 64, eof, err);
        if (*err || *eof) return false;
        string na = eatTo(sn, j, -1, 64, eof, err);
        if (*err) return false; // Expect eof here!
        fs->st_mtim.tv_sec = atol(se.c_str());
        fs->st_mtim.tv_nsec = atol(na.c_str());
    }

    /*
    secs_and_nanos = eatTo(v, i, separator, 64, eof, err);
    if (*err || *eof) return false;

    // Extract access time, secs and nanos from string.
    {
        vector<char> sn(secs_and_nanos.begin(), secs_and_nanos.end());
        auto j = sn.begin();
        string se = eatTo(sn, j, '.', 64, eof, err);
        if (*err || *eof) return false;
        string na = eatTo(sn, j, -1, 64, eof, err);
        if (*err) return false; // Expect eof here!
        fs->st_atim.tv_sec = atol(se.c_str());
        fs->st_atim.tv_nsec = atol(na.c_str());
    }

    secs_and_nanos = eatTo(v, i, separator, 64, eof, err);
    if (*err || *eof) return false;

    // Extract change time, secs and nanos from string.
    {
        vector<char> sn(secs_and_nanos.begin(), secs_and_nanos.end());
        auto j = sn.begin();
        string se = eatTo(sn, j, '.', 64, eof, err);
        if (*err || *eof) return false;
        string na = eatTo(sn, j, -1, 64, eof, err);
        if (*err) return false; // Expect eof here!
        fs->st_ctim.tv_sec = atol(se.c_str());
        fs->st_ctim.tv_nsec = atol(na.c_str());
    }
    */
    string filename;
    if (dir_to_prepend) {
        filename = dir_to_prepend->str() + "/" + eatTo(v, i, separator, 1024, eof, err);
    } else {
        filename = eatTo(v, i, separator, 1024, eof, err);
    }
    if (*err || *eof) return false;
    if (filename.length() > 1 && filename.back() == '/')
    {
        filename = filename.substr(0, filename.length() - 1);
    }
    *path = Path::lookup(filename);
    *link = eatTo(v, i, separator, 1024, eof, err);
    if (*err || *eof) return false;
    *is_sym_link = false;
    *is_hard_link = false;
    if (link->length() > 4 && link->substr(0, 4) == " -> ")
    {
        *link = link->substr(4);
        fs->st_size = link->length();
        *is_sym_link = true;
        *is_hard_link = false;
    }
    else if (link->length() > 9 && link->substr(0, 9) == " link to ")
    {
        *link = link->substr(9);
        fs->st_size = link->length();
        *is_sym_link = false;
        *is_hard_link = true;
    }
    if (dir_to_prepend) {
        *tar = dir_to_prepend->str() + "/" + eatTo(v, i, separator, 1024, eof, err);
    } else {
        *tar = eatTo(v, i, separator, 1024, eof, err);
    }
    if (*err || *eof) return false;

    string off = eatTo(v, i, separator, 32, eof, err);
    if (*err || *eof) return false;
    *offset = atol(off.c_str());

    string multipart = eatTo(v, i, separator, 128, eof, err);
    if (*err || *eof) return false;

    vector<char> multip(multipart.begin(), multipart.end());
    auto j = multip.begin();
    if (multipart == "1") {
        *num_parts = 1;
        *part_size = 0;
        *last_part_size = 0;
    }
    else
    {
        string num_parts_s = eatTo(multip, j, ',', 64, eof, err);
        if (*err || *eof) return false;
        string offset_s = eatTo(multip, j, ',', 64, eof, err);
        if (*err || *eof) return false;
        string part_size_s = eatTo(multip, j, ',', 64, eof, err);
        if (*err || *eof) return false;
        string last_part_size_s = eatTo(multip, j, -1, 64, eof, err);
        if (*err) return false;
        *num_parts = atol(num_parts_s.c_str());
        *part_offset = atol(offset_s.c_str());
        *part_size = atol(part_size_s.c_str());
        *last_part_size = atol(last_part_size_s.c_str());
    }
    string meta_hash = eatTo(v, i, separator, 65, eof, err);
    meta_hash.pop_back(); // Last column in line has the newline
    if (*err) return false; // Accept eof here!

    return true;
}
