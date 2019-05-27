/*
 Copyright (C) 2018 Fredrik Öhrström

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

#include "filesystem_helpers.h"

#include "log.h"

#include <vector>
#include <map>

using namespace std;

static ComponentId CACHE = registerLogComponent("cache");

RC ReadOnlyFileSystem::chmod(Path *p, FileStat *fs)
{
    return RC::ERR;
}

RC ReadOnlyFileSystem::utime(Path *p, FileStat *fs)
{
    return RC::ERR;
}

Path *ReadOnlyFileSystem::tempDir()
{
    return NULL;
}

Path *ReadOnlyFileSystem::mkTempFile(string prefix, string content)
{
    return NULL;
}

Path *ReadOnlyFileSystem::mkTempDir(string prefix)
{
    return NULL;
}

Path *ReadOnlyFileSystem::mkDir(Path *p, string name, int permissions)
{
    return NULL;
}

RC ReadOnlyFileSystem::rmDir(Path *p)
{
    return RC::ERR;
}

RC ReadOnlyFileSystem::createFile(Path *file, std::vector<char> *buf)
{
    return RC::ERR;
}

bool ReadOnlyFileSystem::createFile(Path *path, FileStat *stat,
                                    std::function<size_t(off_t offset, char *buffer, size_t len)> cb)
{
    return false;
}

bool ReadOnlyFileSystem::createSymbolicLink(Path *path, FileStat *stat, string link)
{
    return false;
}

bool ReadOnlyFileSystem::createHardLink(Path *path, FileStat *stat, Path *target)
{
    return false;
}

bool ReadOnlyFileSystem::createFIFO(Path *path, FileStat *stat)
{
    return false;
}

bool ReadOnlyFileSystem::deleteFile(Path *path)
{
    return false;
}

RC ReadOnlyFileSystem::enableWatch()
{
    return RC::ERR;
}

RC ReadOnlyFileSystem::addWatch(Path *dir)
{
    return RC::ERR;
}

int ReadOnlyFileSystem::endWatch()
{
    return 0;
}

RC ReadOnlyFileSystem::mountDaemon(Path *dir, FuseAPI *fuseapi, bool foreground, bool debug)
{
    return RC::ERR;
}

unique_ptr<FuseMount> ReadOnlyFileSystem::mount(Path *dir, FuseAPI *fuseapi, bool debug)
{
    return NULL;
}

RC ReadOnlyFileSystem::umount(ptr<FuseMount> fuse_mount)
{
    return RC::ERR;
}

bool StatOnlyFileSystem::readdir(Path *p, vector<Path*> *vec)
{
    return false;
}

ssize_t StatOnlyFileSystem::pread(Path *p, char *buf, size_t size, off_t offset)
{
    return -4711;
}

RC StatOnlyFileSystem::recurse(Path *root, std::function<RecurseOption(Path *path, FileStat *stat)> cb)
{
    assert(0);
    return RC::ERR;
}

RC StatOnlyFileSystem::recurse(Path *root, std::function<RecurseOption(const char *path, const struct stat *sb)> cb)
{
    assert(0);
    return RC::ERR;
}

RC StatOnlyFileSystem::ctimeTouch(Path *p)
{
    return RC::ERR;
}

RC StatOnlyFileSystem::stat(Path *p, FileStat *fs)
{
    if (contents_.count(p) != 0) {
        *fs = contents_[p];
        return RC::OK;
    }
    return RC::ERR;
}

RC StatOnlyFileSystem::loadVector(Path *file, size_t blocksize, std::vector<char> *buf)
{
    return RC::OK;
}

bool StatOnlyFileSystem::readLink(Path *file, string *target)
{
    return false;
}

RC ReadOnlyCacheFileSystemBaseImplementation::stat(Path *p, FileStat *fs)
{
    if (entries_.count(p) == 0) {
        return RC::ERR;
    }
    CacheEntry *ce = &entries_[p];
    *fs = ce->stat;
    return RC::OK;
}

bool ReadOnlyCacheFileSystemBaseImplementation::readdir(Path *p, vector<Path*> *vec)
{
    if (entries_.count(p) == 0) {
        return false;
    }
    CacheEntry *ce = &entries_[p];
    for (auto& p : ce->direntries) {
        vec->push_back(p.first->subpath(drop_prefix_depth_));
    }
    return true;
}

bool ReadOnlyCacheFileSystemBaseImplementation::fileCached(Path *p)
{
    if (entries_.count(p) == 0) {
        // No such file found!
        debug(CACHE, "no such file found in cache index: %s\n", p->c_str());
        return false;
    }
    CacheEntry *e = &entries_[p];
    if (e->cached) {
        return true;
    }
    e->cached = e->isCached(cache_fs_, cache_dir_, p);
    if (e->cached) {
        return true;
    }

    debug(CACHE, "needs: %s\n", p->c_str());
    RC rc = fetchFile(p);

    if (rc.isErr()) {
        failure(CACHE, "Could not fetch file: %s\n", p->c_str());
        return false;
    }

    e->cached = e->isCached(cache_fs_, cache_dir_, p);

    if (!e->cached) {
        failure(CACHE, "Failed to fetch file: %s\n", p->c_str());
    }
    return e->cached;
}

CacheEntry *ReadOnlyCacheFileSystemBaseImplementation::cacheEntry(Path *p)
{
    if (entries_.count(p) == 0) return NULL;
    return &entries_[p];
}

ssize_t ReadOnlyCacheFileSystemBaseImplementation::pread(Path *p, char *buf, size_t size, off_t offset)
{
    if (!fileCached(p)) {  return -1; }
    Path *pp = p->prepend(cache_dir_);
    return cache_fs_->pread(pp, buf, size, offset);
}

RecurseOption ReadOnlyCacheFileSystemBaseImplementation::recurse_helper_(Path *p,
                                                                         std::function<RecurseOption(Path *path, FileStat *stat)> cb)
{
    CacheEntry *ce = &entries_[p];
    assert(ce);
    RecurseOption ro = cb(ce->path, &ce->stat);
    if (ro == RecurseSkipSubTree || ro == RecurseStop) {
        return ro;
    }
    for (auto& p : ce->direntries) {
        if (p.second->stat.isDirectory()) {
            ro = recurse_helper_(p.second->path, cb);
            if (ro == RecurseStop) {
                return ro;
            }
        } else {
            ro = cb(p.second->path, &p.second->stat);
        }
    }
    return RecurseContinue;
}

RC ReadOnlyCacheFileSystemBaseImplementation::recurse(Path *root, function<RecurseOption(Path *path, FileStat *stat)> cb)
{
    recurse_helper_(root, cb);
    return RC::OK;
}

RC ReadOnlyCacheFileSystemBaseImplementation::recurse(Path *root, std::function<RecurseOption(const char *path, const struct stat *sb)> cb)
{
    assert(0);
    return RC::ERR;
}

RC ReadOnlyCacheFileSystemBaseImplementation::ctimeTouch(Path *p)
{
    return RC::ERR;
}

RC ReadOnlyCacheFileSystemBaseImplementation::loadVector(Path *p, size_t blocksize, std::vector<char> *buf)
{
    if (!fileCached(p)) { return RC::ERR; }
    Path *pp = p->prepend(cache_dir_);
    return cache_fs_->loadVector(pp, blocksize, buf);
}

bool ReadOnlyCacheFileSystemBaseImplementation::readLink(Path *path, string *target)
{
    fprintf(stderr, "readLink not implemented...\n");
    return false;
}

bool CacheEntry::isCached(FileSystem *cache_fs, Path *cache_dir, Path *f)
{
    Path *p = f->prepend(cache_dir);
    FileStat st;
    RC rc = cache_fs->stat(p, &st);
    if (rc.isErr()) {
        debug(CACHE, "stat (not found) \"%s\"\n", p->c_str());
        return false;
    }

    // The size must be exactly right.
    if (st.st_size == stat.st_size) {
        // All storages can handle seconds resolution.
        if (st.st_mtim.tv_sec == stat.st_mtim.tv_sec) {
            // Nanosecond match! Yay!
            if (st.st_mtim.tv_nsec == stat.st_mtim.tv_nsec) {
                return true;
            }
            if (st.st_mtim.tv_nsec/1000 == stat.st_mtim.tv_nsec/1000) {
                long remainder = st.st_mtim.tv_nsec - (st.st_mtim.tv_nsec/1000)*1000;
                if (remainder == 0) {
                    // The file in the file system has been stored
                    // somewhere that only stores micros. NTFS does this.
                    // Fine, lets utime the file to its correct value!
                    debug(CACHE, "storage truncated mtime to microseconds, fixing utime for %s\n", p->c_str());
                    cache_fs->utime(p, &stat);
                    return true;
                }
            }
            if (st.st_mtim.tv_nsec/1000000 == stat.st_mtim.tv_nsec/1000000) {
                long remainder = st.st_mtim.tv_nsec - (st.st_mtim.tv_nsec/1000000)*1000000;
                if (remainder == 0) {
                    // The file in the file system has been stored
                    // somewhere that only stores millis. Google Drive for example.
                    // Fine, lets utime the file to its correct value!
                    debug(CACHE, "storage truncated mtime to milliseconds, fixing utime for %s\n", p->c_str());
                    cache_fs->utime(p, &stat);
                    return true;
                }
            }
        }
    }
    debug(CACHE, "stat (wrong size %zu (%zu) or mtime %zd:%d (%zd:%d) ) \"%s\"\n",
          st.st_size,
          stat.st_size,
          st.st_mtim.tv_sec, st.st_mtim.tv_nsec,
          stat.st_mtim.tv_sec, stat.st_mtim.tv_nsec,
          p->c_str());
    return false;
}
