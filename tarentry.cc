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

#include "tarentry.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <string.h>
#include <tar.h>
#include <unistd.h>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <openssl/sha.h>
#include <sstream>
#include <zlib.h>

#include "tarfile.h"
#include "log.h"
#include "util.h"

using namespace std;

static ComponentId TARENTRY = registerLogComponent("tarentry");
static ComponentId HARDLINKS = registerLogComponent("hardlinks");

bool sanityCheck(const char *x, const char *y);

TarEntry::TarEntry(size_t size)
{
    abspath_ = Path::lookupRoot();
    path_ = Path::lookupRoot();
    memset(&sb_, 0, sizeof(sb_));
    sb_.st_size = size;
    link_ = NULL;
    taz_file_in_use_ = false;
    children_size_ = 0;
    parent_ = NULL;
    is_tar_storage_dir_ = false;
    num_long_path_blocks = 0;
    num_long_link_blocks = 0;
    num_header_blocks = 0;
    tarpath_ = path_;
    hash_calculated_ = false;
    name_ = Atom::lookup("");

    header_size_ = num_header_blocks = 0;
    disk_size = sb_.st_size;
    // Round size to nearest 512 byte boundary
    children_size_ = blocked_size_ = (size%T_BLOCKSIZE==0)?size:(size+T_BLOCKSIZE-(size%T_BLOCKSIZE));

    debug(TARENTRY, "Regular File Entry added size %ju blocked size %ju!\n", disk_size,
          blocked_size_);
}

TarEntry::TarEntry(Path *ap, Path *p, const struct stat *b, bool header)
{
    abspath_ = ap;
    path_ = p;
    sb_ = *b;
    link_ = NULL;
    taz_file_in_use_ = false;
    children_size_ = 0;
    parent_ = NULL;
    is_tar_storage_dir_ = false;
    num_long_path_blocks = 0;
    num_long_link_blocks = 0;
    num_header_blocks = 1;
    tarpath_ = path_;
    hash_calculated_ = false;
    name_ = p->name();

    // Allocate the TAR object here. It is kept alive forever, until unmount.
    tar_fdopen(&tar_, 0, "", NULL, 0, O_CREAT, 0);
    tar_->options |= TAR_GNU;
    int rc = th_set_from_stat(tar_, &sb_);
    if (rc & TH_COULD_NOT_SET_MTIME) {
        error(TARENTRY, "Could not set last modified time in tar for file: %s\n", abspath_->c_str());
    }
    if (rc & TH_COULD_NOT_SET_SIZE) {
        error(TARENTRY, "Could not set size in tar for file: %s\n", abspath_->c_str());
    }
    th_set_path(tar_, tarpath_->c_str());

    if (TH_ISSYM(tar_))
    {
        char destination[PATH_MAX];
        ssize_t l = readlink(abspath_->c_str(), destination, sizeof(destination));
        if (l < 0) {
            error(TARENTRY, "Could not read link >%s< in underlying filesystem err %d\n", abspath_->c_str(), errno);

            return;
        }
        if (l >= PATH_MAX) {
            l = PATH_MAX - 1;
        }
        destination[l] = '\0';
        link_ = Path::lookup(destination);
        th_set_link(tar_, destination);
        debug(TARENTRY, "Found link from %s to %s\n", abspath_->c_str(), destination);

        if (tar_->th_buf.gnu_longlink != NULL) {
            // We needed to use gnu long links, aka an extra header block
            // plus at least one block for the file name. A link target path longer than 512
            // bytes will need a third block etc
            num_long_link_blocks = 2 + strlen(destination)/T_BLOCKSIZE;
            num_header_blocks += num_long_link_blocks;
            debug(TARENTRY, "Added %ju blocks for long link header for %s\n", num_long_link_blocks, destination);
        }
    }

    th_finish(tar_);

    if (tar_->th_buf.gnu_longname != NULL) {
        // We needed to use gnu long names, aka an extra header block
        // plus at least one block for the file name. A path longer than 512
        // bytes will need a third block etc
        num_long_path_blocks = 2 + tarpath_->c_str_len()/T_BLOCKSIZE;
        num_header_blocks += num_long_path_blocks;
        debug(TARENTRY, "Added %ju blocks for long path header for %s\n", num_long_path_blocks, path_->c_str());
    }

    updateSizes();

    if (!header) {
        stringstream ss;
        ss << permissionString(sb_.st_mode) << separator_string << sb_.st_uid << "/" << sb_.st_gid;
        tv_line_left = ss.str();

        ss.str("");
        if (TH_ISSYM(tar_)) {
            ss << 0;
        } else {
            ss << sb_.st_size;
        }
        tv_line_size = ss.str();

        ss.str("");
        char datetime[20];
        memset(datetime, 0, sizeof(datetime));
        strftime(datetime, 20, "%Y-%m-%d %H:%M.%S", localtime(&sb_.st_mtime));
        ss << datetime;
        ss << separator_string;
        char secs_and_nanos[32];
        memset(secs_and_nanos, 0, sizeof(secs_and_nanos));
        snprintf(secs_and_nanos, 32, "%012ju.%09ju", sb_.st_mtim.tv_sec, sb_.st_mtim.tv_nsec);
        ss << secs_and_nanos;
        tv_line_right = ss.str();
    }
    debug(TARENTRY, "Entry %s added\n", path_->c_str());
}

void TarEntry::calculateTarpath(Path *storage_dir) {
    tarpath_ = path_->subpath(storage_dir->depth());
    th_set_path(tar_, tarpath_->c_str());
    th_finish(tar_);
    tarpath_hash_ = hashString(tarpath_->path());
    assert(sanityCheck(tarpath_->c_str(), th_get_pathname(tar_)));
}

void TarEntry::createSmallTar(int i) {
    small_tars_[i] = new TarFile(this, SMALL_FILES_TAR, i, true);
    tars_.push_back(small_tars_[i]);
}
void TarEntry::createMediumTar(int i) {
    medium_tars_[i] = new TarFile(this, MEDIUM_FILES_TAR, i, true);
    tars_.push_back(medium_tars_[i]);
}
void TarEntry::createLargeTar(uint32_t hash) {
    large_tars_[hash] = new TarFile(this, SINGLE_LARGE_FILE_TAR, hash, true);
    tars_.push_back(large_tars_[hash]);
}

size_t TarEntry::copy(char *buf, size_t size, size_t from) {
    size_t copied = 0;
    debug(TARENTRY, "Copying from %s\n", name_->c_str());
    if (size > 0 && from < header_size_) {
        debug(TARENTRY, "Copying max %zu from %zu, now inside header (header size=%ju)\n", size, from,
              header_size_);
        char tmp[header_size_];
        memset(tmp, 0, header_size_);
        int p = 0;

        if (num_long_link_blocks > 0) {
            char tmp_type = tar_->th_buf.typeflag;
            size_t tmp_size = th_get_size(tar_);

            // Re-use the proper header! Just change the type and size!
            tar_->th_buf.typeflag = GNU_LONGLINK_TYPE;
            th_set_size(tar_, link_->c_str_len());
            th_finish(tar_);

            memcpy(tmp+p, &tar_->th_buf, T_BLOCKSIZE);

            // Reset the header!
            tar_->th_buf.typeflag = tmp_type;
            th_set_size(tar_, tmp_size);
            th_finish(tar_);

            memcpy(tmp+p+T_BLOCKSIZE, link_->c_str(), link_->c_str_len());
            p += num_long_link_blocks*T_BLOCKSIZE;
            debug(TARENTRY, "Wrote long link header for %s\n", link_->c_str());
        }

        if (num_long_path_blocks > 0) {
            char tmp_type = tar_->th_buf.typeflag;
            size_t tmp_size = th_get_size(tar_);

            // Re-use the proper header! Just change the type and size!
            tar_->th_buf.typeflag = GNU_LONGNAME_TYPE;
            // Why can gnu_longname suddenly by 0?
            // Its because we remove the prefix of the path when finishing up the tars!
            // It was long, but now its short. Alas, we do not reshuffle the offset in the tar, yet.
            // So store a short name in the longname.
            assert(tar_->th_buf.gnu_longname == 0 || tarpath_->c_str_len() == strlen(tar_->th_buf.gnu_longname));
            th_set_size(tar_, tarpath_->c_str_len());
            th_finish(tar_);

            memcpy(tmp+p, &tar_->th_buf, T_BLOCKSIZE);

            // Reset the header!
            tar_->th_buf.typeflag = tmp_type;
            th_set_size(tar_, tmp_size);
            th_finish(tar_);

            memcpy(tmp+p+T_BLOCKSIZE, tarpath_->c_str(), tarpath_->c_str_len());
            p += num_long_path_blocks*T_BLOCKSIZE;
            debug(TARENTRY, "Wrote long path header for %s\n", path_->c_str());
        }

        memcpy(tmp+p, &tar_->th_buf, T_BLOCKSIZE);

        // Copy the header out
        size_t len = header_size_-from;
        if (len > size) {
            len = size;
        }
        debug(TARENTRY, "    header out from %s %zu size=%zu\n", path_->c_str(), from, len);
        assert(from+len <= header_size_);
        memcpy(buf, tmp+from, len);
        size -= len;
        buf += len;
        copied += len;
        from += len;
    }

    if (size > 0 && copied < blocked_size_ && from >= header_size_ && from < blocked_size_) {
        debug(TARENTRY, "Copying max %zu from %zu\n now in content", size, from);
        if (virtual_file_) {
            debug(TARENTRY, "Reading from virtual file size=%ju copied=%ju blocked_size=%ju from=%ju header_size=%ju\n",
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
            debug(TARENTRY, "Reading from file size=%ju copied=%ju blocked_size=%ju from=%ju header_size=%ju\n",
                  size, copied, blocked_size_, from, header_size_);
            // Read from file
            int fd = open(abspath_->c_str(), O_RDONLY);
            if (fd==-1) {
                failure(TARENTRY, "Could not open file >%s< in underlying filesystem err %d", path_->c_str(), errno);
                return 0;
            }
            debug(TARENTRY, "    contents out from %s %zu size=%zu\n", path_->c_str(), from-header_size_, size);
            ssize_t l = pread(fd, buf, size, from-header_size_);
            if (l==-1) {
                failure(TARENTRY, "Could not read from file >%s< in underlying filesystem err %d", path_->c_str(), errno);
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
    debug(TARENTRY, "Copied %zu bytes\n", copied);
    return copied;
}

const bool TarEntry::isDir() {
    return TH_ISDIR(tar_);
}

const bool TarEntry::isHardlink() {
    return TH_ISLNK(tar_);
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

void TarEntry::setContent(vector<unsigned char> &c) {
    content = c;
    virtual_file_ = true;
    assert((size_t)sb_.st_size == c.size());
}

void TarEntry::updateSizes() {
    size_t size = header_size_ = num_header_blocks*T_BLOCKSIZE;
    if (TH_ISREG(tar_)) {
        // Directories, symbolic links and fifos have no content in the tar.
        // Only add the size from actual files with content here.
        size += sb_.st_size;
    }
    disk_size = sb_.st_size;
    // Round size to nearest 512 byte boundary
    children_size_ = blocked_size_ = (size%T_BLOCKSIZE==0)?size:(size+T_BLOCKSIZE-(size%T_BLOCKSIZE));

    assert(header_size_ >= T_BLOCKSIZE &&  size >= header_size_ && blocked_size_ >= size);
    assert(TH_ISREG(tar_) || size == header_size_);
//    assert(TH_ISDIR(tar_) || TH_ISSYM(tar_) || TH_ISFIFO(tar_) || TH_ISCHR(tar_) || TH_ISBLK(tar_) || th_get_size(tar_) == (size_t)sb.st_size);
}

void TarEntry::rewriteIntoHardLink(TarEntry *target) {
    link_ = target->tarpath_;
    tar_->th_buf.typeflag = LNKTYPE;
    assert(this->isHardLink());
}

bool TarEntry::fixHardLink(Path *storage_dir) {
    assert(tar_->th_buf.typeflag == LNKTYPE);

    debug(HARDLINKS, "Fix hardlink >%s< to >%s< within storage >%s<\n", path_->c_str(), link_->c_str(), storage_dir->c_str());

    num_header_blocks -= num_long_link_blocks;

    Path *common = Path::commonPrefix(storage_dir, link_);
    debug(HARDLINKS, "COMMON PREFIX >%s< >%s< = >%s<\n", storage_dir->c_str(), link_->c_str(), common?common->c_str():"NULL");
    if (common == NULL || common->depth() < storage_dir->depth()) {
    	warning(HARDLINKS, "Warning: hard link between tars detected! From %s to %s\n", path_->c_str(), link_->c_str());
    	// This hardlink needs to be pushed upwards, into a tar on a higher level!
    	return false;
    }

    Path *l = link_->subpath(storage_dir->depth());
    debug(HARDLINKS, "CUT LINK >%s< to >%s<\n", link_->c_str(), l->c_str());
    th_set_link(tar_, l->c_str());
    if (tar_->th_buf.gnu_longlink != NULL) {
        // We needed to use gnu long links, aka an extra header block
        // plus at least one block for the file name. A link target path longer than 512
        // bytes will need a third block etc
        num_long_link_blocks = 2 + l->c_str_len()/T_BLOCKSIZE;
        num_header_blocks += num_long_link_blocks;
        debug(HARDLINKS, "Added %ju blocks for long link header for %s\n",
              num_long_link_blocks, tarpath_->c_str(), l->c_str());
    }

    th_finish(tar_);
    updateSizes();
    debug(HARDLINKS, "Updated hardlink %s to %s\n", tarpath_->c_str(), link_->c_str());
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
    parent->entries_.insert(parent->entries_.end(), entry);
}

/**
 * Update the mtim argument with this entry's mtim, if this entry is younger.
 */
void TarEntry::updateMtim(struct timespec *mtim) {
    if (isInTheFuture(&sb_.st_mtim)) {
        fprintf(stderr, "Entry %s has a future timestamp! Ignoring the timstamp.\n", path()->c_str());
    } else {
        if (sb_.st_mtim.tv_sec > mtim->tv_sec ||
            (sb_.st_mtim.tv_sec == mtim->tv_sec && sb_.st_mtim.tv_nsec > mtim->tv_nsec)) {
            memcpy(mtim, &sb_.st_mtim, sizeof(*mtim));
        }
    }
}

void TarEntry::registerTarFile(TarFile *tf, size_t o) {
    tar_file_ = tf;
    tar_offset_ = o;
}

void TarEntry::registerTazFile() {
    taz_file_ = new TarFile(this, DIR_TAR, 0, true);
    tars_.push_back(taz_file_);
}

void TarEntry::registerGzFile() {
    gz_file_ = new TarFile(this, REG_FILE, 0, false);
    tars_.push_back(gz_file_);
}

void TarEntry::registerParent(TarEntry *p) {
    parent_ = p;
}

TarEntry *TarEntry::newVolumeHeader() {
    struct stat sb;
    memset(&sb, 0, sizeof(sb));
    TarEntry *header = new TarEntry(Path::lookup(""), Path::lookup(""), &sb, true);
    memcpy(header->tar_->th_buf.name, "tarredfs", 9);
    header->tar_->th_buf.typeflag = GNU_VOLHDR_TYPE;
    header->name_ = Atom::lookup("VolHead");
    return header;
}

void TarEntry::secsAndNanos(char *buf, size_t len)
{
    memset(buf, 0, len);
    snprintf(buf, len, "%012ju.%09ju", sb_.st_mtim.tv_sec, sb_.st_mtim.tv_nsec);
}

void TarEntry::injectHash(const char *buf, size_t len)
{
    assert(len<90);
    memcpy(tar_->th_buf.name+9, buf, len);
    th_finish(tar_);
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
}

void TarEntry::sortEntries() {
    std::sort(entries_.begin(), entries_.end(),
              [](TarEntry *a, TarEntry *b)->bool {
                  return TarSort::lessthan(a->path(), b->path());
              });
}

string TarEntry::headerHash() {
    if (!hash_calculated_) {
        SHA256_CTX sha256;
        SHA256_Init(&sha256);
        
        size_t len = headerSize();
        assert(len <= 512 * 5);
        char buf[len];
        memset(buf, 0x42, len);
        TarEntry *te = this;
        size_t rc = te->copy(buf, len, 0);
        assert(rc == len);
        // Update the header hash with the exact header bits.
        SHA256_Update(&sha256, buf, len);
        // Update the header hash with seconds and nanoseconds.
        char secs_and_nanos[32];
        te->secsAndNanos(secs_and_nanos, 32);
        SHA256_Update(&sha256, secs_and_nanos, strlen(secs_and_nanos));
        SHA256_Final((unsigned char*) header_hash_, &sha256);
        hash_calculated_ = true;
    }
    return toHex(header_hash_, SHA256_DIGEST_LENGTH);
}

string TarEntry::contentHash() {
    return "0";
}

void cookEntry(string *listing, TarEntry *entry) {
    
    // -r-------- fredrik/fredrik 745 1970-01-01 01:00 testing
    // drwxrwxr-x fredrik/fredrik   0 2016-11-25 00:52 autoconf/
    // -r-------- fredrik/fredrik   0 2016-11-25 11:23 libtar.so -> libtar.so.0.1
    listing->append(entry->tv_line_left);            
    listing->append(separator_string);
    listing->append(entry->tv_line_size);
    listing->append(separator_string);
    listing->append(entry->tv_line_right);
    listing->append(separator_string);
    listing->append(entry->tarpath()->path());
    listing->append(separator_string);
    if (entry->link() != NULL) {
        if (entry->isSymLink()) {
            listing->append(" -> ");
        } else {
            listing->append(" link to ");
        }
        listing->append(entry->link()->path());
    } else {
        listing->append(" ");
    }
    listing->append(separator_string);
    listing->append(entry->tarFile()->name());
    listing->append(separator_string);
    stringstream ss;
    ss << entry->tarOffset()+entry->headerSize();
    listing->append(ss.str());
    listing->append(separator_string);
    listing->append(entry->contentHash());
    listing->append(separator_string);
    listing->append(entry->headerHash());
    listing->append("\n");
    listing->append(separator_string);
}

bool eatEntry(vector<char> &v, vector<char>::iterator &i, Path *dir_to_prepend,
              mode_t *mode, size_t *size, size_t *offset, string *tar, Path **path,
              string *link, bool *is_sym_link, time_t *secs, time_t *nanos, bool *eof, bool *err)
{
    string permission = eatTo(v, i, separator, 32, eof, err);
    if (*err || *eof) return false;

    *mode = stringToPermission(permission);
    if (*mode == 0) {
        *err = true;
        return false;
    }

    string uidgid = eatTo(v, i, separator, 32, eof, err);
    if (*err || *eof) return false;
    string si = eatTo(v, i, separator, 32, eof, err);
    if (*err || *eof) return false;
    *size = atol(si.c_str());

    string datetime = eatTo(v, i, separator, 32, eof, err);
    if (*err || *eof) return false;
    string secs_and_nanos = eatTo(v, i, separator, 64, eof, err);
    if (*err || *eof) return false;

    // Extract secs and nanos from string.
    std::vector<char> sn(secs_and_nanos.begin(), secs_and_nanos.end());
    auto j = sn.begin();
    string se = eatTo(sn, j, '.', 64, eof, err);
    if (*err || *eof) return false;
    string na = eatTo(sn, j, -1, 64, eof, err);
    if (*err) return false; // Expect eof here!
    *secs = atol(se.c_str());
    *nanos = atol(na.c_str());

    string filename = dir_to_prepend->path() + "/" + eatTo(v, i, separator, 1024, eof, err);
    if (*err || *eof) return false;
    if (filename.length() > 1 && filename.back() == '/')
    {
        filename = filename.substr(0, filename.length() - 1);
    }
    *path = Path::lookup(filename);
    *link = eatTo(v, i, separator, 1024, eof, err);
    if (*err || *eof) return false;
    *is_sym_link = false;
    if (link->length() > 4 && link->substr(0, 4) == " -> ")
    {
        *link = link->substr(4);
        *size = link->length();
        *is_sym_link = true;
    }
    else if (link->length() > 9 && link->substr(0, 9) == " link to ")
    {
        *link = link->substr(9);
        *size = link->length();
        *is_sym_link = false;
    }
    *tar = dir_to_prepend->path() + "/" + eatTo(v, i, separator, 1024, eof, err);
    if (*err || *eof) return false;
    string off = eatTo(v, i, separator, 32, eof, err);
    *offset = atol(off.c_str());
    if (*err || *eof) return false;
    string content_hash = eatTo(v, i, separator, 40, eof, err);
    if (*err || *eof) return false;
    string header_hash = eatTo(v, i, separator, 40, eof, err);
    if (*err) return false; // Accept eof here!
    return true;
}
                

