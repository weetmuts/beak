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

#include"reverse.h"

using namespace std;

int ReverseTarredFS::parseTarredfsContent(vector<char> &v, string path) {
    auto i = v.begin();
    string header = eatTo(v,i,0,30*1024*1024);

    std::vector<char> data(header.begin(), header.end());
    auto j = data.begin();
    
    string type = eatTo(data, j, '\n', 64);
    string uid = eatTo(data, j, '\n', 10*1024*1024); // Accept up to a ~million uniq uids
    string gid = eatTo(data, j, '\n', 10*1024*1024); // Accept up to a ~million uniq gids

    if (type != "#tarredfs 0.1") {
        debug("Type was not \"#tarredfs 0.1\" as expected! It was \"%s\"\n", type.c_str());
        return ERR;
    }

    vector<Entry*> es;
    
    while (i != v.end()) {
        string permission = eatTo(v,i,0,32);
        mode_t m = stringToPermission(permission);
        if (m == 0) break;
        
        string uidgid = eatTo(v,i,0,32);
        string size = eatTo(v,i,0,32);
        size_t s = atol(size.c_str());
        
        string datetime = eatTo(v,i,0,32);
        string secs_and_nanos = eatTo(v,i,0,64);

        std::vector<char> sn(secs_and_nanos.begin(), secs_and_nanos.end());
        auto j = sn.begin();    
        string se = eatTo(sn, j, '.', 64);
        string na = eatTo(sn, j, 0, 64);

        string filename = path+eatTo(v,i,0,1024);
        if (filename.length() > 1 && filename.back() == '/') {
            filename = filename.substr(0,filename.length()-1);
        }
        string symbolic_link = eatTo(v,i,0,1024);
        if (symbolic_link.length() > 4) {
            symbolic_link = symbolic_link.substr(4);
            s = symbolic_link.length();
        }
        string tar = path+eatTo(v,i,0,1024);
        string offset = eatTo(v,i,0,32);
        size_t o = atol(offset.c_str());

        entries[filename] = Entry(m, s, o, filename);
        Entry *e = &entries[filename];
        e->symlink = symbolic_link;
        e->secs = atol(se.c_str());
        e->nanos = atol(na.c_str());
        e->tar = tar;
        es.push_back(e);
    }

    for (auto i : es) {
        debug("FILE %s in dir %s\n", i->file.c_str(), i->path.c_str());
        string dir = i->path;
        if (entries.count(dir) == 0) {
            loadCache(dir);
        }
        Entry *d = &entries[dir];
        d->dir.push_back(i);
        d->loaded = true;
    }
    
    return OK;
}

void ReverseTarredFS::loadTaz(string taz_path, string path) {
    TAR *t;

    if (tazs.count(taz_path) == 1) {
        return;
    }
    
    if (path.back() != '/') {
        path = path+"/";
    }
    int rc = tar_open(&t, taz_path.c_str(), NULL, O_RDONLY, 0, TAR_GNU);
    if (rc) {
        debug("Could not open taz %s\n", taz_path.c_str());
        return;
    }

    rc = th_read(t);
    if (rc) {
        debug("Could not read volume header in %s\n", taz_path.c_str());
        tar_close(t);    
        return;
    }

    if (!TH_ISVOLHDR(t)) {
        debug("First entry is not a volume header %s\n", taz_path.c_str());
        tar_close(t);    
        return;
    }

    string n = th_get_pathname(t);
    if (n != "tarredfs") {
        debug("Volume header is not tarredfs %s", taz_path.c_str());
        tar_close(t);    
        return;
    }

    rc = th_read(t);
    if (rc) {
        debug("Could not read tarredfs-contents in %s\n", taz_path.c_str());
        tar_close(t);    
        return;
    }

    if (!TH_ISREG(t)) {
        debug("Second entry is not a regular file in %s\n", taz_path.c_str());
        tar_close(t);    
        return;
    }

    n = th_get_pathname(t);
    if (n != "tarredfs-contents") {
        debug("Second entry in %s is not tarredfs-contents but %s", taz_path.c_str(), n.c_str());
        tar_close(t);    
        return;
    }

    size_t size = th_get_size(t);
    vector<char> buf;

    debug("SIZE %ju \n", size);

    char block[T_BLOCKSIZE+1];
    for (size_t i=0; i<size; i += T_BLOCKSIZE)
    {
        memset(block, 0, T_BLOCKSIZE+1);
        ssize_t k = tar_block_read(t, block);

        if (k != T_BLOCKSIZE)
        {
            if (k != -1) { errno = EINVAL; }
            tar_close(t);
            debug("Internal error reading block from taz file.");
            return;
        }
        buf.insert(buf.end(), block, block+k);
    }

    rc = parseTarredfsContent(buf, path);
    if (rc) {
        debug("Could not parse the tarredfs-contents file in %s\n", taz_path.c_str());    
        tar_close(t);
        return;
    }
    
    debug("Found proper taz file! %s\n", taz_path.c_str());
    char taz_hash[SHA256_DIGEST_LENGTH];
    char hash[SHA256_DIGEST_LENGTH];
    
    tazs[taz_path] = Taz(t);

    for (;;) 
    {
        rc = th_read(t);
        if (rc) break;
        
        debug("Found %s\n", th_get_pathname(t));
    } 

    
    tar_close(t);
    return;
}

void ReverseTarredFS::loadCache(string path) {
    string file, taz;
    struct stat sb;
    string opath = path;
    
    debug("Load cache for >%s<\n", path.c_str());
    // Walk up in the directory structure until a taz file is found.
    for(;;) {
        string c;
        if (path.back() != '/') {
            c = "/";
        }
        taz = root_dir+path+c+"taz00000000.tar";
        debug("Looking for cache %s\n", taz.c_str());
        int rc = stat(taz.c_str(), &sb);
        if (!rc && S_ISREG(sb.st_mode)) {
            // Found a taz file!
            loadTaz(taz, path);
            if (entries.count(path) == 1) {
                // Success
                debug("Found %s in taz %s\n", path.c_str(), taz.c_str());
                return;
            }
            if (path != opath) {
                // The file, if it exists should have been found here. Therefore we
                // conclude that the file does not exist.
                debug("NOT found %s in taz %s\n", path.c_str(), taz.c_str());
                return;
            }
        }
        if (path == "/") {
            // No taz file found anywhere! This filesystem should not have been mounted!
            debug("No taz found anywhere!\n");
            return; 
        }
        // Move up in the directory tree.
        file = "/"+basename(path)+file;
        path = dirname(path);
    }
    assert(0);
}

int ReverseTarredFS::getattrCB(const char *path, struct stat *stbuf) {
    pthread_mutex_lock(&global);
    
    memset(stbuf, 0, sizeof(struct stat));
    if (path[0] == '/') {        
        string p = path;
        
        if (entries.count(p) == 0) {
            loadCache(p);
        }
        if (entries.count(p) == 0) {
            debug("Could not find %s in any taz file!\n", p.c_str());
            goto err;
        }

        Entry &e = entries[p];
        if (e.isDir()) {
            stbuf->st_mode = e.mode_bits;
            stbuf->st_nlink = 2;
            stbuf->st_size = e.size;
            stbuf->st_uid = geteuid();
            stbuf->st_gid = getegid();
            stbuf->st_mtim.tv_sec = e.secs;
            stbuf->st_mtim.tv_nsec = e.nanos;
            goto ok;
        }
        
        stbuf->st_mode = e.mode_bits;
        stbuf->st_nlink = 1;
        stbuf->st_size = e.size;
        stbuf->st_uid = geteuid();
        stbuf->st_gid = getegid();
        stbuf->st_mtim.tv_sec = e.secs;
        stbuf->st_mtim.tv_nsec = e.nanos;
        debug("OK\n");
        goto ok;
    }

err:    
    pthread_mutex_unlock(&global);
    return -ENOENT;

ok:
    pthread_mutex_unlock(&global);
    return 0;
}

int ReverseTarredFS::readdirCB(const char *path, void *buf, fuse_fill_dir_t filler,
                               off_t offset, struct fuse_file_info *fi)
{
    pthread_mutex_lock(&global);
    int rc = OK;
    
    if (path[0] == '/') {        
        string p = path;
        
        if (entries.count(p) == 0) {
            loadCache(p);
        }
        if (entries.count(p) == 0) {
            debug("Could not find %s in any taz file!\n", p.c_str());
            goto err;
        }

        Entry &e = entries[p];
        if (!e.loaded) {
            loadCache(p);
        }
        if (e.isDir()) {    
            filler(buf, ".", NULL, 0);
            filler(buf, "..", NULL, 0);
            for (auto i : e.dir)
            {
                char filename[256];
                memset(filename, 0, 256);
                snprintf(filename, 255, "%s", i->file.c_str());
                filler(buf, filename, NULL, 0);
                debug("Reading dir line >%s<\n", i->file.c_str());
            }
        }
    } else {
        rc = -ENOENT;
    }

err:
    pthread_mutex_unlock(&global);   
    return rc;
}

int ReverseTarredFS::readlinkCB(const char *path, char *buf, size_t s) {
    pthread_mutex_lock(&global);
    
    debug("readlinkCB >%s< bufsiz=%ju\n", path, s);
    if (path[0] == '/') {        
        string p = path;
        
        if (entries.count(p) == 0) {
            loadCache(p);
        }
        if (entries.count(p) == 0) {
            debug("Could not find %s in any taz file!\n", p.c_str());
            goto err;
        }
        
        Entry &e = entries[p];
        size_t c = e.symlink.length();

        if (c > s) {
            c = s;
        }
        memcpy(buf, e.symlink.c_str(), c);
        buf[c] = 0;
        debug("readlinkCB >%s< bufsiz=%ju returns buf=>%s<\n", path, s, buf);
        
        goto ok;
    }

err:    
    pthread_mutex_unlock(&global);
    return -ENOENT;

ok:
    pthread_mutex_unlock(&global);
    return 0;
}

int ReverseTarredFS::readCB(const char *path, char *buf, size_t size, off_t offset_, struct fuse_file_info *fi)
{
    assert(path[0] == '/');
    assert(offset_ >= 0);

    try {
        pthread_mutex_lock(&global);

        size_t offset = (size_t)offset_;
        int rc = 0;
        string p = path;
        int fd;
        ssize_t l;
        
        if (entries.count(p) == 0) {
            loadCache(p);
        }
        if (entries.count(p) == 0) {
            debug("Could not find %s in any taz file!\n", p.c_str());
            throw;
        }
        
        Entry &e = entries[p];

        string tar = root_dir+e.tar;
        
        if (offset > e.size) {
            // Read outside of file size
            rc = 0;
            goto ok;
        }
        
        if (offset+size > e.size) {
            // Shrink actual read to fit file.
            size = e.size-offset;
        }
        
        // Offset into tar file.
        offset += e.offset;
        
        fd = open(tar.c_str(), O_RDONLY);
        if (fd==-1) {
            failure("Could not open file >%s< in underlying filesystem err %d", tar.c_str(), errno);
            throw;
        }
        debug("Reading %ju bytes from offset %ju in file %s\n", size, offset, tar.c_str());
        l = pread(fd, buf, size, offset);
        close(fd);        
        if (l==-1) {
            failure("Could not read from file >%s< in underlying filesystem err %d", tar.c_str(), errno);           
            throw;
        }
        rc = l;
    ok:
        pthread_mutex_unlock(&global);       
        return rc;
    }
    catch (...) {        
        pthread_mutex_unlock(&global);
        return -ENOENT;
    }
}

