/*
 Copyright (C) 2016-2018 Fredrik Öhrström

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

#include "restore.h"

#include "beak.h"
#include "filesystem.h"
#include "index.h"
#include "lock.h"
#include "tarfile.h"

#include <algorithm>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <memory>

#include "log.h"
#include "tarentry.h"

using namespace std;

ComponentId RESTORE = registerLogComponent("restore");

struct RestoreFileSystem : FileSystem
{
    Restore *rev_;
    PointInTime *point_;

    bool readdir(Path *p, std::vector<Path*> *vec)
    {
        return false;
    }

    ssize_t pread(Path *p, char *buf, size_t size, off_t offset)
    {
        return 0;
    }

    void recurseInto(RestoreEntry *d, std::function<void(Path*,FileStat*)> cb)
    {
        rev_->loadDirContents(point_, d->path);

        // Recurse depth first.
        for (auto e : d->dir) {
            if (e->fs.isDirectory()) {
                recurseInto(e, cb);
                cb(e->path, &e->fs);
            }
        }
        for (auto e : d->dir) {
            if (!e->fs.isDirectory()) {
                cb(e->path, &e->fs);
            }
        }
    }

    RC recurse(Path *root, std::function<RecurseOption(Path *path, FileStat *stat)> cb)
    {
        point_ = rev_->singlePointInTime();
        assert(point_);

        RestoreEntry *d = rev_->findEntry(point_, Path::lookupRoot());
        assert(d);
        recurseInto(d, cb);
        return RC::OK;
    }

    RC recurse(Path *root, std::function<RecurseOption(const char *path, const struct stat *sb)> cb)
    {
        return recurse(root, [=](Path *p, FileStat *st) {
                struct stat sb;
                st->storeIn(&sb);
                return cb(p->c_str(), &sb);
            });
    }

    RC ctimeTouch(Path *p)
    {
        return RC::ERR;
    }

    RC stat(Path *p, FileStat *fs)
    {
        return RC::ERR;
    }

    RC chmod(Path *p, FileStat *fs)
    {
        return RC::ERR;
    }

    RC utime(Path *p, FileStat *fs)
    {
        return RC::ERR;
    }

    Path *mkTempFile(std::string prefix, std::string content)
    {
        return NULL;
    }

    Path *mkTempDir(std::string prefix)
    {
        return NULL;
    }

    Path *mkDir(Path *p, std::string name)
    {
        return NULL;
    }

    RC rmDir(Path *p)
    {
        return RC::ERR;
    }

    RC loadVector(Path *file, size_t blocksize, std::vector<char> *buf)
    {
        return RC::OK;
    }

    RC createFile(Path *file, std::vector<char> *buf)
    {
        return RC::ERR;
    }
    bool createFile(Path *path, FileStat *stat,
                     std::function<size_t(off_t offset, char *buffer, size_t len)> cb)
    {
        return false;
    }

    bool createSymbolicLink(Path *file, FileStat *stat, string target)
    {
        return false;
    }

    bool createHardLink(Path *file, FileStat *stat, Path *target)
    {
        return false;
    }

    bool createFIFO(Path *file, FileStat *stat)
    {
        return false;
    }

    bool readLink(Path *file, string *target)
    {
        return false;
    }

    bool deleteFile(Path *file)
    {
        return false;
    }

    RC mountDaemon(Path *dir, FuseAPI *fuseapi, bool foreground=false, bool debug=false)
    {
        return RC::ERR;
    }

    unique_ptr<FuseMount> mount(Path *dir, FuseAPI *fuseapi, bool debug=false)
    {
        return NULL;
    }

    RC umount(ptr<FuseMount> fuse_mount)
    {
        return RC::ERR;
    }

    RC enableWatch()
    {
        return RC::ERR;
    }

    RC addWatch(Path *dir)
    {
        return RC::ERR;
    }

    int endWatch()
    {
        return 0;
    }

    RestoreFileSystem(Restore *rev) : FileSystem("RestoreFileSystem"), rev_(rev) { }
};

Restore::Restore(FileSystem *backup_fs)
{
    single_point_in_time_ = NULL;
    pthread_mutexattr_init(&global_attr);
    pthread_mutexattr_settype(&global_attr, PTHREAD_MUTEX_RECURSIVE);
    pthread_mutex_init(&global, &global_attr);
    backup_fs_ = backup_fs;
    contents_fs_ = unique_ptr<FileSystem>(new RestoreFileSystem(this));
}

// The gz file to load, and the dir to populate with its contents.
bool Restore::loadGz(PointInTime *point, Path *gz, Path *dir_to_prepend)
{
    RC rc = RC::OK;
    if (point->hasLoadedGzFile(gz))
    {
        return true;
    }
    point->addLoadedGzFile(gz);

    vector<char> buf;
    rc = backup_fs_->loadVector(gz, T_BLOCKSIZE, &buf);
    if (rc.isErr()) return false;

    vector<char> contents;
    gunzipit(&buf, &contents);
    auto i = contents.begin();

    debug(RESTORE, "parsing %s for files in %s\n", gz->c_str(), dir_to_prepend->c_str());
    struct IndexEntry index_entry;
    struct IndexTar index_tar;

    vector<RestoreEntry*> es;
    bool parsed_tars_already = point->hasGzFiles();

    rc = Index::loadIndex(contents, i, &index_entry, &index_tar, dir_to_prepend,
             [this,point,&es,dir_to_prepend](IndexEntry *ie){
                         if (!point->hasPath(ie->path)) {
                             debug(RESTORE, "adding entry for >%s< %p\n", ie->path->c_str());
                             // Trigger storage of entry.
                             point->addPath(ie->path);
                         } else {
                             debug(RESTORE, "using existing entry for >%s< %p\n", ie->path->c_str());
                         }
                         RestoreEntry *e = point->getPath(ie->path);
                         assert(e->path = ie->path);
                         e->loadFromIndex(ie);
                         if (ie->is_hard_link) {
                             // A Hard link as stored in the beakfs >must< point to a file
                             // in the same directory or to a file in subdirectory.
                             e->fs.hard_link = dir_to_prepend->append(ie->link);
                         }
                         es.push_back(e);
                     },
                     [this,point,parsed_tars_already](IndexTar *it)
                          {
                              if (!parsed_tars_already)
                              {
                                  Path *p = it->path->prepend(Path::lookupRoot());
                                  if (p->name()->c_str()[0] == REG_FILE_CHAR)
                                  {
                                      point->addGzFile(p->parent(), p);
                                  }
                                  point->addTar(p);
                              }
                          });

    if (rc.isErr())
    {
        failure(RESTORE, "Could not parse the index file %s\n", gz->c_str());
        return false;
    }

    for (auto i : es)
    {
        // Now iterate over the files found.
        // Some of them might be in subdirectories.
        Path *p = i->path;
        if (!p->parent()) continue;
        Path *pp = p->parent();
        RestoreEntry *d = point->getPath(pp);
        if (d == NULL)
        {
            d = point->addPath(pp);
            d->path = pp;
        }
        debug(RESTORE, "added %s %p to dir >%s< %p\n", i->path->c_str(), i, pp->c_str(), d);
        d->dir.push_back(i);
        d->loaded = true;
    }

    debug(RESTORE, "found proper index file! %s\n", gz->c_str());

    return true;
}

Path *Restore::loadDirContents(PointInTime *point, Path *path)
{
    FileStat stat;
    Path *gz = point->getGzFile(path);
    debug(RESTORE, "looking for index file in dir >%s< (found %p)\n", path->c_str(), gz);
    if (gz != NULL)
    {
        gz = gz->prepend(rootDir());
        RC rc = backup_fs_->stat(gz, &stat);
        debug(RESTORE, "%s --- rc=%d %d\n", gz->c_str(), rc.toInteger(), stat.isRegularFile());
        if (rc.isOk() && stat.isRegularFile()) {
            // Found a gz file!
            debug(RESTORE, "found a gz file %s for %s\n", gz->c_str(), path->c_str());
            loadGz(point, gz, path);
        }
    }
    else
    {
        debug(RESTORE, "no gz file found %s\n", path->c_str());
    }

    return gz;
}

void Restore::loadCache(PointInTime *point, Path *path)
{
//    Path *opath = path;

    RestoreEntry *e = point->getPath(path);
    if (e != NULL && e->loaded)
    {
        return;
    }

    debug(RESTORE, "load cache for '%s'\n", path->c_str());
    // Walk up in the directory structure until a gz file is found.
    for (;;)
    {
        Path *gz = loadDirContents(point, path);
        if (gz != NULL)
        {
            if (point->hasPath(path))
            {
                if (path == Path::lookupRoot())
                {
                    debug(RESTORE, "reached root\n");
                    return;
                }
                // Success
                debug(RESTORE, "found '%s' in index '%s'\n", path->c_str(), gz->c_str());
                return;
            }
            // Can we terminate this search early?
        }
        if (path->isRoot()) {
            // No gz file found anywhere! This filesystem should not have been mounted!
            debug(RESTORE, "no index file found anywhere!\n");
            return;
        }
        // Move up in the directory tree.
        path = path->parent();
        debug(RESTORE, "moving up to %s\n", path->c_str());
    }
    assert(0);
}

RestoreEntry *Restore::findEntry(PointInTime *point, Path *path)
{
    if (!point->hasPath(path))
    {
        // No cache index loaded for this path, try to load.
        loadCache(point, path);
        if (!point->hasPath(path))
        {
            // Still no index loaded for the path, ie it does not exist.
            debug(RESTORE, "not found '%s'\n", path->c_str());
            return NULL;
        }
    }

    return point->getPath(path);
}

struct RestoreFuseAPI : FuseAPI
{
    Restore *restore_;

    RestoreFuseAPI(Restore *r) : restore_(r) {}

    int getattrCB(const char *path_char_string, struct stat *stbuf)
    {
        debug(RESTORE, "getattr '%s'\n", path_char_string);

        LOCK(&restore_->global);

        string path_string = path_char_string;
        Path *path = Path::lookup(path_string);
        RestoreEntry *e;
        PointInTime *point;

        if (path->depth() == 1)
        {
            memset(stbuf, 0, sizeof(struct stat));
            stbuf->st_mode = S_IFDIR | S_IRUSR | S_IXUSR;
            stbuf->st_nlink = 2;
            stbuf->st_size = 0;
            stbuf->st_uid = geteuid();
            stbuf->st_gid = getegid();
#if HAS_ST_MTIM
            stbuf->st_mtim.tv_sec = restore_->mostRecentPointInTime()->ts()->tv_sec;
            stbuf->st_mtim.tv_nsec = restore_->mostRecentPointInTime()->ts()->tv_nsec;
            stbuf->st_atim.tv_sec = restore_->mostRecentPointInTime()->ts()->tv_sec;
            stbuf->st_atim.tv_nsec = restore_->mostRecentPointInTime()->ts()->tv_nsec;
            stbuf->st_ctim.tv_sec = restore_->mostRecentPointInTime()->ts()->tv_sec;
            stbuf->st_ctim.tv_nsec = restore_->mostRecentPointInTime()->ts()->tv_nsec;
#elif HAS_ST_MTIME
            stbuf->st_mtime = restore_->mostRecentPointInTime()->ts()->tv_sec;
            stbuf->st_atime = restore_->mostRecentPointInTime()->ts()->tv_sec;
            stbuf->st_ctime = restore_->mostRecentPointInTime()->ts()->tv_sec;
#else
#error
#endif

            goto ok;
        }

        point = restore_->singlePointInTime();
        if (!point) {
            Path *root = path->subpath(1,1);
            point = restore_->findPointInTime(root->str());
            if (!point) goto err;
            if (path->depth() == 2) {
                // We are getting the attributes for the virtual point_in_time directory.
                memset(stbuf, 0, sizeof(struct stat));
                stbuf->st_mode = S_IFDIR | S_IRUSR | S_IXUSR;
                stbuf->st_nlink = 2;
                stbuf->st_size = 0;
                stbuf->st_uid = geteuid();
                stbuf->st_gid = getegid();
#if HAS_ST_MTIM
                stbuf->st_mtim.tv_sec = point->ts()->tv_sec;
                stbuf->st_mtim.tv_nsec = point->ts()->tv_nsec;
                stbuf->st_atim.tv_sec = point->ts()->tv_sec;
                stbuf->st_atim.tv_nsec = point->ts()->tv_nsec;
                stbuf->st_ctim.tv_sec = point->ts()->tv_sec;
                stbuf->st_ctim.tv_nsec = point->ts()->tv_nsec;
#elif HAS_ST_MTIME
                stbuf->st_mtime = point->ts()->tv_sec;
                stbuf->st_atime = point->ts()->tv_sec;
                stbuf->st_ctime = point->ts()->tv_sec;
#else
#error
#endif
                goto ok;
            }
            if (path->depth() > 2) {
                path = path->subpath(2)->prepend(Path::lookupRoot());
            }
        }

        e = restore_->findEntry(point, path);
        if (!e) goto err;

        memset(stbuf, 0, sizeof(struct stat));

        if (e->fs.isDirectory())
        {
            stbuf->st_mode = e->fs.st_mode;
            stbuf->st_nlink = 2;
            stbuf->st_size = e->fs.st_size;
            stbuf->st_uid = e->fs.st_uid;
            stbuf->st_gid = e->fs.st_gid;
#if HAS_ST_MTIM
            stbuf->st_mtim.tv_sec = e->fs.st_mtim.tv_sec;
            stbuf->st_mtim.tv_nsec = e->fs.st_mtim.tv_nsec;
            stbuf->st_atim.tv_sec = e->fs.st_mtim.tv_sec;
            stbuf->st_atim.tv_nsec = e->fs.st_mtim.tv_nsec;
            stbuf->st_ctim.tv_sec = e->fs.st_mtim.tv_sec;
            stbuf->st_ctim.tv_nsec = e->fs.st_mtim.tv_nsec;
#elif HAS_ST_MTIME
            stbuf->st_mtime = e->fs.st_mtim.tv_sec;
            stbuf->st_atime = e->fs.st_mtim.tv_sec;
            stbuf->st_ctime = e->fs.st_mtim.tv_sec;
#else
#error
#endif
            goto ok;
        }

        stbuf->st_mode = e->fs.st_mode;
        stbuf->st_nlink = 1;
        stbuf->st_size = e->fs.st_size;
        stbuf->st_uid = e->fs.st_uid;
        stbuf->st_gid = e->fs.st_gid;
#if HAS_ST_MTIM
        stbuf->st_mtim.tv_sec = e->fs.st_mtim.tv_sec;
        stbuf->st_mtim.tv_nsec = e->fs.st_mtim.tv_nsec;
        stbuf->st_atim.tv_sec = e->fs.st_mtim.tv_sec;
        stbuf->st_atim.tv_nsec = e->fs.st_mtim.tv_nsec;
        stbuf->st_ctim.tv_sec = e->fs.st_mtim.tv_sec;
        stbuf->st_ctim.tv_nsec = e->fs.st_mtim.tv_nsec;
#elif HAS_ST_MTIME
        stbuf->st_mtime = e->fs.st_mtim.tv_sec;
        stbuf->st_atime = e->fs.st_mtim.tv_sec;
        stbuf->st_ctime = e->fs.st_mtim.tv_sec;
#else
#error
#endif
        stbuf->st_rdev = e->fs.st_rdev;
        goto ok;

    err:

        UNLOCK(&restore_->global);
        return -ENOENT;

    ok:

        UNLOCK(&restore_->global);
        return 0;
    }

    int readdirCB(const char *path_char_string, void *buf,
                  fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
    {
        debug(RESTORE, "readdir '%s'\n", path_char_string);

        LOCK(&restore_->global);

        string path_string = path_char_string;
        Path *path = Path::lookup(path_string);
        RestoreEntry *e;
        PointInTime *point = restore_->singlePointInTime();

        if (!point) {
            if (path->depth() == 1) {
                filler(buf, ".", NULL, 0);
                filler(buf, "..", NULL, 0);

                for (auto &p : restore_->history())
                {
                    char filename[256];
                    memset(filename, 0, 256);
                    snprintf(filename, 255, "%s", p.direntry.c_str());
                    filler(buf, filename, NULL, 0);
                }
                goto ok;
            }

            Path *pnt_dir = path->subpath(1,1);
            point = restore_->findPointInTime(pnt_dir->str());
            if (!point) goto err;
            path = path->subpath(2)->prepend(Path::lookupRoot());
        }

        e = restore_->findEntry(point, path);
        if (!e) goto err;

        if (!e->fs.isDirectory()) goto err;

        if (!e->loaded) {
            debug(RESTORE,"not loaded %s\n", e->path->c_str());
            restore_->loadCache(point, e->path);
        }
        filler(buf, ".", NULL, 0);
        filler(buf, "..", NULL, 0);

        for (auto i : e->dir)
        {
            char filename[256];
            memset(filename, 0, 256);
            snprintf(filename, 255, "%s", i->path->name()->c_str());
            filler(buf, filename, NULL, 0);
        }
        goto ok;

    err:

        UNLOCK(&restore_->global);
        return -ENOENT;

    ok:

        UNLOCK(&restore_->global);
        return 0;
    }

    int readlinkCB(const char *path_char_string, char *buf, size_t s)
    {
        debug(RESTORE, "readlink %s\n", path_char_string);

        LOCK(&restore_->global);

        string path_string = path_char_string;
        Path *path = Path::lookup(path_string);
        size_t c;
        RestoreEntry *e;
        PointInTime *point = restore_->singlePointInTime();
        if (!point) {
            Path *pnt_dir = path->subpath(1,1);
            point = restore_->findPointInTime(pnt_dir->c_str());
            if (!point) goto err;
            path = path->subpath(2)->prepend(Path::lookupRoot());
        }
        e = restore_->findEntry(point, path);
        if (!e) goto err;

        c = e->symlink.length();
        if (c > s) c = s;

        memcpy(buf, e->symlink.c_str(), c);
        buf[c] = 0;
        debug(RESTORE, "readlink %s bufsiz=%ju returns buf=>%s<\n", path, s, buf);

        goto ok;

    err:

        UNLOCK(&restore_->global);
        return -ENOENT;

    ok:

        UNLOCK(&restore_->global);
        return 0;
    }

    int readCB(const char *path_char_string, char *buf,
               size_t size, off_t offset_, struct fuse_file_info *fi)
    {
        debug(RESTORE, "read '%s' offset=%ju size=%ju\n", path_char_string, offset_, size);

        LOCK(&restore_->global);

        int n = 0;
        off_t file_offset = offset_;
        string path_string = path_char_string;
        Path *path = Path::lookup(path_string);

        RestoreEntry *e;
        Path *tar;
        TarFileName tfn;
        PointInTime *point = restore_->singlePointInTime();
        if (!point)
        {
            Path *pnt_dir = path->subpath(1,1);
            point = restore_->findPointInTime(pnt_dir->str());
            if (!point) goto err;
            path = path->subpath(2)->prepend(Path::lookupRoot());
        }

        e = restore_->findEntry(point, path);
        if (!e) goto err;

        tar = e->tar->prepend(restore_->rootDir());
        if (!tfn.parseFileName(tar->str())) {
            debug(RESTORE, "bad tar file name '%s'\n", tar->c_str());
            goto err;
        }

        if (file_offset > e->fs.st_size)
        {
            // Read outside of file size
            goto ok;
        }

        if (file_offset + (off_t)size > e->fs.st_size)
        {
            // Shrink actual read to fit file.
            size = e->fs.st_size - file_offset;
        }

        if (e->num_parts == 1)
        {
            // Offset into a single tar file.
            file_offset += e->offset_;
            debug(RESTORE, "reading %ju bytes from offset %ju in file %s\n", size, file_offset, tar->c_str());
            n = restore_->backupFileSystem()->pread(tar, buf, size, file_offset);
            if (n == -1)
            {
                failure(RESTORE,
                        "Could not read (1) from file >%s< in underlying filesystem err %d\n",
                        tar->c_str(), errno);
                goto err;
            }
        }
        else
        {
            // There is more than one part
            n =  e->readParts(file_offset, buf, size,
                      [&](uint partnr, off_t offset_inside_part, char *buffer, size_t length_to_read)
                      {
                          char name[4096];
                          tfn.size = e->part_size;
                          tfn.last_size = e->last_part_size;
                          tfn.part_nr = partnr;
                          tfn.num_parts = e->num_parts;
                          Path *dir = e->path->parent()->prepend(restore_->rootDir());
                          tfn.writeTarFileNameIntoBuffer(name, sizeof(name), dir);
                          Path *tarf = Path::lookup(name);
                          assert(length_to_read > 0);
                          debug(RESTORE, "reading %ju bytes from offset %ju in tar part %s\n",
                                length_to_read, offset_inside_part, tarf->c_str());
                          int nn = restore_->backupFileSystem()->pread(tarf, buffer, length_to_read, offset_inside_part);
                          if (nn <= 0)
                          {
                              failure(RESTORE,
                                      "Could not read (2) from file >%s< in underlying filesystem err %d\n",
                                      tarf->c_str(), errno);
                              return 0;
                          }
                          return nn;
                      });
        }
    ok:

        UNLOCK(&restore_->global);
        return n;

    err:

        UNLOCK(&restore_->global);
        return -ENOENT;
    }
};

RC Restore::lookForPointsInTime(PointInTimeFormat f, Path *path)
{
    bool ok;
    if (path == NULL) return RC::ERR;

    vector<Path*> contents;
    if (!backup_fs_->readdir(path, &contents)) {
        return RC::ERR;
    }
    for (auto f : contents)
    {
        TarFileName tfn;
        ok = tfn.parseFileName(f->str());

        if (ok && tfn.type == REG_FILE)
        {
            PointInTime p(tfn.sec, tfn.nsec);;
            char datetime[20];
            memset(datetime, 0, sizeof(datetime));
            strftime(datetime, 20, "%Y-%m-%d_%H:%M", localtime(&p.ts()->tv_sec));

            p.ago = timeAgo(p.ts());
            p.datetime = datetime;
            p.filename = f->str();
            history_.push_back(p);
            debug(RESTORE, "found index file %s\n", f->c_str());
        }
    }

    if (history_.size() == 0) {
        return RC::ERR;
    }
    sort(history_.begin(), history_.end(),
              [](PointInTime &a, PointInTime &b)->bool {
                  return (b.ts()->tv_sec < a.ts()->tv_sec) ||
                      (b.ts()->tv_sec == a.ts()->tv_sec &&
                       b.ts()->tv_nsec < a.ts()->tv_nsec);
                      });

    most_recent_point_in_time_ = &history_[0];
    int i = 0;
    for (auto &point : history_) {
        point.key = i;
        string de = string("@")+to_string(i)+" ";
        i++;
        if (f == absolute_point) {
            // Drop the relative @ prefix here.
            de = point.datetime;
        } else if (f == relative_point) {
            de += point.ago;
        } else {
            de += point.datetime+" "+point.ago;
        }
        point.direntry = de;
        points_in_time_[point.direntry] = &point;
        FileStat fs;
        fs.st_mode = S_IFDIR | S_IRUSR | S_IXUSR;
        point.addPath(Path::lookupRoot());
        RestoreEntry *re = point.getPath(Path::lookupRoot());
        *re = RestoreEntry(fs, 0, Path::lookupRoot());
    }

    if (i > 0) {
        return RC::OK;
    }
    return RC::ERR;
}

PointInTime *Restore::findPointInTime(string s) {
    return points_in_time_[s];
}

PointInTime *Restore::setPointInTime(string g) {
    if ((g.length() < 2) | (g[0] != '@')) {
        error(RESTORE,"Specify generation as @0 @1 @2 etc.\n");
    }
    for (size_t i=1; i<g.length(); ++i) {
        if (g[i] < '0' || g[i] > '9') {
            return NULL;
        }
    }
    g = g.substr(1);
    size_t gg = atoi(g.c_str());
    if (gg >= history_.size()) {
        return NULL;
    }
    single_point_in_time_ = &history_[gg];
    return single_point_in_time_;
}

RC Restore::loadBeakFileSystem(Argument *storage)
{
    setRootDir(storage->storage->storage_location);

    for (auto &point : history()) {
        string name = point.filename;
        debug(RESTORE,"found backup for %s filename %s\n", point.ago.c_str(), name.c_str());

        // Check that it is a proper file.
        FileStat stat;
        Path *gz = Path::lookup(rootDir()->str() + "/" + name);

        RC rc = backup_fs_->stat(gz, &stat);
        if (rc.isErr() || !stat.isRegularFile())
        {
            error(RESTORE, "Not a regular file %s\n", gz->c_str());
        }

        // Populate the list of all tars from the root index file.
        bool ok = loadGz(&point, gz, Path::lookupRoot());
        if (!ok) {
            error(RESTORE, "Could not load index file for backup %s!\n", point.ago.c_str());
        }

        // Populate the root directory with its contents.
        loadCache(&point, Path::lookupRoot());

        RestoreEntry *e = findEntry(&point, Path::lookupRoot());
        assert(e != NULL);

        // Look for the youngest timestamp inside root to
        // be used as the timestamp for the root directory.
        // The root directory is by definition not defined inside gz file.
        time_t youngest_secs = 0, youngest_nanos = 0;
        for (auto i : e->dir)
        {
            if (i->fs.st_mtim.tv_sec > youngest_secs ||
                (i->fs.st_mtim.tv_sec == youngest_secs &&
                 i->fs.st_mtim.tv_nsec > youngest_nanos))
            {
                youngest_secs = i->fs.st_mtim.tv_sec;
                youngest_nanos = i->fs.st_mtim.tv_nsec;
            }
        }
        e->fs.st_mtim.tv_sec = youngest_secs;
        e->fs.st_mtim.tv_nsec = youngest_nanos;
    }
    return RC::OK;
}

FuseAPI *Restore::asFuseAPI()
{
    return new RestoreFuseAPI(this);
}

unique_ptr<Restore> newRestore(ptr<FileSystem> backup_fs)
{
    return unique_ptr<Restore>(new Restore(backup_fs));
}

void RestoreEntry::loadFromIndex(IndexEntry *ie)
{
    fs = ie->fs;
    offset_ = ie->offset;
    path = ie->path;
    is_sym_link = ie->is_sym_link;
    symlink = ie->link;
    tar = Path::lookup(ie->tar);
    num_parts = ie->num_parts;
    part_offset = ie->part_offset;
    part_size = ie->part_size;
    last_part_size = ie->last_part_size;
}

bool RestoreEntry::findPartContainingOffset(size_t file_offset, uint *partnr, size_t *offset_inside_part)
{
    // The first file header HHHH can be longer than the part header hh
    // The size of the parts are always the same, except for the last part.
    // HHHH ffff
    // hh ffffff
    // hh ffffff
    // hh fff

    //fprintf(stdout, "\n\nfile_offset=%zu ====>\n", file_offset);
    if ((size_t)file_offset < part_size)
    {
        // We are in the first part:
        *partnr = 0;
        *offset_inside_part = file_offset;
        //fprintf(stdout, "first part partnr=0 part_offset=%zu\n", *offset_inside_part);
        return true;
    }
    // Remove the first part.
    file_offset -= part_size;
    //fprintf(stdout, "adjusted file_offset=%zu\n", file_offset);
    //size_t first_part_data_size = part_size - part_offset;
    //fprintf(stdout, "first_part_data_size=%zu\n", first_part_data_size);
    // Now all parts have the same part_offset
    size_t part_data_size = part_size - part_offset;
    //fprintf(stdout, "part_data_size=%zu\n", part_data_size);

    uint n = file_offset/part_data_size;
    //fprintf(stdout, "n=%u\n", n);
    size_t remainder = file_offset - n*part_data_size;
    //fprintf(stdout, "remainder=%zu\n", remainder);
    *offset_inside_part = remainder + part_offset;
    *partnr = n+1;
    //fprintf(stdout, "====> partnr=%u offset=%zu\n", *partnr, *offset_inside_part);
    return true;
}

size_t RestoreEntry::lengthOfPart(uint partnr)
{
    if (partnr == num_parts-1)
    {
        return last_part_size;
    }
    return part_size;
}

ssize_t RestoreEntry::readParts(off_t file_offset, char *buffer, size_t length,
                                function<ssize_t(uint partnr, off_t part_offset, char *buffer, size_t length)> cb)
{
    ssize_t n = 0;
    // First adjust the file internal offset to skip the tar offset for the whole file,
    // which is stored inside the first part.
    file_offset += offset_;
    while (length > 0)
    {
        uint partnr;
        size_t offset_inside_part;
        //fprintf(stdout, "\n\nLength to read %zu", length);
        findPartContainingOffset(file_offset, &partnr, &offset_inside_part);
        size_t length_of_part = lengthOfPart(partnr);
        size_t length_to_read = length;
        if (length_to_read+offset_inside_part > length_of_part)
        {
            length_to_read = length_of_part-offset_inside_part;
        }
        assert(length_to_read > 0);
        ssize_t nn = cb(partnr, offset_inside_part, buffer, length_to_read);
        if (nn <= 0)
        {
            break;
        }
        n += nn;
        length -= length_to_read;
        file_offset += length_to_read;
        buffer += length_to_read;
    }
    return n;
}
