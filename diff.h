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

#ifndef DIFF_H
#define DIFF_H

#include <string.h>
#include <sys/stat.h>
#include <map>
#include <string>

using namespace std;

typedef int (*FileCB)(const char *,const struct stat *,int,struct FTW *);

struct Entry {
    struct stat sb;

    Entry() { }
    Entry(const struct stat *s) { memcpy(&sb, s, sizeof(struct stat)); }

    bool same(Entry *e);
};

typedef Entry *EntryP;

enum Target { FROM, TO };

struct DiffTarredFS {
    Path *from_dir;
    Path *to_dir;

    map<Path*,EntryP,depthFirstSortPath> from_files;
    map<Path*,EntryP,depthFirstSortPath> to_files;

    int recurse(Target t, FileCB cb);
    int addFromFile(const char *fpath, const struct stat *sb, struct FTW *ftwbuf);
    int addToFile(const char *fpath, const struct stat *sb, struct FTW *ftwbuf);
    int addFile(Target t, const char *fpath, const struct stat *sb, struct FTW *ftwbuf);
    void compare();

};

#endif
