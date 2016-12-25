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

#include"forward.h"

#include"log.h"

#include<ftw.h>
#include<string.h>
#include<unistd.h>

#include<algorithm>
#include<codecvt>
#include<locale>
#include<set>
#include<sstream>

using namespace std;

int TarredFS::recurse(FileCB cb) {
    // Recurse into the root dir. Maximum 256 levels deep.
    // Look at symbolic links (ie do not follow them) so that
    // we can store the links in the tar file.
    int rc = nftw(root_dir.c_str(), cb, 256, FTW_PHYS|FTW_DEPTH);
    
    if (rc  == -1) {
        error("Could not scan files");
        exit(EXIT_FAILURE);
    }
    return 0;
}

int TarredFS::addTarEntry(const char *fpath, const struct stat *sb, struct FTW *ftwbuf) {
    
    size_t len = strlen(fpath);

    // Skip root_dir path.
    const char *pp;
    if (len > root_dir.length()) {
        // We are in a subdirectory of the root.
        pp = fpath+root_dir.length();
    } else {
        pp = "";
    }

    len = strlen(pp);
    char name[len+2];
    strcpy(name, pp);
    
    if(S_ISDIR(sb->st_mode)) {
        name[len] = '/';
        name[len+1] = 0;
    }    

    int status = 0;
    for (auto & p : filters) {
        int rc = regexec(&p.second, name, (size_t) 0, NULL, 0);
        if (p.first.type == INCLUDE) {
            status |= rc;
        } else {
            status |= !rc;
        }
    }
    if (name[1] != 0 && status) {
        debug("Filter dropped \"%s\"\n", name);            
        return 0;
    } else {
        debug("Filter NOT dropped \"%s\"\n", name);
    }
        
    
    string p = pp;
    TarEntry *te = new TarEntry(p, sb, root_dir);
    files[te->path] = te;
    
    if (TH_ISDIR(te->tar)) {
        // Storing the path in the lookup 
        directories[te->path] = te;
        debug("Added directory '%s'\n", te->path.c_str());
    } 
    
    return 0;
}

void TarredFS::findChunkPoints() {
    // Accumulate blocked sizes into children_size in the parent.
    // Set the parent pointer.
    for(auto & direntry : files) {
        TarEntry *te = direntry.second;
        string dir = dirname(te->path);
        if (dir != te->path) {
            TarEntry *parent = directories[dir];
            assert(parent != NULL);
            te->parent = parent;
            parent->children_size += te->children_size;
        }
    }
    
    // Find chunk points
    for(auto & direntry : files) {
        TarEntry *te = direntry.second;
        
        if (TH_ISDIR(te->tar)) {
            bool must_chunk = (te->path == "/" || te->depth == forced_chunk_depth);
            bool ought_to_chunk = (tar_trigger_size > 0 && te->children_size > tar_trigger_size);
            if (must_chunk || ought_to_chunk) {
                te->is_chunk_point = true;
                chunk_points[te] = pair<size_t,size_t>(te->children_size,0);
                string hs = humanReadable(te->children_size);
                verbose("Chunk point % 5s '%s'\n", hs.c_str(), te->path.c_str());
                TarEntry *i = te;
                while (i->parent != NULL) {
                    i->parent->children_size -= te->children_size;
                    i = i->parent;
                }
            }
        } 
    }
}        

void TarredFS::recurseAddDir(string path, TarEntry *direntry) {
    if (direntry->added_to_directory || path == "/") {
        // Stop if the direntry is already added to a parent.
        // Stop at the root.
        return;
    }
    string name = basename(path);
    string dir = dirname(path);        
    TarEntry *parent = directories[dir];
    assert(parent!=NULL);
    if (!direntry->added_to_directory) {
        parent->dirs.push_back(direntry);
        direntry->added_to_directory = true;
        
        debug("ADDED recursive dir %s to %s\n", name.c_str(), dir.c_str());
        recurseAddDir(dir, parent);
    }
}
    
void TarredFS::addDirsToDirectories() {
    // Find all directories that are chunk points
    // and make sure they can be listed in all the parent
    // directories down to the root. Even if those intermediate
    // directores might not be chunk points.
    for(auto & direntry : files) {
        string path = direntry.first;
        assert(path.length()>0);
        if (!direntry.second->isDir() || path == "/" || !direntry.second->is_chunk_point) {
            // Ignore files
            // Ignore the root
            // Ignore directories that are not chunk points
            continue;
        }
        string name = basename(path);
        string dir = dirname(path);
        TarEntry *parent = directories[dir];
        assert(parent!=NULL);
        // Add the chunk point directory to its parent.
        if (!direntry.second->added_to_directory) {
            parent->dirs.push_back(direntry.second);
            direntry.second->added_to_directory = true;
            debug("ADDED dir %s to %s\n", name.c_str(), dir.c_str());
            
            // Now make sure the parent is linked to its parent all the way to the root.
            // Despite these parents might not be chunk points.
            recurseAddDir(dir, parent);
        }
    }    
}

void TarredFS::addEntriesToChunkPoints() {
    for(auto & direntry : files) {
        string path = direntry.first;
        TarEntry *dir = NULL;
        
        if (path == "/") {
            // Ignore the root, since there is no chunk_point to add it to.
            continue;
        }
        
        do {
            path = dirname(path);
            dir = directories[path];
            // dir is NULL for directories that are only stored inside tars.
        } while (dir == NULL || !dir->is_chunk_point);            
        dir->entries.push_back(direntry.second);
        debug("ADDED content %s            TO          \"%s\"\n", direntry.first.c_str(), dir->path.c_str());
    }    
}

std::locale const user_locale("");

std::wstring to_wstring(std::string const& s) {
    std::wstring_convert<std::codecvt_utf8<wchar_t> > conv;
    return conv.from_bytes(s);
}

std::string to_string(std::wstring const& s) {
    std::wstring_convert<std::codecvt_utf8<wchar_t> > conv;
    return conv.to_bytes(s);
}

std::string tolowercase(std::string const& s) {
    auto ss = to_wstring(s);
    for (auto& c : ss) {
        c = std::tolower(c, user_locale);
    }
    return to_string(ss);
}

void TarredFS::pruneDirectories() {
    set<string> paths;
    map<string,string> paths_lowercase;
    
    for (auto & p : chunk_points) {
        string s = p.first->path;
        do {
            pair<set<string>::iterator,bool> rc = paths.insert(s);
            if (rc.second == false) {
                break;
            }
            debug( "Added %s to paths.\n", s.c_str());
            s = dirname(s);
        } while (s.length()>1);            
    }
    
    map<string,TarEntry*> newd;
    for (auto & d : directories) {
        if (paths.count(d.first) != 0) {
            debug( "Re-added %s to paths.\n", d.first.c_str());
            newd[d.first] = d.second;

            // Now detect directory case conflicts that will prevent storage
            // on case-insensitive drives. E.g.
            //    /Development/PROGRAMS/src
            //    /Development/programs/src
            // We are doing this check on the remaining directories after the chunk points have
            // been selected. Thus a lot of case conflicts can be handled inside the tars.
            // Typically all file name conflicts are handled.
            string dlc = tolowercase(d.first);
            if (paths_lowercase.count(dlc) > 0) {
                error("Case conflict for:\n%s\n%s\n", d.first.c_str(), paths_lowercase[dlc].c_str());
            }
            paths_lowercase[dlc] = d.first;
        }
    }
    // The root directory is always a chunk point.
    newd["/"] = directories["/"];
    newd["/"]->is_chunk_point = true;
    
    directories = newd;
    debug( "dir size %ju\n", directories.size());
    for (auto & d : directories) {
        debug("Dir >%s<\n", d.first.c_str());
    }    
}

size_t TarredFS::findNumTarsFromSize(size_t amount, size_t total_size) {
    // We have 128M of data
    // The amount (= min tar size) is 10M, how many tars?
    // 1 -> 10
    // 2 -> 20
    // 4 -> 40
    // 8 -> 80
    // 16 -> 160 which is larger than 128
    // thus return that we should use 8 tar files.
    size_t n = 1;

    while (amount < total_size) {
        amount *= 2;
        n*=2;
    }
    return n;
}

void TarredFS::calculateNumTars(TarEntry *te,
                                size_t *nst, size_t *nmt, size_t *nlt,
                                size_t *sfs, size_t *mfs, size_t *lfs,
                                size_t *sc, size_t *mc)
{
    // The tricky calculation. How to group files into tars.
    //
    // We want to avoid avalance effects, ie that adding a single byte to a file,
    // triggers new timestamps and content in all following tars in the same
    // chunk point. You get this often if you simply take the files in alphabetic order
    // and switch to the next tar when the current one is filled up.
    //
    // Sum the sizes of the normal files
    size_t small_files_size = 0;
    size_t num_small_files = 0;

    size_t medium_files_size = 0;
    size_t num_medium_files = 0;

    size_t large_files_size = 0;
    size_t num_large_files = 0;

    size_t small_size = target_target_tar_size / 100; // Default 10M/100 = 100K
    size_t medium_size = target_target_tar_size; // Default 10M
    
    for(auto & entry : te->entries) {
        if (entry->blocked_size < small_size) {
            small_files_size += entry->blocked_size;
            num_small_files++;
            debug("Found small file %s %zu\n", entry->tarpath.c_str(), entry->blocked_size);
        } else if (entry->blocked_size < medium_size) {
            medium_files_size += entry->blocked_size;
            num_medium_files++;
            debug("Found medium file %s %zu\n", entry->tarpath.c_str(), entry->blocked_size);
        } else {
            large_files_size += entry->blocked_size;
            num_large_files++;
            debug("Found large file %s %zu\n", entry->tarpath.c_str(), entry->blocked_size);            
        }
    }
    
    *nst = findNumTarsFromSize(target_target_tar_size, small_files_size);
    *sfs = small_files_size;

    *nmt = findNumTarsFromSize(target_target_tar_size, medium_files_size);
    *mfs = medium_files_size;

    *nlt = num_large_files;
    *lfs = large_files_size;

    *sc = small_size;
    *mc = medium_size;
    
    if (small_files_size <= target_target_tar_size ||
        medium_files_size <= target_target_tar_size) {
        // Either the small tar or the medium tar is not big enough.
        // Put them all in a single tar and hope that they together are as large as
        // the target tar size.
        *sc = medium_size;
        *nst = *nst + *nmt - 1;
        *sfs = *sfs + *mfs;
        *nmt = 0;
        *mfs = 0;
    }     
}

size_t TarredFS::groupFilesIntoTars() {
    size_t num = 0;
    for (auto & e : chunk_points) {
        TarEntry *te = e.first;
        
        debug("CHUNK %07ju %07ju %s\n", e.second.first, e.second.second, e.first->path.c_str());

        for(auto & entry : te->entries) {
            // This will remove the prefix (ie path outside of tar) and update the hash.
            entry->removePrefix(te->path.length());
        }

        size_t nst,nmt,nlt,sfs,mfs,lfs,smallcomp,mediumcomp;
        calculateNumTars(te, &nst,&nmt,&nlt,&sfs,&mfs,&lfs,
                         &smallcomp,&mediumcomp);

        debug("CHUNK nst=%zu nmt=%zu nlt=%zu sfs=%zu mfs=%zu lfs=%zu\n",
              nst,nmt,nlt,sfs,mfs,lfs);
        
        // This is the tar that store all the sub directories for this chunk point.
        te->dir_tar = TarFile(DIR_TAR, 0, true);
        TarFile *dirs = &te->dir_tar;
        size_t has_dir = 0;
        
        // Order of creation: l m r z
        TarFile *curr;
        // Create the small files tars
        for (size_t i=0; i<nst; ++i) {
            te->small_tars[i] = TarFile(SMALL_FILES_TAR, i, false);
        }
        // Create the medium files tars
        for (size_t i=0; i<nmt; ++i) {
            te->medium_tars[i] = TarFile(MEDIUM_FILES_TAR, i, false);
        }

        // Add the tar entries to the tar files.
        for(auto & entry : te->entries) {
            // The entries must be files inside the chunkpoint directory,
            // or subdirectories inside the chunkpoint subdirectory!
            assert(entry->path.length() > te->path.length());
            assert(!strncmp(entry->path.c_str(), te->path.c_str(), te->path.length()));
            
            if (entry->isDir()) {
                dirs->addEntryLast(entry);                
            } else {
                // Spread the files over the tars by the hash of the file name.
                if (entry->blocked_size < smallcomp) {
                    size_t o = entry->tarpath_hash % nst;
                    curr = &te->small_tars[o];
                    assert(curr->tar_contents == SMALL_FILES_TAR);
                } else if (entry->blocked_size < mediumcomp) {
                    size_t o = entry->tarpath_hash % nmt;
                    curr = &te->medium_tars[o];
                    assert(curr->tar_contents == MEDIUM_FILES_TAR);
                } else {
                    // Create the large files tar here.
                    if (te->large_tars.count(entry->tarpath_hash) == 0) {
                        te->large_tars[entry->tarpath_hash] = TarFile(SINGLE_LARGE_FILE_TAR,
                                                              entry->tarpath_hash, false);
                        curr = &te->large_tars[entry->tarpath_hash];
                        te->num_tars++;
                    } else {
                        curr = &te->large_tars[entry->tarpath_hash];
                    }
                    assert(curr->tar_contents == SINGLE_LARGE_FILE_TAR);
                } 
                curr->addEntryLast(entry);
            }            
        }

        // Finalize the tar files and add them to the contents listing.
        for (auto & t : te->large_tars) {
            TarFile *tf = &t.second;
            tf->calculateSHA256Hash();
            if (tf->tar_offset > dirs->volume_header->blocked_size) {
                tf->size = tf->tar_offset;
                debug("%s%s size became %zu\n", te->path.c_str(), tf->name.c_str(), tf->size);
                te->files.push_back(tf->name);
            }
        }
        for (auto & t : te->medium_tars) {
            TarFile *tf = &t.second;
            tf->calculateSHA256Hash();            
            if (tf->tar_offset > dirs->volume_header->blocked_size) {
                tf->size = tf->tar_offset;
                debug("%s%s size became %zu\n", te->path.c_str(), tf->name.c_str(), tf->size);
                te->files.push_back(tf->name);
            }
        }
        for (auto & t : te->small_tars) {
            TarFile *tf = &t.second;
            tf->calculateSHA256Hash();            
            if (tf->tar_offset > dirs->volume_header->blocked_size) {
                tf->size = tf->tar_offset;
                debug("%s%s size became %zu\n", te->path.c_str(), tf->name.c_str(), tf->size);
                te->files.push_back(tf->name);
            }
        }

        set<uid_t> uids;
        set<gid_t> gids;
        for(auto & entry : te->entries) {
            uids.insert(entry->sb.st_uid);
            gids.insert(entry->sb.st_gid);
        }
        string null("\0",1);
        string listing;
        listing.append("#tarredfs " TARREDFS_VERSION "\n");
        listing.append("#uids");        
        for (auto & x : uids) {
            stringstream ss;
            ss << x;
            listing.append(" ");            
            listing.append(ss.str());
        }
        listing.append("\n");
        listing.append("#gids");        
        for (auto & x : gids) {
            stringstream ss;
            ss << x;
            listing.append(" ");            
            listing.append(ss.str());
        }
        listing.append("\n");
        listing.append(null);
        size_t width = 3;
        string space(" ");
        for(auto & entry : te->entries) {
            ssize_t diff = width - entry->tv_line_size.length();
            stringstream ss;            
            if (diff < 0) {
                width = entry->tv_line_size.length();
                diff = 0;
            }
            while (diff > 0) {
                ss << space;
                diff--;
            }
            entry->tv_line_size = ss.str()+entry->tv_line_size;
        }
        
        for(auto & entry : te->entries) {
            // -r-------- fredrik/fredrik 745 1970-01-01 01:00 testing
            // drwxrwxr-x fredrik/fredrik   0 2016-11-25 00:52 autoconf/
            // -r-------- fredrik/fredrik   0 2016-11-25 11:23 libtar.so -> libtar.so.0.1
            listing.append(entry->tv_line_left);
            listing.append(null);
            listing.append(entry->tv_line_size);
            listing.append(null);
            listing.append(entry->tv_line_right);
            listing.append(null);
            listing.append(entry->tarpath);
            listing.append(null);
            if (entry->link.length() > 0) {
                listing.append(" -> ");
                listing.append(entry->link);
            }
            listing.append(null);
            listing.append(entry->tar_file->name);
            listing.append(null);
            stringstream ss;
            ss << entry->tar_offset+entry->header_size;
            listing.append(ss.str());
            listing.append("\n");
            listing.append(null);
        }
        struct stat sb;
        memset(&sb, 0, sizeof(sb));
        sb.st_size = listing.length();
        sb.st_uid = geteuid();
        sb.st_gid = getegid();
        sb.st_mode = S_IFREG | 0400;
        sb.st_nlink = 1;
        
        TarEntry *list = new TarEntry("/tarredfs-contents", &sb, "");
        list->setContent(listing);
        dirs->addEntryFirst(list);                                   
        dirs->size = dirs->tar_offset;
        dirs->calculateSHA256Hash();        

        if (dirs->size > dirs->volume_header->blocked_size ) {
            debug("%s%s size became %zu\n", te->path.c_str(), dirs->name.c_str(), dirs->size);
            te->files.push_back(dirs->name);
            te->dir_tar_in_use = true;
            assert(dirs->offsets.size() > 0);
            has_dir = 1;
        }

        num += has_dir+te->small_tars.size()+te->medium_tars.size()+te->large_tars.size();
    }
    return num;
}

void TarredFS::sortChunkPointEntries() {
    for (auto & p : chunk_points) {
        TarEntry *te = p.first;
        std::sort(te->entries.begin(), te->entries.end(),
                  [](TarEntry *a, TarEntry *b)->bool {
                      return TarSort::compare(a->path.c_str(), b->path.c_str());
                  });
    }
}

TarFile *TarredFS::findTarFromPath(string path) {
    debug("Find tar from %s\n", path.c_str());
    string n = basename(path);
    string d = dirname(path);

    size_t l = n.length();
    if (l < 8 || l > 50 || n[0] != 't' || n[1] != 'a' || (n[2] != 'r' && n[2] != 'z' && n[2] != 'l' && n[2] != 'm') ||
        n[l-1] != 'r' || n[l-2] != 'a' || n[l-3] != 't' || n[l-4] != '.')
    {
        debug("Not a tar file.\n");
        return NULL;
    }

    const char t = n[2];    
    TarEntry *te = directories[d];
    if (!te) {
        debug("Not a directory >%s<\n",d.c_str()); 
        return NULL;
    }
    string num = n.substr(3,l-7);
    size_t i = (size_t)strtol(num.c_str(), NULL, 16);
    if (t == 'l') {
        if (te->large_tars.count(i) == 0) {
            debug("No such large tar >%zx<\n", i);
            return NULL;
        }
        return &te->large_tars[i];
    } else
    if (t == 'm') {
        if (te->medium_tars.count(i) == 0) {
            debug("No such medium tar >%zx<\n", i);
            return NULL;
        }
        return &te->medium_tars[i];
    } else
    if (t == 'r') {
        if (te->small_tars.count(i) == 0) {
            debug("No such small tar >%zx<\n", i);
            return NULL;
        }
        return &te->small_tars[i];
    } else if (t == 'z') {
        if (!te->dir_tar_in_use) {
            debug("No such dir tar >%zx<\n", i);
            return NULL;
        }
        return &te->dir_tar;
    }
    // Should not get here.
    assert(0);
    return NULL;
}

int TarredFS::getattrCB(const char *path, struct stat *stbuf) {
    pthread_mutex_lock(&global);
    
    memset(stbuf, 0, sizeof(struct stat));
    debug("getattrCB >%s<\n", path);
    if (path[0] == '/') {        
        string p = path;
        string dir_suffix = "";
        if (p.length() > 1) {
            dir_suffix = "/";
        }
        TarEntry *te = directories[p+dir_suffix];
        if (te) {
            memcpy(stbuf, &te->sb, sizeof(*stbuf));
            stbuf->st_mode = S_IFDIR | 0500;
            stbuf->st_size = 0;
            goto ok;
        }
        
        TarFile *tar = findTarFromPath(path);
        if (tar) {
            stbuf->st_uid = geteuid();
            stbuf->st_gid = getegid();
            stbuf->st_mode = S_IFREG | 0500;
            stbuf->st_nlink = 1;
            stbuf->st_size = tar->size;
            memcpy(&stbuf->st_mtim, &tar->mtim, sizeof(stbuf->st_mtim));
            goto ok;
        }
    }
    
    pthread_mutex_unlock(&global);
    return -ENOENT;

ok:
    pthread_mutex_unlock(&global);
    return 0;
}

int TarredFS::readdirCB(const char *path, void *buf, fuse_fill_dir_t filler,
                        off_t offset, struct fuse_file_info *fi)
{

    debug("readdirCB >%s<\n", path);

    if (path[0] != '/') {
        return ENOENT;
    }
        
    string p = path;
    string dir_suffix = "";
    if (p.length() > 1) {
        dir_suffix = "/";
    }
    TarEntry *te = directories[p+dir_suffix];
    if (!te) {
        return ENOENT;
    }

    pthread_mutex_lock(&global);
    
    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);
    for (auto & e : te->dirs) {
        char filename[256];
        snprintf(filename, 256, "%s", e->name.c_str());
        filler(buf, filename, NULL, 0);
        debug( "    dir \"%s\"\n", filename);
    }
    
    for (auto & f : te->files) {
        char filename[256];
        snprintf(filename, 256, "%s", f.c_str());
        filler(buf, filename, NULL, 0);
        debug( "    file entry %s\n", filename);
    }
    
    pthread_mutex_unlock(&global);   
    return 0;
}

int TarredFS::readCB(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    pthread_mutex_lock(&global);
    size_t org_size = size;

    debug("readCB >%s< size %zu offset %zu\n", path, size, offset);
    TarFile *tar = findTarFromPath(path);
    if (!tar) {
        goto err;
    }

    if ((size_t)offset >= tar->size) {
        goto zero;
    }

    while (size>0) {
        pair<TarEntry*,size_t> r = tar->findTarEntry(offset);
        TarEntry *te = r.first;
        size_t tar_offset = r.second;
        assert(te != NULL);
        size_t l =  te->copy(buf, size, offset - tar_offset);
        debug("readCB copy size=%ju result=%ju\n", size, l);
        size -= l;
        buf += l;
        offset += l;
        if (l==0) break;
    }

    if (offset >= (ssize_t)(tar->size-T_BLOCKSIZE*2)) {
        // Last two zero pages?
        size_t l = T_BLOCKSIZE;
        if (size < l) {
            l = size;
        }                         
        memset(buf,0,l);
        size -= l;
        debug("readCB clearing last pages.");
    }

    pthread_mutex_unlock(&global);
    return org_size-size;

err:
    pthread_mutex_unlock(&global);
    return -ENOENT;

zero:
    pthread_mutex_unlock(&global);
    return 0;
}

