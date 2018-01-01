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

#ifndef DIFF_H
#define DIFF_H

#include <map>
#include <string>

#include "filesystem.h"

struct DiffEntry {
    //FileStat fs;

    //DiffEntry(FileStat *s) { if (s) memcpy(&fs, s, sizeof(struct stat)); }

    bool same(DiffEntry *e);
};

enum Target { FROM, TO };

struct DiffTarredFS {
    int loadZ01File(Target ft, Path *file);
    void compare();

    /*
    int addFromFile(const char *fpath, FileStat *s, struct FTW *ftwbuf);
    int addToFile(const char *fpath, FileStat *s, struct FTW *ftwbuf);
    int addFile(Target t, const char *fpath, FileStat *s, struct FTW *ftwbuf);
    int addLinesFromFile(Target t, Path *p);
    void compare();

    Path *fromDir() { return from_dir; }
    Path *toDir() { return to_dir; }
    void setFromDir(Path *p) { from_dir = p; }
    void setToDir(Path *p) { to_dir = p; }
    void setListMode() { list_mode_ = true; }
    */
private:
    /*
    bool list_mode_ = false;
    Path *from_dir;
    Path *to_dir;
    */
    std::map<Path*,DiffEntry,depthFirstSortPath> from_files;
    std::map<Path*,DiffEntry,depthFirstSortPath> to_files;
};

#endif
