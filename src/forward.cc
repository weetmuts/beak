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

#include "forward.h"

#include <asm-generic/errno-base.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <ftw.h>
#include <iterator>
#include <locale>
#include <set>
#include <zlib.h>


#include "log.h"
#include "tarfile.h"

using namespace std;

static ComponentId FORWARD = registerLogComponent("forward");
static ComponentId HARDLINKS = registerLogComponent("hardlinks");
static ComponentId FUSE = registerLogComponent("fuse");
ForwardTarredFS::ForwardTarredFS(FileSystem *fs) {
    file_system_ = fs;
}

thread_local ForwardTarredFS *current_fs;

static int addEntry(const char *fpath, const struct stat *sb, int tflag, struct FTW *ftwbuf)
{ 
    return current_fs->addTarEntry(fpath, sb);
}

int ForwardTarredFS::recurse() {
    // Recurse into the root dir. Maximum 256 levels deep.
    // Look at symbolic links (ie do not follow them) so that
    // we can store the links in the tar file.
    current_fs = this;
    int rc = nftw(root_dir.c_str(), addEntry, 256, FTW_PHYS|FTW_ACTIONRETVAL);

    if (rc  == -1) {
        error(FORWARD,"Could not scan files");
        exit(EXIT_FAILURE);
    }
    return 0;
}

int ForwardTarredFS::addTarEntry(const char *p, const struct stat *sb)
{
    Path *abspath = Path::lookup(p);
    Path *path = abspath->subpath(root_dir_path->depth());
    path = path->prepend(Path::lookup(""));

    // Sockets cannot be stored.
    if(S_ISSOCK(sb->st_mode)) { return FTW_CONTINUE; }

    // Ignore any directory that has a subdir named .beak
    if(S_ISDIR(sb->st_mode) && abspath->depth() > root_dir_path->depth())
    {
        struct stat sb;
        char buf[abspath->c_str_len()+7];
        memcpy(buf, abspath->c_str(), abspath->c_str_len());
        memcpy(buf+abspath->c_str_len(), "/.beak", 7);
        int err = stat(buf, &sb);
        if (err == 0) {
            // Oups found .beak subdir! This directory and children
            // must be ignored!
            info(FORWARD,"Skipping subbeak %s\n", path->c_str());
            return FTW_SKIP_SUBTREE;
        }
    }

    // Ignore any directory named .beak, this is just a special
    // case, do not enter the .beak directory inside the configured
    // beak source dir that we are pushing.
    if (abspath->name()->str() == ".beak") return FTW_SKIP_SUBTREE;

    size_t len = strlen(path->c_str());
    char name[len+2];
    strcpy(name, path->c_str());

    if(S_ISDIR(sb->st_mode)) {
        name[len] = '/';
        name[len+1] = 0;
    }

    int status = 0;
    for (auto & p : filters) {
        bool match  = p.second.match(name);
        int rc = (match)?0:1;
        if (p.first.type == INCLUDE) {
            status |= rc;
        } else {
            status |= !rc;
        }
    }
    if (name[1] != 0 && status) {
        debug(FORWARD, "Filter dropped \"%s\"\n", name);
        return 0;
    } else {
        debug(FORWARD, "Filter NOT dropped \"%s\"\n", name);
    }

    // Creation and storage of entry.
    TarEntry *te = new TarEntry(abspath, path, sb, tarheaderstyle_);
    files[te->path()] = te;

    if (te->isDirectory()) {
        // Storing the path in the lookup
        directories[te->path()] = te;
        debug(FORWARD, "Added directory >%s<\n", te->path()->c_str());
    }

    if (!te->isDirectory() && sb->st_nlink > 1) {
        debug(HARDLINKS, "Found hard link '%s' to inode %ju\n",
              te->path()->c_str(), sb->st_ino);
        TarEntry *prev = hard_links[sb->st_ino];
        if (prev == NULL) {
            // Note that the directory tree traversal goes bottom up, which means
            // that the deepest file (that is hard linked) will be stored in the tar as a file.
            // The shallower files, will be links to the deepest file.
            debug(HARDLINKS, "Storing inode %ju contents here '%s'\n", sb->st_ino, te->path()->c_str());
            hard_links[sb->st_ino] = te;
        } else {
            // Second occurrence of this inode. Store it as a hard link.
            debug(HARDLINKS, "Rewriting %s into a hard link to %s\n",
                  te->path()->c_str(), prev->path()->c_str());
            te->rewriteIntoHardLink(prev);
            hardlinksavings += prev->blockedSize() - prev->headerSize();
        }
    }
    return FTW_CONTINUE;
}

void ForwardTarredFS::findTarCollectionDirs() {
    // Accumulate blocked sizes into children_size in the parent.
    // Set the parent pointer.
    for(auto & e : files) {
        TarEntry *te = e.second;
        Path *dir = te->path()->parent();
        if (dir) {
            TarEntry *parent = directories[dir];
            assert(parent != NULL);
            te->registerParent(parent);
            parent->addChildrenSize(te->childrenSize());
        }
    }

    // Find tar collection dirs
    for(auto & e : files) {
        TarEntry *te = e.second;

        debug(FORWARD, "ISDIR >%s< %d\n", te->path()->c_str(), te->isDirectory());

        if (te->isDirectory()) {
            bool must_generate_tars = (te->path()->depth() <= 1 ||
                                 te->path()->depth() == forced_tar_collection_dir_depth);

            debug(FORWARD, "TARS >%s< %d gentars? %d\n", te->path()->c_str(), te->path()->depth(), must_generate_tars);
            for (auto &g : triggers) {
                bool match = g.match(te->path()->c_str());
                if (match) {
                    must_generate_tars = true;
                    break;
                }
            }
            bool ought_to_generate_tars =
                (tar_trigger_size > 0 && te->childrenSize() > tar_trigger_size);

            if (must_generate_tars || ought_to_generate_tars) {
                te->setAsStorageDir();
                tar_storage_directories[te->path()] = te;
                string hs = humanReadable(te->childrenSize());
                verbose(FORWARD, "Tar collection dir % 5s '%s'\n", hs.c_str(), te->path()->c_str());
                TarEntry *i = te;
                while (i->parent() != NULL) {
                    i->parent()->addChildrenSize(-te->childrenSize());
                    i = i->parent();
                }
            }
        }
    }
}

void ForwardTarredFS::recurseAddDir(Path *path, TarEntry *direntry) {
    if (direntry->isAddedToDir() || path->isRoot()) {
        // Stop if the direntry is already added to a parent.
        // Stop at the root.
        return;
    }
    TarEntry *parent = directories[path->parent()];
    assert(parent!=NULL);
    if (!direntry->isAddedToDir()) {
        parent->addDir(direntry);
        direntry->setAsAddedToDir();
        debug(FORWARD, "ADDED recursive dir %s to %s\n",
              path->name()->c_str(), path->parent()->c_str());
        recurseAddDir(path->parent(), parent);
    }
}

void ForwardTarredFS::addDirsToDirectories() {
    // Find all directories that are tar collection dirs
    // and make sure they can be listed in all the parent
    // directories down to the root. Even if those intermediate
    // directories might not be tar collection dirs.
    for(auto & e : files) {
        Path *path = e.first;
        TarEntry *te = e.second;
        if (!te->isDirectory() || path->isRoot() || !te->isStorageDir()) {
            // Ignore files
            // Ignore the root
            // Ignore directories that are not tar collection dirs.
            continue;
        }
        TarEntry *parent = directories[path->parent()];
        assert(parent!=NULL);
        // Add the tar collection dir to its parent.
        if (!te->isAddedToDir()) {
            parent->addDir(te);
            te->setAsAddedToDir();
            debug(FORWARD,"ADDED dir >%s< to >%s\n",
                  path->name()->c_str(), path->parent()->c_str());

            // Now make sure the parent is linked to its parent all the way to the root.
            // Despite these parents might not be tar collection dirs.
            recurseAddDir(path->parent(), parent);
        }
    }
}

void ForwardTarredFS::addEntriesToTarCollectionDirs() {
    for(auto & e : files) {
        Path *path = e.first;
        TarEntry *dir = NULL;
        TarEntry *te = e.second;

        if (path->isRoot()) {
            // Ignore the root, since there is no tar_collection_dir to add it to.
            continue;
        }

        do {
            path = path->parent();
            dir = directories[path];
            // dir is NULL for directories that are only stored inside tars.
        } while (dir == NULL || !dir->isStorageDir());
        dir->addEntry(te);

        debug(FORWARD,"ADDED content %s            TO          \"%s\"\n",
              te->path()->c_str(), dir->path()->c_str());
    }
}

void ForwardTarredFS::pruneDirectories() {
    set<Path*> paths;
    map<string,string> paths_lowercase;

    string lcn = getLocale()->name();
    string utf8 = ".UTF-8";
    if (utf8.size() > lcn.size() ||
        !equal(utf8.rbegin(), utf8.rend(), lcn.rbegin())) {
        error(FORWARD, "Tarredfs expects your locale to use the encoding UTF-8!\n");
    }

    for (auto & p : tar_storage_directories) {
        Path *s = p.first;
        do {
            pair<set<Path*>::iterator,bool> rc = paths.insert(s);
            if (rc.second == false) {
                break;
            }
            debug(FORWARD, "Added %s to paths.\n", s->c_str());
            s = s->parent();
        } while (s != NULL);
    }

    map<Path*,TarEntry*> newd;
    for (auto & d : directories) {
        if (paths.count(d.first) != 0) {
            debug(FORWARD, "Re-added %s to paths.\n", d.first->c_str());
            newd[d.first] = d.second;

            // Now detect directory case conflicts that will prevent storage
            // on case-insensitive drives. E.g.
            //    /Development/PROGRAMS/src
            //    /Development/programs/src
            // We are doing this check on the remaining directories after the tar collection dirs have
            // been selected. Thus a lot of case conflicts can be handled inside the tars.
            // All file name conflicts are handled.
            string dlc = tolowercase(d.first->str());
            if (paths_lowercase.count(dlc) > 0) {
                error(FORWARD, "Case conflict for:\n%s\n%s\n", d.first->c_str(), paths_lowercase[dlc].c_str());
            }
            paths_lowercase[dlc] = d.first->str();
        }
    }
    // The root directory is always a tar collection dir.
    Path *root = Path::lookup("");
    newd[root] = directories[root];
    newd[root]->setAsStorageDir();

    directories = newd;
    debug(FORWARD,"dir size %ju\n", directories.size());
    for (auto & d : directories) {
        debug(FORWARD,"Dir >%s<\n", d.first->c_str());
    }
}

size_t ForwardTarredFS::findNumTarsFromSize(size_t amount, size_t total_size) {
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

void ForwardTarredFS::calculateNumTars(TarEntry *te,
                                size_t *nst, size_t *nmt, size_t *nlt,
                                size_t *sfs, size_t *mfs, size_t *lfs,
                                size_t *sc, size_t *mc)
{
    // The tricky calculation. How to group files into tars.
    //
    // We want to avoid avalance effects, ie that adding a single byte to a file,
    // triggers new timestamps and content in all following tars in the same
    // tar collection dir. You get this often if you simply take the files in alphabetic order
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

    for(auto & entry : te->entries()) {
        if (entry->blockedSize() < small_size) {
            small_files_size += entry->blockedSize();
            num_small_files++;
            debug(FORWARD,"Found small file %s %zu\n", entry->tarpath()->c_str(), entry->blockedSize());
        } else if (entry->blockedSize() < medium_size) {
            medium_files_size += entry->blockedSize();
            num_medium_files++;
            debug(FORWARD,"Found medium file %s %zu\n", entry->tarpath()->c_str(), entry->blockedSize());
        } else {
            large_files_size += entry->blockedSize();
            num_large_files++;
            debug(FORWARD,"Found large file %s %zu\n", entry->tarpath()->c_str(), entry->blockedSize());
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

void ForwardTarredFS::fixTarPathsAndHardLinks() {
    for (auto & e : tar_storage_directories) {
        TarEntry *te = e.second;
        vector<pair<TarEntry*,TarEntry*>> to_be_moved;

        for(auto & e : te->entries()) {
            TarEntry *entry = e;
            // This will remove the prefix (ie path outside of tar) and update the hash.
            entry->calculateTarpath(te->path());
	    
            if (entry->isHardLink()) {
                debug(HARDLINKS, "Found hardlink from >%s< to >%s<\n", entry->path()->c_str(), te->path()->c_str());

                bool rc = entry->fixHardLink(te->path());
                if (!rc) {
                    // The hard link must be pushed closer to the directory tree root!
                    TarEntry *parent = findNearestStorageDirectory(te->parent());
                    if (parent == NULL) {
                        error(HARDLINKS,"Cound not find higher storage directory from >%s<\n", te->parent()?te->parent()->path()->c_str():"?");
                    }
                    debug(HARDLINKS, "Moving >%s< from >%s< to >%s<\n", entry->path()->c_str(), te->path()->c_str(), parent->path()->c_str());
                    to_be_moved.push_back(pair<TarEntry*,TarEntry*>(entry,parent));
                    te->copyEntryToNewParent(entry, parent);
                    entry->calculateTarpath(parent->path());
                    entry->fixHardLink(parent->path());
                    debug(HARDLINKS, "New tarpath of entry >%s< stored in >%s<\n", entry->tarpath()->c_str(), parent->path()->c_str());
                    // Now, we have to copy the directory entries whose time stamps
                    // will be updated when the hard link is extracted. I.e. the
                    // sub-directories have already had their timestamps set correctly.
                    // But a late hardlink extraction will touch those. So lets copy
                    // the directory entries to the higher storage directory, to patch
                    // up after the hard link is extracted.
                    Path *common_path = Path::commonPrefix(entry->path(), entry->link());
                    TarEntry *common_te = directories[common_path];
                    common_te = findNearestStorageDirectory(common_te);
                }
            }
        }
        for (auto & p : to_be_moved) {
            te->moveEntryToNewParent(p.first, p.second);
        }
    }

}

size_t ForwardTarredFS::groupFilesIntoTars() {
    size_t num = 0;

    for (auto & e : files) {
        TarEntry *te = e.second;
        te->calculateHash();
    }
    
    for (auto & e : tar_storage_directories) {
        TarEntry *te = e.second;

        debug(FORWARD, "TAR COLLECTION DIR >%s< >%s<\n", e.first->c_str(), te->path()->c_str());

        size_t nst,nmt,nlt,sfs,mfs,lfs,smallcomp,mediumcomp;
        calculateNumTars(te, &nst,&nmt,&nlt,&sfs,&mfs,&lfs,
                         &smallcomp,&mediumcomp);

        debug(FORWARD, "TAR COLLECTION DIR nst=%zu nmt=%zu nlt=%zu sfs=%zu mfs=%zu lfs=%zu\n",
              nst,nmt,nlt,sfs,mfs,lfs);

        // This is the taz file that store sub directories for this tar collection dir.
        te->registerTazFile();
        te->registerGzFile();
        size_t has_dir = 0;

        // Order of creation: l m r z
        TarFile *curr = NULL;
        // Create the small files tars
        for (size_t i=0; i<nst; ++i) {
            te->createSmallTar(i);
        }
        // Create the medium files tars
        for (size_t i=0; i<nmt; ++i) {
            te->createMediumTar(i);
        }

        // Add the tar entries to the tar files.
        for(auto & entry : te->entries()) {
            // The entries must be files inside the tar collection directory,
            // or subdirectories inside the tar collection subdirectory!
            assert(entry->path()->depth() > te->path()->depth());

            if (entry->isDirectory()) {
                te->tazFile()->addEntryLast(entry);
            } else if (entry->isHardLink()) {
            	te->tazFile()->addEntryFirst(entry);
            } else {
                bool skip = false;

                if (!skip) {
                    if (entry->blockedSize() < smallcomp) {
                        size_t o = entry->tarpathHash() % nst;
                        curr = te->smallTar(o);
                    } else if (entry->blockedSize() < mediumcomp) {
                        size_t o = entry->tarpathHash() % nmt;
                        curr = te->mediumTar(o);
                    } else {
                        // Create the large files tar here.
                        if (!te->hasLargeTar(entry->tarpathHash())) {
                            te->createLargeTar(entry->tarpathHash());
                            curr = te->largeTar(entry->tarpathHash());
                        } else {
                            curr = te->largeTar(entry->tarpathHash());
                        }
                    }
                    curr->addEntryLast(entry);
                }
            }
        }

        string saving = humanReadable(hardlinksavings);
        debug(HARDLINKS,"Saved %s bytes using hard links\n", saving.c_str());

        // Finalize the tar files and add them to the contents listing.
        for (auto & t : te->largeTars()) {
            TarFile *tf = t.second;
            tf->fixSize();
            tf->calculateHash();
            tf->fixName();
            if (tf->currentTarOffset() > 0) {
                debug(FORWARD,"%s%s size became %zu\n", te->path()->c_str(), tf->name().c_str(), tf->size());
                te->appendFileName(tf->name());
                te->largeHashTars()[tf->hash()] = tf;
            }
        }
        for (auto & t : te->mediumTars()) {
            TarFile *tf = t.second;
            tf->fixSize();
            tf->calculateHash();
            tf->fixName();
            if (tf->currentTarOffset() > 0) {
                debug(FORWARD,"%s%s size became %zu\n", te->path()->c_str(), tf->name().c_str(), tf->size());
                te->appendFileName(tf->name());
                te->mediumHashTars()[tf->hash()] = tf;
            }
        }
        for (auto & t : te->smallTars()) {
            TarFile *tf = t.second;
            tf->fixSize();
            tf->calculateHash();
            tf->fixName();
            if (tf->currentTarOffset() > 0) {
                debug(FORWARD,"%s%s size became %zu\n", te->path()->c_str(), tf->name().c_str(), tf->size());
                te->appendFileName(tf->name());
                te->smallHashTars()[tf->hash()] = tf;
            }
        }

        te->tazFile()->fixSize();
        te->tazFile()->calculateHash();
        te->tazFile()->fixName();       

        
        set<uid_t> uids;
        set<gid_t> gids;
        /*
        for(auto & entry : te->entries()) {
            uids.insert(entry->stat()->st_uid);
            gids.insert(entry->stat()->st_gid);
            }*/

        string gzfile_contents;
        gzfile_contents.append("#tarredfs " XSTR(TARREDFS_VERSION) "\n");
        gzfile_contents.append("#message ");
        gzfile_contents.append(message_);
        gzfile_contents.append("\n");
        gzfile_contents.append("#uids");
        for (auto & x : uids) {
            gzfile_contents.append(" ");
            gzfile_contents.append(std::to_string(x));
        }
        gzfile_contents.append("\n");
        gzfile_contents.append("#gids");
        for (auto & x : gids) {
            gzfile_contents.append(" ");
            gzfile_contents.append(std::to_string(x));
        }
        gzfile_contents.append("\n");
        //gzfile_contents.append("#columns permissions uid/gid mtime_readable mtime_secs.nanos atime_secs.nanos ctime_secs.nanos filename link_information tar_file_name offset_inside_tar content_hash header_hash\n");
        gzfile_contents.append("#files ");
        gzfile_contents.append(to_string(te->entries().size()));
        gzfile_contents.append("\n");
        gzfile_contents.append(separator_string);

        for(auto & entry : te->entries()) {
            cookEntry(&gzfile_contents, entry);
            // Make sure the gzfile timestamp is the latest
            // changed timestamp of all included entries!
            entry->updateMtim(te->gzFile()->mtim());
        }

        vector<TarFile*> tars;
        for (auto & st : tar_storage_directories) {
            TarEntry *ste = st.second;
            bool b = ste->path()->isBelowOrEqual(te->path());
            if (b) {
                for (auto & tf : ste->tars()) {
                    if (tf->size() > 0 ) {
                        tars.push_back(tf);
                        // Make sure the gzfile timestamp is the latest
                        // of all subtars as well.
                        tf->updateMtim(te->gzFile()->mtim());
                    }
                }
            }
        }
        // Finally update with the latest mtime of the current storage directory!
        te->updateMtim(te->gzFile()->mtim());
        
        // Hash the hashes of all the other tar and gz files.
        te->gzFile()->calculateHash(tars, gzfile_contents);
        te->gzFile()->fixName();

        //gzfile_contents.append("#columns tar_path\n");
        gzfile_contents.append("#tars ");
        gzfile_contents.append(to_string(tars.size()));
        gzfile_contents.append("\n");
        gzfile_contents.append(separator_string);
        for (auto & tf : tars) {
            gzfile_contents.append(tf->path()->c_str());
            gzfile_contents.append("\n");
            gzfile_contents.append(separator_string);
        }
                
        vector<unsigned char> compressed_gzfile_contents;
        gzipit(&gzfile_contents, &compressed_gzfile_contents);

        TarEntry *dirs = new TarEntry(compressed_gzfile_contents.size(), tarheaderstyle_);
        dirs->setContent(compressed_gzfile_contents);
        te->gzFile()->addEntryLast(dirs);        
        te->gzFile()->fixSize();

        if (te->tazFile()->size() > 0 ) {
            
            debug(FORWARD,"%s%s size became %zu\n", te->path()->c_str(),
                  te->tazFile()->name().c_str(), te->tazFile()->size());

            te->appendFileName(te->tazFile()->name());
            te->enableTazFile();
            has_dir = 1;
        }
        te->appendFileName(te->gzFile()->name());
        te->enableGzFile();

        num += has_dir+te->smallTars().size()+te->mediumTars().size()+te->largeTars().size();
    }
    return num;
}

void ForwardTarredFS::sortTarCollectionEntries() {
    for (auto & p : tar_storage_directories) {
        TarEntry *te = p.second;
        te->sortEntries();

        vector<TarEntry*> hard_links;

        auto i = te->entries().begin();
        while (i != te->entries().end()) {
            if ((*i)->isHardLink()) {
                i = te->entries().erase(i);
                hard_links.push_back(*i);
            } else {
                ++i;
            }
        }

        for (auto e : hard_links) {
            te->entries().insert(te->entries().begin(),e);
        }
    }
}

/** Iterate up in the directory tree and return the next storate directory */
TarEntry *ForwardTarredFS::findNearestStorageDirectory(TarEntry *te) {
    while (te != NULL) {
        if (te->isStorageDir()) break;
        te = te->parent();
    }
    return te;
}

TarFile *ForwardTarredFS::findTarFromPath(Path *path) {
    bool ok;
    string n = path->name()->str();
    string d = path->parent()->name()->str();

    // File names:
    // (s)01_(001501080787).(579054757)_(1119232)_(3b5e4ec7fe38d0f9846947207a0ea44c)_(0).(tar)

    TarEntry *te = directories[path->parent()];
    if (!te) {
        debug(FORWARD,"Not a directory >%s<\n",d.c_str());
        return NULL;
    }
    TarFileName tfn;
    ok = TarFile::parseFileName(n, &tfn);
    if (!ok) {
        debug(FORWARD,"Not a proper file name: \"%s\"\n", n.c_str());
        return NULL;
    }
    vector<char> hash;
    hex2bin(tfn.header_hash, &hash);

    debug(FORWARD, "Hash >%s< hash len %d >%s<\n", tfn.header_hash.c_str(), hash.size(), toHex(hash).c_str());
    debug(FORWARD, "Type is %d suffix is %s \n", tfn.type, tfn.suffix);

    if (tfn.type == REG_FILE && tfn.suffix == "gz") {
        if (!te->hasGzFile()) {
            debug(FORWARD, "No such gz file >%s<\n", toHex(hash).c_str());
            return NULL;
        }
        return te->gzFile();
    } else
    if (tfn.type == SINGLE_LARGE_FILE_TAR) {
        if (te->largeHashTars().count(hash) == 0) {
            debug(FORWARD, "No such large tar >%s<\n", toHex(hash).c_str());
            return NULL;
        }
        return te->largeHashTar(hash);
    } else
    if (tfn.type == MEDIUM_FILES_TAR) {
        if (te->mediumHashTars().count(hash) == 0) {
            debug(FORWARD, "No such medium tar >%s<\n", toHex(hash).c_str());
            return NULL;
        }
        return te->mediumHashTar(hash);
    } else
    if (tfn.type == SMALL_FILES_TAR) {
        if (te->smallHashTars().count(hash) == 0) {
            debug(FORWARD, "No such small tar >%s<\n", toHex(hash).c_str());
            return NULL;
        }
        return te->smallHashTar(hash);
    } else if (tfn.type == DIR_TAR) {
        if (!te->hasTazFile()) {
            debug(FORWARD, "No such dir tar >%s<\n", toHex(hash).c_str());
            return NULL;
        }
        return te->tazFile();
    }
    // Should not get here.
    assert(0);
    return NULL;
}

int ForwardTarredFS::getattrCB(const char *path_char_string, struct stat *stbuf) {
    pthread_mutex_lock(&global);

    memset(stbuf, 0, sizeof(struct stat));
    debug(FUSE,"getattrCB >%s<\n", path_char_string);
    if (path_char_string[0] == '/') {
        string path_string = path_char_string;
        Path *path = Path::lookup(path_string);

        TarEntry *te = directories[path];
        if (te) {
            memset(stbuf, 0, sizeof(struct stat));
            stbuf->st_mode = S_IFDIR | 0500;
            stbuf->st_nlink = 2;
            stbuf->st_size = 0;
            stbuf->st_blksize = 512;
            stbuf->st_blocks = 0;
            goto ok;
        }

        TarFile *tar = findTarFromPath(path);
        if (tar) {
            stbuf->st_uid = geteuid();
            stbuf->st_gid = getegid();
            stbuf->st_mode = S_IFREG | 0500;
            stbuf->st_nlink = 1;
            stbuf->st_size = tar->size();
            stbuf->st_blksize = 512;
            if (tar->size() > 0) {
                stbuf->st_blocks = 1+(tar->size()/512);
            } else {
                stbuf->st_blocks = 0;
            }
            memcpy(&stbuf->st_mtim, tar->mtim(), sizeof(stbuf->st_mtim));
            goto ok;
        }
    }

    pthread_mutex_unlock(&global);
    return -ENOENT;

ok:
    pthread_mutex_unlock(&global);
    return 0;
}

int ForwardTarredFS::readdirCB(const char *path_char_string, void *buf, fuse_fill_dir_t filler,
                        off_t offset, struct fuse_file_info *fi)
{

    debug(FUSE,"readdirCB >%s<\n", path_char_string);

    if (path_char_string[0] != '/') {
        return ENOENT;
    }

    string path_string = path_char_string;
    Path *path = Path::lookup(path_string);

    TarEntry *te = directories[path];
    if (!te) {
        return ENOENT;
    }

    pthread_mutex_lock(&global);

    filler(buf, ".", NULL, 0);
    filler(buf, "..", NULL, 0);
    for (auto & e : te->dirs()) {
        char filename[256];
        snprintf(filename, 256, "%s", e->path()->name()->c_str());
        filler(buf, filename, NULL, 0);
    }

    for (auto & f : te->files()) {
        char filename[256];
        snprintf(filename, 256, "%s", f.c_str());
        filler(buf, filename, NULL, 0);
    }

    pthread_mutex_unlock(&global);
    return 0;
}

int ForwardTarredFS::readCB(const char *path_char_string, char *buf, size_t size, off_t offset, struct fuse_file_info *fi)
{
    pthread_mutex_lock(&global);
    size_t org_size = size;

    debug(FUSE,"readCB >%s< size %zu offset %zu\n", path_char_string, size, offset);
    string path_string = path_char_string;
    Path *path = Path::lookup(path_string);

    TarFile *tar = findTarFromPath(path);
    if (!tar) {
        goto err;
    }

    if ((size_t)offset >= tar->size()) {
        goto zero;
    }

    while (size>0) {
        pair<TarEntry*,size_t> r = tar->findTarEntry(offset);
        TarEntry *te = r.first;
        size_t tar_offset = r.second;
        assert(te != NULL);
        size_t l =  te->copy(buf, size, offset - tar_offset, file_system_);
        debug(FORWARD, "readCB copy size=%ju result=%ju\n", size, l);
        size -= l;
        buf += l;
        offset += l;
        if (l==0) break;
    }

    if (offset >= (ssize_t)(tar->size()-T_BLOCKSIZE*2)) {
        // Last two zero pages?
        size_t l = T_BLOCKSIZE;
        if (size < l) {
            l = size;
        }
        memset(buf,0,l);
        size -= l;
        debug(FORWARD, "readCB clearing last pages.");
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
