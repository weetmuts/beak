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

    RC listFilesBelow(Path *p, std::vector<Path*> *files, SortOrder so)
    {
        // TODO
        return RC::ERR;
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
    if (point->loaded_gz_files_.count(gz) == 1)
    {
        return true;
    }
    point->loaded_gz_files_.insert(gz);

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
    bool parsed_tars_already = point->gz_files_.size() != 0;

    rc = Index::loadIndex(contents, i, &index_entry, &index_tar, dir_to_prepend,
             [this,point,&es,dir_to_prepend](IndexEntry *ie){
                         if (point->entries_.count(ie->path) == 0) {
                             debug(RESTORE, "adding entry for >%s< %p\n", ie->path->c_str());
                             // Trigger storage of entry.
                             point->entries_[ie->path];
                         } else {
                             debug(RESTORE, "using existing entry for >%s< %p\n", ie->path->c_str());
                         }
                         RestoreEntry *e = &(point->entries_)[ie->path];
                         assert(e->path = ie->path);
                         e->fs = ie->fs;
                         e->offset = ie->offset;
                         e->path = ie->path;
                         e->is_sym_link = ie->is_sym_link;
                         e->symlink = ie->link;
                         e->is_hard_link = ie->is_hard_link;
                         if (e->is_hard_link) {
                             // A Hard link as stored in the beakfs >must< point to a file
                             // in the same directory or to a file in subdirectory.
                             e->hard_link = dir_to_prepend->append(ie->link);
                         }
                         e->tar = Path::lookup(ie->tar);
                         es.push_back(e);
                     },
                     [this,point,parsed_tars_already](IndexTar *it){
                         if (!parsed_tars_already) {
                             Path *p = it->path->prepend(Path::lookupRoot());
                             if (p->name()->c_str()[0] == REG_FILE_CHAR) {
                                 point->gz_files_[p->parent()] = p;
                             }
                         }
                     });

    if (rc.isErr()) {
        failure(RESTORE, "Could not parse the index file %s\n", gz->c_str());
        return false;
    }

    for (auto i : es) {
        // Now iterate over the files found.
        // Some of them might be in subdirectories.
        Path *p = i->path;
        if (!p->parent()) continue;
        Path *pp = p->parent();
        int c = point->entries_.count(pp);
        RestoreEntry *d = &(point->entries_)[pp];
        if (c == 0) {
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
    Path *gz = point->gz_files_[path];
    debug(RESTORE, "looking for index file in dir >%s< (found %p)\n", path->c_str(), gz);
    if (gz != NULL) {
        gz = gz->prepend(rootDir());
        RC rc = backup_fs_->stat(gz, &stat);
        debug(RESTORE, "%s --- rc=%d %d\n", gz->c_str(), rc.toInteger(), stat.isRegularFile());
        if (rc.isOk() && stat.isRegularFile()) {
            // Found a gz file!
            loadGz(point, gz, path);
        }
    }
    return gz;
}

void Restore::loadCache(PointInTime *point, Path *path)
{
    Path *opath = path;

    if (point->entries_.count(path) == 1) {
        RestoreEntry *e = &(point->entries_)[path];
        if (e->loaded) {
            return;
        }
    }

    debug(RESTORE, "load cache for '%s'\n", path->c_str());
    // Walk up in the directory structure until a gz file is found.
    for (;;)
    {
        Path *gz = loadDirContents(point, path);
        if (point->entries_.count(path) == 1) {
            // Success
            debug(RESTORE, "found '%s' in index '%s'\n", path->c_str(), gz->c_str());
            return;
        }
        if (path != opath) {
            // The file, if it exists should have been found here. Therefore we
            // conclude that the file does not exist.
            debug(RESTORE, "NOT found %s in index %s\n", path->c_str(), gz->c_str());
            return;
        }
        if (path->isRoot()) {
            // No gz file found anywhere! This filesystem should not have been mounted!
            debug(RESTORE, "no index file found anywhere!\n");
            return;
        }
        // Move up in the directory tree.
        path = path->parent();
    }
    assert(0);
}

RestoreEntry *Restore::findEntry(PointInTime *point, Path *path)
{
    if (point->entries_.count(path) == 0)
    {
        loadCache(point, path);
        if (point->entries_.count(path) == 0)
        {
            debug(RESTORE, "not found '%s'\n", path->c_str());
            return NULL;
        }
    }

    return &(point->entries_)[path];
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
            stbuf->st_mtim.tv_sec = restore_->mostRecentPointInTime()->ts.tv_sec;
            stbuf->st_mtim.tv_nsec = restore_->mostRecentPointInTime()->ts.tv_nsec;
            stbuf->st_atim.tv_sec = restore_->mostRecentPointInTime()->ts.tv_sec;
            stbuf->st_atim.tv_nsec = restore_->mostRecentPointInTime()->ts.tv_nsec;
            stbuf->st_ctim.tv_sec = restore_->mostRecentPointInTime()->ts.tv_sec;
            stbuf->st_ctim.tv_nsec = restore_->mostRecentPointInTime()->ts.tv_nsec;
#elif HAS_ST_MTIME
            stbuf->st_mtime = restore_->mostRecentPointInTime()->ts.tv_sec;
            stbuf->st_atime = restore_->mostRecentPointInTime()->ts.tv_sec;
            stbuf->st_ctime = restore_->mostRecentPointInTime()->ts.tv_sec;
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
                stbuf->st_mtim.tv_sec = point->ts.tv_sec;
                stbuf->st_mtim.tv_nsec = point->ts.tv_nsec;
                stbuf->st_atim.tv_sec = point->ts.tv_sec;
                stbuf->st_atim.tv_nsec = point->ts.tv_nsec;
                stbuf->st_ctim.tv_sec = point->ts.tv_sec;
                stbuf->st_ctim.tv_nsec = point->ts.tv_nsec;
#elif HAS_ST_MTIME
                stbuf->st_mtime = point->ts.tv_sec;
                stbuf->st_atime = point->ts.tv_sec;
                stbuf->st_ctime = point->ts.tv_sec;
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

        off_t offset = offset_;
        int rc = 0;
        string path_string = path_char_string;
        Path *path = Path::lookup(path_string);

        RestoreEntry *e;
        Path *tar;

        PointInTime *point = restore_->singlePointInTime();
        if (!point) {
            Path *pnt_dir = path->subpath(1,1);
            point = restore_->findPointInTime(pnt_dir->str());
            if (!point) goto err;
            path = path->subpath(2)->prepend(Path::lookupRoot());
        }

        e = restore_->findEntry(point, path);
        if (!e) goto err;

        tar = e->tar->prepend(restore_->rootDir());

        if (offset > e->fs.st_size)
        {
            // Read outside of file size
            rc = 0;
            goto ok;
        }

        if (offset + (off_t)size > e->fs.st_size)
        {
            // Shrink actual read to fit file.
            size = e->fs.st_size - offset;
        }

        // Offset into tar file.
        offset += e->offset;

        debug(RESTORE, "reading %ju bytes from offset %ju in file %s\n", size, offset, tar->c_str());
        rc = restore_->backupFileSystem()->pread(tar, buf, size, offset);
        if (rc == -1)
        {
            failure(RESTORE,
                    "Could not read from file >%s< in underlying filesystem err %d",
                    tar->c_str(), errno);
            goto err;
        }
    ok:

        UNLOCK(&restore_->global);
        return rc;

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

        if (ok && tfn.type == REG_FILE) {

            PointInTime p;
            p.ts.tv_sec = tfn.sec;
            p.ts.tv_nsec = tfn.nsec;
            char datetime[20];
            memset(datetime, 0, sizeof(datetime));
            strftime(datetime, 20, "%Y-%m-%d_%H:%M", localtime(&p.ts.tv_sec));

            p.ago = timeAgo(&p.ts);
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
                  return (b.ts.tv_sec < a.ts.tv_sec) ||
                      (b.ts.tv_sec == a.ts.tv_sec &&
                       b.ts.tv_nsec < a.ts.tv_nsec);
                      });

    most_recent_point_in_time_ = &history_[0];
    int i = 0;
    for (auto &j : history_) {
        j.key = i;
        string de = string("@")+to_string(i)+" ";
        i++;
        if (f == absolute_point) {
            // Drop the relative @ prefix here.
            de = j.datetime;
        } else if (f == relative_point) {
            de += j.ago;
        } else {
            de += j.datetime+" "+j.ago;
        }
        j.direntry = de;
        points_in_time_[j.direntry] = &j;
        FileStat fs;
        fs.st_mode = S_IFDIR | S_IRUSR | S_IXUSR;
        j.entries_[Path::lookupRoot()] = RestoreEntry(fs, 0, Path::lookupRoot());
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

RC Restore::loadBeakFileSystem(Settings *settings)
{
    setRootDir(settings->from.storage->storage_location);
    setMountDir(settings->to.origin);

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
