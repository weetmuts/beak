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

Path *ReadOnlyFileSystem::mkTempFile(string prefix, string content)
{
    return NULL;
}

Path *ReadOnlyFileSystem::mkTempDir(string prefix)
{
    return NULL;
}

Path *ReadOnlyFileSystem::mkDir(Path *p, string name)
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

void StatOnlyFileSystem::recurse(Path *root, std::function<void(Path *path, FileStat *stat)> cb)
{
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
    return RC::ERR;
}

bool ReadOnlyCacheFileSystemBaseImplementation::readdir(Path *p, vector<Path*> *vec)
{
    if (entries_.count(p) == 0) {
        return false;
    }
    CacheEntry *ce = &entries_[p];
    for (CacheEntry *e : ce->direntries) {
        vec->push_back(e->path);
    }
    return true;
}

bool ReadOnlyCacheFileSystemBaseImplementation::fileCached(Path *p)
{
    error(CACHE, "Could not fetch file for caching!");
    return true;
}

CacheEntry *ReadOnlyCacheFileSystemBaseImplementation::cacheEntry(Path *p)
{
    if (entries_.count(p) == 0) return NULL;
    return &entries_[p];
}

ssize_t ReadOnlyCacheFileSystemBaseImplementation::pread(Path *p, char *buf, size_t size, off_t offset)
{
    if (!fileCached(p)) {  return -1; }

    return cache_fs_->pread(p, buf, size, offset);
}

void ReadOnlyCacheFileSystemBaseImplementation::recurse(Path *root, function<void(Path *path, FileStat *stat)> cb)
{
}


RC ReadOnlyCacheFileSystemBaseImplementation::loadVector(Path *file, size_t blocksize, std::vector<char> *buf)
{
    return RC::ERR;
}

bool ReadOnlyCacheFileSystemBaseImplementation::readLink(Path *path, string *target)
{
    return false;
}
