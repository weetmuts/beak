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

#include "filesystem.h"
#include "reverse.h"
#include "index.h"
#include "tarfile.h"

#include <algorithm>
#include <asm-generic/errno-base.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <memory>
#include <sys/stat.h>
#include <sys/types.h>

#include "log.h"
#include "tarentry.h"

using namespace std;

ComponentId REVERSE = registerLogComponent("reverse");

ReverseTarredFS::ReverseTarredFS(FileSystem *fs)
{
    single_point_in_time_ = NULL;
    file_system_ = fs;
}

// The gz file to load, and the dir to populate with its contents.
bool ReverseTarredFS::loadGz(PointInTime *point, Path *gz, Path *dir_to_prepend)
{
    int rc;
    debug(REVERSE, "Loadgz %s %p >%s<\n", gz->c_str(), gz, dir_to_prepend->c_str());
    if (point->loaded_gz_files_.count(gz) == 1)
    {
        debug(REVERSE, "Already loaded!\n");
        return true;
    }
    point->loaded_gz_files_.insert(gz);
    
    vector<char> buf;
    rc = loadVector(gz, T_BLOCKSIZE, &buf);
    if (rc) return false;
    
    vector<char> contents;
    gunzipit(&buf, &contents);
    auto i = contents.begin();

    debug(REVERSE, "Parsing %s for files in %s\n", gz->c_str(), dir_to_prepend->c_str());
    struct IndexEntry index_entry;
    struct IndexTar index_tar;

    vector<Entry*> es;
    bool parsed_tars_already = point->gz_files_.size() != 0;

    rc = Index::loadIndex(contents, i, &index_entry, &index_tar, dir_to_prepend,
                     [this,point,&es](IndexEntry *ie){
                         debug(REVERSE," Adding entry for >%s<\n", ie->path->c_str());
                         point->entries_[ie->path] = Entry(ie->fs, ie->offset, ie->path);
                         Entry *e = &(point->entries_)[ie->path];
                         e->link = ie->link;
                         e->is_sym_link = ie->is_sym_link;
                         e->tar = ie->tar;
                         es.push_back(e);
                     },
                     [this,point,parsed_tars_already](IndexTar *it){
                         if (!parsed_tars_already) {
                             if (it->path->name()->c_str()[0] == REG_FILE_CHAR) { 
                                 point->gz_files_[it->path->parent()] = it->path;
                             }
                         }
                     });

    if (rc) {
        failure(REVERSE, "Could not parse the index file %s\n", gz->c_str());
        return false;
    }

    for (auto i : es) {
        // Now iterate over the files found.
        // Some of them might be in subdirectories.
        Path *p = i->path;
        Path *pp = p->parent();
        Entry *d = &(point->entries_)[pp];
        debug(REVERSE, "   found %s added to >%s<\n", i->path->c_str(), pp->c_str());
        d->dir.push_back(i);
        d->loaded = true;        
    }
    
    debug(REVERSE, "Found proper gz file! %s\n", gz->c_str());
    
    return true;
}

void ReverseTarredFS::loadCache(PointInTime *point, Path *path)
{
    struct stat sb;
    Path *opath = path;

    if (point->entries_.count(path) == 1) {
        Entry *e = &(point->entries_)[path];
        if (e->loaded) {
            return;
        }
    }
        
    debug(REVERSE, "Load cache for >%s<\n", path->c_str());
    // Walk up in the directory structure until a gz file is found.
    for (;;)
    {
        Path *gz = point->gz_files_[path];
        debug(REVERSE, "Looking for z01 gz file in dir >%s< (found %p)\n", path->c_str(), gz);
        if (gz != NULL) {
            gz = gz->prepend(rootDir());
            int rc = stat(gz->c_str(), &sb);
            debug(REVERSE, "%s --- %s rc=%d %d\n", gz->c_str(), rc, S_ISREG(sb.st_mode));
            if (!rc && S_ISREG(sb.st_mode)) {
                // Found a gz file!
                loadGz(point, gz, path);
                if (point->entries_.count(path) == 1) {
                    // Success
                    debug(REVERSE, "Found %s in gz %s\n", path->c_str(), gz->c_str());
                    return;
                }
                if (path != opath) {
                    // The file, if it exists should have been found here. Therefore we
                    // conclude that the file does not exist.
                    debug(REVERSE, "NOT found %s in gz %s\n", path->c_str(), gz->c_str());
                    return;
                }
            }
        }
        if (path->isRoot()) {
            // No gz file found anywhere! This filesystem should not have been mounted!
            debug(REVERSE, "No gz found anywhere!\n");
            return;
        }
        // Move up in the directory tree.
        path = path->parent();
    }
    assert(0);
}


Entry *ReverseTarredFS::findEntry(PointInTime *point, Path *path) {
    if (point->entries_.count(path) == 0)
    {
        loadCache(point, path);
        if (point->entries_.count(path) == 0)
        {
            debug(REVERSE, "Not found %s!\n", path->c_str());
            return NULL;
        }
    }

    return &(point->entries_)[path];
}

int ReverseTarredFS::getattrCB(const char *path_char_string, struct stat *stbuf)
{
    debug(REVERSE, "getattrCB >%s<\n", path_char_string);

    pthread_mutex_lock(&global);

    string path_string = path_char_string;
    Path *path = Path::lookup(path_string);
    Entry *e;
    PointInTime *point;
    
    if (path->depth() == 1)
    {
        memset(stbuf, 0, sizeof(struct stat));        
        stbuf->st_mode = S_IFDIR | S_IRUSR | S_IXUSR;
        stbuf->st_nlink = 2;
        stbuf->st_size = 0;
        stbuf->st_uid = geteuid();
        stbuf->st_gid = getegid();
        stbuf->st_mtim.tv_sec = most_recent_point_in_time_->ts.tv_sec;
        stbuf->st_mtim.tv_nsec = most_recent_point_in_time_->ts.tv_nsec;
        stbuf->st_atim.tv_sec = most_recent_point_in_time_->ts.tv_sec;
        stbuf->st_atim.tv_nsec = most_recent_point_in_time_->ts.tv_nsec;
        stbuf->st_ctim.tv_sec = most_recent_point_in_time_->ts.tv_sec;
        stbuf->st_ctim.tv_nsec = most_recent_point_in_time_->ts.tv_nsec;
        goto ok;
    }

    point = single_point_in_time_;
    if (!point) {
        Path *root = path->subpath(1,1);
        point = findPointInTime(root->str());
        if (!point) goto err;
        if (path->depth() == 2) {
            // We are getting the attributes for the virtual point_in_time directory.
            memset(stbuf, 0, sizeof(struct stat));        
            stbuf->st_mode = S_IFDIR | S_IRUSR | S_IXUSR;
            stbuf->st_nlink = 2;
            stbuf->st_size = 0;
            stbuf->st_uid = geteuid();
            stbuf->st_gid = getegid();
            stbuf->st_mtim.tv_sec = point->ts.tv_sec;
            stbuf->st_mtim.tv_nsec = point->ts.tv_nsec;
            stbuf->st_atim.tv_sec = point->ts.tv_sec;
            stbuf->st_atim.tv_nsec = point->ts.tv_nsec;
            stbuf->st_ctim.tv_sec = point->ts.tv_sec;
            stbuf->st_ctim.tv_nsec = point->ts.tv_nsec;
            goto ok;            
        }
        if (path->depth() > 2) {
            path = path->subpath(2)->prepend(Path::lookupRoot());
        }
    }

    e = findEntry(point, path);
    if (!e) goto err;
    
    memset(stbuf, 0, sizeof(struct stat));
    
    if (e->fs.isDirectory())
    {
        stbuf->st_mode = e->fs.st_mode;
        stbuf->st_nlink = 2;
        stbuf->st_size = e->fs.st_size;
        stbuf->st_uid = e->fs.st_uid;
        stbuf->st_gid = e->fs.st_gid;
        stbuf->st_mtim.tv_sec = e->fs.st_mtim.tv_sec;
        stbuf->st_mtim.tv_nsec = e->fs.st_mtim.tv_nsec;
        stbuf->st_atim.tv_sec = e->fs.st_mtim.tv_sec;
        stbuf->st_atim.tv_nsec = e->fs.st_mtim.tv_nsec;
        stbuf->st_ctim.tv_sec = e->fs.st_mtim.tv_sec;
        stbuf->st_ctim.tv_nsec = e->fs.st_mtim.tv_nsec;
        goto ok;
    }
    
    stbuf->st_mode = e->fs.st_mode;
    stbuf->st_nlink = 1;
    stbuf->st_size = e->fs.st_size;
    stbuf->st_uid = e->fs.st_uid;
    stbuf->st_gid = e->fs.st_gid;
    stbuf->st_mtim.tv_sec = e->fs.st_mtim.tv_sec;
    stbuf->st_mtim.tv_nsec = e->fs.st_mtim.tv_nsec;
    stbuf->st_atim.tv_sec = e->fs.st_mtim.tv_sec;
    stbuf->st_atim.tv_nsec = e->fs.st_mtim.tv_nsec;
    stbuf->st_ctim.tv_sec = e->fs.st_mtim.tv_sec;
    stbuf->st_ctim.tv_nsec = e->fs.st_mtim.tv_nsec;
    stbuf->st_rdev = e->fs.st_rdev;
    goto ok;

    err:

    pthread_mutex_unlock(&global);
    return -ENOENT;
    
    ok:
    
    pthread_mutex_unlock(&global);
    return 0;
}

int ReverseTarredFS::readdirCB(const char *path_char_string, void *buf,
		fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
    debug(REVERSE, "readdirCB >%s<\n", path_char_string);
    
    pthread_mutex_lock(&global);

    string path_string = path_char_string;
    Path *path = Path::lookup(path_string);
    Entry *e;
    PointInTime *point = single_point_in_time_;

    if (!point) {
        if (path->depth() == 1) {
            filler(buf, ".", NULL, 0);
            filler(buf, "..", NULL, 0);
            
            for (auto &p : history_)
            {
                char filename[256];
                memset(filename, 0, 256);
                snprintf(filename, 255, "%s", p.direntry.c_str());
                filler(buf, filename, NULL, 0);
            }
            goto ok;        
        }
                
        Path *pnt_dir = path->subpath(1,1);
        point = findPointInTime(pnt_dir->str());
        if (!point) goto err;
        path = path->subpath(2)->prepend(Path::lookupRoot());
    }
    
    e = findEntry(point, path);
    if (!e) goto err;

    if (!e->fs.isDirectory()) goto err;

    if (!e->loaded) {
        debug(REVERSE,"Not loaded %s\n", e->path->c_str());
        loadCache(point, e->path);
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
    
    pthread_mutex_unlock(&global);
    return -ENOENT;

    ok:
    
    pthread_mutex_unlock(&global);
    return 0;
}

int ReverseTarredFS::readlinkCB(const char *path_char_string, char *buf, size_t s)
{
    debug(REVERSE, "readlinkCB >%s<\n", path_char_string);

    pthread_mutex_lock(&global);

    string path_string = path_char_string;
    Path *path = Path::lookup(path_string);
    size_t c;
    Entry *e;
    PointInTime *point = single_point_in_time_;
    if (!point) {
        Path *pnt_dir = path->subpath(1,1);
        point = findPointInTime(pnt_dir->c_str());
        if (!point) goto err;
        path = path->subpath(2)->prepend(Path::lookupRoot());
    }
    e = findEntry(point, path);
    if (!e) goto err;
    
    c = e->link.length();
    if (c > s) c = s;

    memcpy(buf, e->link.c_str(), c);
    buf[c] = 0;
    debug(REVERSE, "readlinkCB >%s< bufsiz=%ju returns buf=>%s<\n", path, s, buf);
    
    goto ok;

    err:

    pthread_mutex_unlock(&global);
    return -ENOENT;

    ok:

    pthread_mutex_unlock(&global);
    return 0;
}

int ReverseTarredFS::readCB(const char *path_char_string, char *buf,
		size_t size, off_t offset_, struct fuse_file_info *fi)
{
    debug(REVERSE, "readCB >%s< offset=%ju size=%ju\n", path_char_string, offset_, size);

    pthread_mutex_lock(&global);
    
    off_t offset = offset_;
    int rc = 0;
    string path_string = path_char_string;
    Path *path = Path::lookup(path_string);

    Entry *e;
    Path *tar;
    
    PointInTime *point = single_point_in_time_;
    if (!point) {
        Path *pnt_dir = path->subpath(1,1);
        point = findPointInTime(pnt_dir->str());
        if (!point) goto err;
        path = path->subpath(2)->prepend(Path::lookupRoot());
    }
    
    e = findEntry(point, path);
    if (!e) goto err;

    tar = Path::lookup(rootDir()->str() + e->tar);
    
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

    debug(REVERSE, "Reading %ju bytes from offset %ju in file %s\n", size, offset, tar->c_str());
    rc = file_system_->pread(tar, buf, size, offset);
    if (rc == -1)
    {
        failure(REVERSE,
                "Could not read from file >%s< in underlying filesystem err %d",
                tar->c_str(), errno);
        goto err;
    }
    ok:

    pthread_mutex_unlock(&global);
    return rc;
    
    err:

    pthread_mutex_unlock(&global);
    return -ENOENT;
}


bool ReverseTarredFS::lookForPointsInTime(PointInTimeFormat f, Path *path)
{
    bool ok;
    if (path == NULL) return false;
    
    std::vector<Path*> contents;
    if (!file_system_->readdir(path, &contents)) {
        return false;
    }
    for (auto f : contents)
    {
        TarFileName tfn;
        ok = TarFile::parseFileName(f->str(), &tfn);
        
        if (ok && tfn.type == REG_FILE) {
            PointInTime p;
            p.ts.tv_sec = tfn.secs;
            p.ts.tv_nsec = tfn.nsecs;
            char datetime[20];
            memset(datetime, 0, sizeof(datetime));
            strftime(datetime, 20, "%Y-%m-%d %H:%M", localtime(&p.ts.tv_sec));

            p.ago = timeAgo(&p.ts);
            p.datetime = datetime;
            p.filename = f->str();
            history_.push_back(p);
        }
    }
    std::sort(history_.begin(), history_.end(),
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
        j.entries_[Path::lookupRoot()] = Entry(fs, 0, Path::lookupRoot());
    }
    
    return i > 0;
}

PointInTime *ReverseTarredFS::findPointInTime(string s) {
    return points_in_time_[s];
}
    
bool ReverseTarredFS::setPointInTime(string g) {
    if ((g.length() < 2) | (g[0] != '@')) {
        error(REVERSE,"Specify generation as @0 @1 @2 etc.\n");
    }
    for (size_t i=1; i<g.length(); ++i) {
        if (g[i] < '0' || g[i] > '9') {
            return false;
        }
    }
    g = g.substr(1);
    size_t gg = atoi(g.c_str());
    if (gg < 0 || gg >= history_.size()) {
        return false;
    }
    single_point_in_time_ = &history_[gg];
    return true;
}




