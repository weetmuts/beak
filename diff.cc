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

#include"diff.h"

#include"log.h"

#include<ftw.h>
#include<limits.h>
#include<string.h>
#include<unistd.h>

#include<algorithm>
#include<codecvt>
#include<locale>
#include<set>
#include<sstream>

using namespace std;

ComponentId DIFF = registerLogComponent("diff");

bool Entry::same(Entry *e) {
    return sb.st_mode == e->sb.st_mode &&
        sb.st_uid == e->sb.st_uid &&
        sb.st_gid == e->sb.st_gid &&
        sb.st_size == e->sb.st_size &&
        sb.st_mtim.tv_sec == e->sb.st_mtim.tv_sec &&
        sb.st_mtim.tv_nsec == e->sb.st_mtim.tv_nsec;        
}
        
int DiffTarredFS::recurse(Target t, FileCB cb) {
    string s = (t==FROM)? from_dir : to_dir;

    // Recurse into the root dir. Maximum 256 levels deep.
    // Look at symbolic links (ie do not follow them) so that
    // we can store the links in the tar file.
    int rc = nftw(s.c_str(), cb, 256, FTW_PHYS|FTW_DEPTH);
    
    if (rc  == -1) {
        error(DIFF, "diff", "Could not scan files");
        exit(EXIT_FAILURE);
    }
    return 0;
}

int DiffTarredFS::addFromFile(const char *fpath, const struct stat *sb, struct FTW *ftwbuf) {
    return addFile(FROM, fpath, sb, ftwbuf);
}

int DiffTarredFS::addToFile(const char *fpath, const struct stat *sb, struct FTW *ftwbuf) {
    return addFile(TO, fpath, sb, ftwbuf);
}

int DiffTarredFS::addFile(Target t, const char *fpath, const struct stat *sb, struct FTW *ftwbuf) {
    
    size_t len = strlen(fpath);
    string root;

    if (t == FROM) {
        root = from_dir;
    } else {
        root = to_dir;
    }
    // Skip root_dir path.
    const char *pp;
    if (len > root.length()) {
        // We are in a subdirectory of the root.
        pp = fpath+root.length();
    } else {
        pp = "";
    }

    if(!S_ISDIR(sb->st_mode)) {
        string p = pp;
        if (t == FROM) {
            from_files[p] = new Entry(sb);
        } else {
            to_files[p] = new Entry(sb);
        }
    }
        
    return 0;
}

void DiffTarredFS::compare() {

    map<string,EntryP,depthFirstSort> added;
    map<string,EntryP,depthFirstSort> deleted;
    map<string,EntryP,depthFirstSort> changed;

    size_t size_added=0, size_changed=0, size_deleted=0;
    
    for (auto & i : to_files) {
        if (from_files.count(i.first) == 0) {
            added[i.first] = i.second;
        } else {
            Entry *e = from_files[i.first];
            if (!e->same(i.second)) {
                changed[i.first] = i.second;
            }
        }
    }
    
    for (auto i : from_files) {
        if (to_files.count(i.first) == 0) {
            deleted[i.first] = i.second;
        } 
    }

    for (auto i : added) {
        string s = humanReadable(i.second->sb.st_size);
        printf("Added %s %s\n", i.first.c_str(), s.c_str());
        size_added += i.second->sb.st_size;
    }
    for (auto i : changed) {
        string s = humanReadable(i.second->sb.st_size);        
        printf("Changed %s %s\n", i.first.c_str(), s.c_str());
        size_changed += i.second->sb.st_size;
    }
    for (auto i : deleted) {
        string s = humanReadable(i.second->sb.st_size);        
        printf("Deleted %s %s\n", i.first.c_str(), s.c_str());
        size_deleted += i.second->sb.st_size;        
    }

    string up = humanReadable(size_added + size_changed);
    string del = humanReadable(size_deleted);
    printf("Uploading %s\n", up.c_str());
    if (size_deleted != 0) {
        printf("Deleting %s\n", del.c_str());
    }
}

static DiffTarredFS diff_fs;

template<DiffTarredFS*fs> struct AddFromFile {
    static int add(const char *fpath, const struct stat *sb, int tflag, struct FTW *ftwbuf) {
        return fs->addFromFile(fpath, sb, ftwbuf);
    }
};
template<DiffTarredFS*fs> struct AddToFile {
    static int add(const char *fpath, const struct stat *sb, int tflag, struct FTW *ftwbuf) {
        return fs->addToFile(fpath, sb, ftwbuf);
    }
};

void printDiffHelp(const char *app) {
    fprintf(stdout,
            "usage: %s [oldDirectory] [newDirectory]\n"
            "\n"
            "general options:\n"
            "    -h   --help      print help\n"
            "\n"
            , app);
}

string real(const char *p) {
    char tmp[PATH_MAX];
    const char *rc = realpath(p, tmp);
    if (!rc) {
        error(DIFF, "Could not find real path for %s\n", p);
    }                                               
    assert(rc == tmp);
    return string(tmp);
}


int main(int argc, char **argv) {

    if (argc < 3) {
        printDiffHelp(argv[0]);
        exit(0);
    }
    diff_fs.from_dir = real(argv[1]);
    diff_fs.to_dir = real(argv[2]);
    
    AddFromFile<&diff_fs> aff;
    AddToFile<&diff_fs> atf;

    diff_fs.recurse(FROM, aff.add);
    diff_fs.recurse(TO, atf.add);

    diff_fs.compare();
}
