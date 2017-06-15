/*
    Copyright (C) 2016-2017 Fredrik Öhrström

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

#include "diff.h"

#include <assert.h>
#include <ftw.h>
#include <limits.h>
#include <stddef.h>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <sys/types.h>
#include <sys/stat.h>
#include <fstream>
#include <unistd.h>
#include <utility>

#include "log.h"
#include "util.h"

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
    string s = (t==FROM)? from_dir->path() : to_dir->path();

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

    Path *p = Path::lookup(fpath);
    Path *root = to_dir;
    if (t == FROM) {
        root = from_dir;
    }
    Path *pp = p->subpath(root->depth());

    if(!S_ISDIR(sb->st_mode)) {
        if (t == FROM) {
            from_files[pp] = new Entry(sb);
        } else {
            to_files[pp] = new Entry(sb);
        }
    }

    return 0;
}

int DiffTarredFS::addLinesFromFile(Target t, Path *p) {
    vector<char> buf;

    ifstream is (p->path(), ifstream::binary);
    if (is) {
        is.seekg (0, is.end);
        size_t length = is.tellg();
        is.seekg (0, is.beg);
        char *tmp = new char[length];
        is.read (tmp,length);
        is.close();
        buf.insert(buf.end(), tmp, tmp+length);
        delete[] tmp;
    }
    return 0;
}

void DiffTarredFS::compare() {

    map<Path*,EntryP,depthFirstSortPath> added;
    map<Path*,EntryP,depthFirstSortPath> deleted;
    map<Path*,EntryP,depthFirstSortPath> changed;

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
        if (!list_mode_) {
            printf("Added %s %s\n", i.first->c_str(), s.c_str());
        }
        size_added += i.second->sb.st_size;
    }
    for (auto i : changed) {
        string s = humanReadable(i.second->sb.st_size);
        if (!list_mode_) {
            printf("Changed %s %s\n", i.first->c_str(), s.c_str());
        } else {
            printf("%s\n", i.first->c_str());
        }
        size_changed += i.second->sb.st_size;
    }
    for (auto i : deleted) {
        string s = humanReadable(i.second->sb.st_size);
        if (!list_mode_) {
            printf("Deleted %s %s\n", i.first->c_str(), s.c_str());
        } else {
            printf("%s\n", i.first->c_str());
        }
        size_deleted += i.second->sb.st_size;
    }

    if (!list_mode_) {
        string up = humanReadable(size_added + size_changed);
        string del = humanReadable(size_deleted);
        printf("Uploading %s\n", up.c_str());
        if (size_deleted != 0) {
            printf("Deleting %s\n", del.c_str());
        }
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
            "usage: %s [-h] [-l] [oldDirectory] [newDirectory]\n"
            "\n"
            "general options:\n"
            "    -h   --help      print help\n"
            "    -l               list old files being changed\n"
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

    for (int i = 1; argv[i] != 0; ++i)
    {
        if (!strcmp(argv[i], "-h"))
        {
            printDiffHelp(argv[0]);
            exit(1);
        }
        else if (!strcmp(argv[i], "-l"))
        {
            diff_fs.setListMode();
            eraseArg(i, &argc, argv);
            i--;
        }
    }
    
    diff_fs.setFromDir(Path::lookup(real(argv[1])));
    diff_fs.setToDir(Path::lookup(real(argv[2])));

    struct stat fs,ts;
    int rcf = stat(diff_fs.fromDir()->c_str(), &fs);
    int rct = stat(diff_fs.toDir()->c_str(), &ts);
    if (rcf) {
        error(DIFF, "Could not stat \"%s\"\n", diff_fs.fromDir()->c_str());
    }
    if (rct) {
        error(DIFF, "Could not stat \"%s\"\n", diff_fs.toDir()->c_str());
    }
    if (S_ISDIR(fs.st_mode) && S_ISDIR(ts.st_mode)) {
        AddFromFile<&diff_fs> aff;
        AddToFile<&diff_fs> atf;

        diff_fs.recurse(FROM, aff.add);
        diff_fs.recurse(TO, atf.add);
    } else if (S_ISREG(fs.st_mode) && S_ISREG(ts.st_mode)) {
        diff_fs.addLinesFromFile(FROM, diff_fs.fromDir());
        diff_fs.addLinesFromFile(TO, diff_fs.toDir());
    } else {
        error(DIFF, "Comparison must be between two files or two directories.");
    }

    diff_fs.compare();
}
