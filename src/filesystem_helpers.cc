/*
 Copyright (C) 2018-2020 Fredrik Öhrström

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
static ComponentId MAPFS = registerLogComponent("mapfs");

RC ReadOnlyFileSystem::chmod(Path *p, FileStat *fs)
{
    return RC::ERR;
}

RC ReadOnlyFileSystem::utime(Path *p, FileStat *fs)
{
    return RC::ERR;
}

Path *ReadOnlyFileSystem::userRunDir()
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
                                    std::function<size_t(off_t offset, char *buffer, size_t len)> cb,
                                    size_t buffer_size)
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

void ReadOnlyFileSystem::allowAccessTimeUpdates()
{
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
    // This should perhaps be sorted in the proper order to simlulate depth first search?
    for (auto &p : contents_)
    {
        RecurseOption ro = cb(p.first, &p.second);
        // RecurseContinue,
        // RecurseSkipSubTree,
        // RecurseStop
        if (ro == RecurseStop) break;
    }
    return RC::OK;
}

RC StatOnlyFileSystem::recurse(Path *root, std::function<RecurseOption(const char *path, const struct stat *sb)> cb)
{
    // This should perhaps be sorted in the proper order to simlulate depth first search?
    for (auto &p : contents_)
    {
        struct stat tmp;
        p.second.storeIn(&tmp);
        cb(p.first->c_str(), &tmp);
    }
    return RC::OK;
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

FILE *StatOnlyFileSystem::openAsFILE(Path *file, const char *mode)
{
    return NULL;
}

void MapFileSystem::addDirToParent(Path *dir)
{
    // We have a directory
    assert(dir);
    // Check that the dir is already added to the cache entries.
    assert(entries_.count(dir) == 1);
    // Get the dir entry.
    MapEntry *dir_entry = &entries_[dir];

    // Check if there is a parent to add it to.
    Path *parent = dir->parent();
    if (parent == NULL) return; // Nope, give up here.

    // Check if the parent is not in the entries.
    if (entries_.count(parent) == 0)
    {
        // Add the parent!
        FileStat dir_stat;
        dir_stat.setAsDirectory();
        entries_[parent] = MapEntry(dir_stat, parent, NULL);
    }
    // Get the parent entry.
    MapEntry *parent_entry = &entries_[parent];

    // Check if dir is already added to the parent.
    if (parent_entry->direntries.count(dir) == 0)
    {
        // Nope, lets add dir to parent contents.
        parent_entry->direntries[dir] = dir_entry;
    }

    // Proceed by having parent added to its parent.
    addDirToParent(parent);
}

void MapFileSystem::mapFile(FileStat stat, Path *path, Path *source)
{
    assert(path);
    assert(source);

    // Add a map entry for this file.
    entries_[path] = MapEntry(stat, path, source);
    MapEntry *file_entry = &entries_[path];

    // We should try to add the file to a directory.
    Path *dir = path->parent();
    if (dir != NULL)
    {
        if (entries_.count(dir) == 0)
        {
            FileStat dir_stat;
            dir_stat.setAsDirectory();
            entries_[dir] = MapEntry(dir_stat, dir, NULL);
            // Add this dir to its parent directory.
            addDirToParent(dir);
        }
        MapEntry *dir_entry = &entries_[dir];
        dir_entry->direntries[path] = file_entry;
    }
    debug(MAPFS, "%s sourced from %s\n", path->c_str(), source->c_str());
}

bool MapFileSystem::readdir(Path *p, vector<Path*> *vec)
{
    return false;
}

ssize_t MapFileSystem::pread(Path *p, char *buf, size_t size, off_t offset)
{
    if (entries_.count(p) == 0) return -4711;

    MapEntry *me = &entries_[p];
    return origin_fs_->pread(me->source, buf, size, offset);
}

RecurseOption MapFileSystem::recurse_helper_(Path *p,
                                             std::function<RecurseOption(Path *path, FileStat *stat)> cb)
{
    if (entries_.count(p) == 0) return RecurseContinue;
    MapEntry *me = &entries_[p];
    assert(me);
    RecurseOption ro = cb(me->path, &me->stat);
    if (ro == RecurseSkipSubTree || ro == RecurseStop) {
        return ro;
    }
    for (auto& p : me->direntries)
    {
        if (p.second->stat.isDirectory())
        {
            ro = recurse_helper_(p.second->path, cb);
            if (ro == RecurseStop)
            {
                return ro;
            }
        }
        else
        {
            ro = cb(p.second->path, &p.second->stat);
        }
    }
    return RecurseContinue;
}

RC MapFileSystem::recurse(Path *root, function<RecurseOption(Path *path, FileStat *stat)> cb)
{
    recurse_helper_(root, cb);
    return RC::OK;
}

RC MapFileSystem::recurse(Path *root, std::function<RecurseOption(const char *path, const struct stat *sb)> cb)
{
    assert(0);
    return RC::ERR;
}

RC MapFileSystem::ctimeTouch(Path *p)
{
    return RC::ERR;
}

RC MapFileSystem::stat(Path *p, FileStat *fs)
{
    if (entries_.count(p) == 0) return RC::ERR;

    *fs = entries_[p].stat;
    return RC::OK;
}

RC MapFileSystem::loadVector(Path *file, size_t blocksize, std::vector<char> *buf)
{
    return RC::OK;
}

bool MapFileSystem::readLink(Path *file, string *target)
{
    return false;
}

FILE *MapFileSystem::openAsFILE(Path *file, const char *mode)
{
    return NULL;
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
