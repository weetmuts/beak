/*
 Copyright (C) 2019 Fredrik Öhrström

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

#include"fileinfo.h"
#include"log.h"

#include<map>
#include<set>
#include<utility>

using namespace std;

static ComponentId DIFF = registerLogComponent("diff");

enum class Action : short {
    NoChange, Changed, Permission, Added, Removed
};

const char *actionName(Action a) {
    switch (a) {
    case Action::NoChange: return "unchanged";
    case Action::Changed: return "changed";
    case Action::Permission: return "permission changed";
    case Action::Added: return "added";
    case Action::Removed: return "removed";
    };
    assert(0);
}
// How many files found for a given file type
// ie 723 Source files á 12374 KiB with
// suffixes (c,cc,h,perl)
struct TypeSummary
{
    size_t count {};
    size_t size {};
    set<const char *> suffixes;

    void add(const char *identifier, const FileStat *stat) {
        size += stat->st_size;
        count ++;
        suffixes.insert(identifier);
    }
};

struct DirSummary
{
    map<pair<Action,FileType>,TypeSummary> types;

    void add(Action a, const FileInfo &fi, const FileStat *stat) {
        TypeSummary *ts = &types[{a,fi.type}];
        ts->add(fi.identifier, stat);
    }

    void print(Path *p) {
        printf("\n%s:\n", p->c_str_nls());
        for (auto& a : types) {
            Action act = a.first.first;
            FileType ft = a.first.second;
            TypeSummary *st = &a.second;
            // 32 sources added (java,c,perl)
            const char *an_s = NULL;
            an_s = "";
            if (st->count > 1) {
                an_s = "s";
            }
            printf("%zu %s%s %s (", st->count, fileTypeName(ft), an_s, actionName(act));
            for (auto s: st->suffixes) {
                printf("%s", s);
            }
            printf(")\n");
        }
    }
};

class DiffImplementation : public Diff
{
    map<Path*,FileStat,depthFirstSortPath> old;
    map<Path*,FileStat,depthFirstSortPath> curr;
    // Remember the summaries for each directory.
    map<Path*,DirSummary,depthFirstSortPath> dirs;

    // Directories to skip: .beak
    set<Path*> skip_these_directories;

    // The grouped directories are for example: .git .hg
    // For these, diff, will report a summary of changed/added/removed inside the dir.
    set<Path*> group_these_directories;

    set<Path*> files_with_changed_permissions;
    set<Path*> files_with_changed_mtime;
    set<Path*> files_removed;
    set<Path*> files_added;

    map<Path*,int> entries_removed;
    map<Path*,int> entries_added;
    map<Path*,size_t> entries_removed_size;
    map<Path*,size_t> entries_added_size;

    bool changes_found {};

    RC diff(FileSystem *old_fs, Path *old_path,
            FileSystem *curr_fs, Path *curr_path,
            ProgressStatistics *progress);

    void report();

    void add(Action a, Path *p, FileStat *stat);

    ~DiffImplementation() = default;
};

unique_ptr<Diff> newDiff()
{
    return unique_ptr<Diff>(new DiffImplementation());
}

void DiffImplementation::add(Action a, Path *p, FileStat *stat)
{
    if (stat->isRegularFile())
    {
        Path *dir = p->parent();
        DirSummary *dir_summary = &dirs[dir]; // Remember, [] will add missing entry automatically.
        FileInfo fi = fileInfo(p);
        dir_summary->add(a, fi, stat);
    }
}

/*
   beak diff work: s3_work_crypt:

   HomeAutomation/Docs:

   7 documents changed (.docx)

   HomeAutomation/Development:

   32 sources added (java,c,perl)
   2 sources changed (Makefile,c)
   32 object files added (class,o)
   1 library file changed (so)
   1 virtual image changed (vmk)
   3 other files changed (pof,xml,...)

   Media:
   7 pictures changed (jpg)
   1 video added (mp4)

   Foobar/... removed (1287 files 8.5 GiB)
   Local/Barfoo/... added (923 files 128.5 GiB)
*/

RC DiffImplementation::diff(FileSystem *old_fs, Path *old_path,
                            FileSystem *curr_fs, Path *curr_path,
                            ProgressStatistics *progress)
{
    RC rc = RC::OK;
    int depth = old_path->depth();
    Atom *dotbeak = Atom::lookup(".beak");

    // Recurse the old file system
    rc = old_fs->recurse(old_path,
                         [&](Path *path, FileStat *stat)
                         {
                             if (path->depth() > depth) {
                                 if (path->name() == dotbeak) {
                                     // Ignore .beak directories and their contents.
                                     debug(DIFF, "Skipping in old: \"%s\"\n", path->c_str());
                                     return RecurseSkipSubTree;
                                 }
                                 Path *p = path->subpath(depth)->prepend(Path::lookupRoot());
                                 old[p] = *stat;
                                 debug(DIFF, "Old \"%s\"\n", p->c_str());
                             }
                             return RecurseContinue;
                         }
        );

    // Recurse the new file system
    depth = curr_path->depth();
    rc = curr_fs->recurse(curr_path,
                            [&](Path *path, FileStat *stat)
                            {
                                if (path->depth() > depth) {
                                    if (path->name() == dotbeak) {
                                        // Ignore .beak directories and their contents.
                                        debug(DIFF, "Skipping \"%s\"\n", path->c_str());
                                        return RecurseSkipSubTree;
                                    }
                                    Path *p = path->subpath(depth)->prepend(Path::lookupRoot());
                                    curr[p] = *stat;
                                    debug(DIFF, "Curr \"%s\"\n", p->c_str());
                                }
                                return RecurseContinue;
                            }
        );


    for (auto& p : curr)
    {
        if (old.count(p.first) == 1)
        {
            // File exists in curr and old, lets compare the stats.
            FileStat *newstat = &p.second;
            FileStat *oldstat = &old[p.first];
            if (newstat->isRegularFile())
            {
                if (newstat->hard_link) {
                    debug(DIFF, "Hard link in new: %s\n", newstat->hard_link->c_str());
                }
                if (oldstat->hard_link) {
                    debug(DIFF, "Hard link in old: %s\n", oldstat->hard_link->c_str());
                    if (old.count(oldstat->hard_link) > 0) {
                        oldstat = &old[oldstat->hard_link];
                        debug(DIFF, "Followed hard link\n");
                    }
                }
                if (!newstat->sameSize(oldstat) ||
                    !newstat->sameMTime(oldstat))
                {
                    debug(DIFF, "Content diff %s\n", p.first->c_str());
                    files_with_changed_mtime.insert(p.first);
                    changes_found = true;
                    add(Action::Changed, p.first, newstat);
                }
                if (!newstat->samePermissions(oldstat))
                {
                    debug(DIFF, "Permission diff %s\n", p.first->c_str());
                    files_with_changed_permissions.insert(p.first);
                    changes_found = true;
                    add(Action::Permission, p.first, newstat);
                }
            }
        } else {
            // New file found
            debug(DIFF, "New file found %s\n", p.first->c_str());
            files_added.insert(p.first);
            add(Action::Added, p.first, &p.second);
            changes_found = true;
        }
    }

    for (auto& p : old)
    {
        if (curr.count(p.first) == 0)
        {
            // File that disappeard found.
            debug(DIFF, "Removed file found %s\n", p.first->c_str());
            files_removed.insert(p.first);
            add(Action::Removed, p.first, &p.second);
            changes_found = true;
        }
    }

    return rc;
}

void DiffImplementation::report()
{
    for (auto &d : dirs)
    {
        d.second.print(d.first);
    }

    /*
    for (auto& p : files_with_changed_mtime)
    {
        string hr = humanReadable(p);
        printf("    changed:       %s (%s)\n", p.first->c_str(), hr.c_str());
    }

    for (auto p : files_with_changed_permissions)
    {
        printf(" permission:       %s\n", p->c_str());
    }

    for (auto p : files_added)
    {
        Path *par = p->parent();
        if (par == NULL || !mapContains(files_added, par)) {
            // The parent is either the root or it has not been added.
            // Therefore we should print this added entry.
            int c = 0;
            if (mapContains(entries_added,p)) {
                c = entries_added[p];
            }
            if (c > 0) {
                // This is a a new directory with children.
                string hr = humanReadable(entries_added_size[p]);
                printf("      added:       %s/... (%d entries %s)\n", p->c_str(), c, hr.c_str());
            } else {
                // This is a file, or empty directory.
                printf("      added:       %s\n", p->c_str());
            }
        }
    }

    for (auto p : files_removed)
    {
        Path *par = p->parent();
        if (par == NULL || !mapContains(files_removed, par)) {
            // The parent is either the root or it has not been removed.
            // Therefore we should print this removed entry.
            int c = 0;
            if (mapContains(entries_removed,p)) {
                c = entries_removed[p];
            }
            if (c > 0) {
                // This is a removed directory, with children now removed.
                string hr = humanReadable(entries_removed_size[p]);
                printf("    removed:       %s/... (%d entries %s)\n", p->c_str(), c, hr.c_str());
            } else {
                // This is a file, or empty directory.
                printf("    removed:       %s\n", p->c_str());
            }
        }
    }

    if (changes_found) {
        printf("\nSummary: ");
    }

    bool comma_needed = false;
    if (files_with_changed_mtime.size() > 0) {
        string hr = humanReadable(sum_of_all_changed_mtime_files_sizes);
        printf("%zu changed (%s)", files_with_changed_mtime .size(), hr.c_str());
        changes_found = true;
        comma_needed = true;
    }
    if (files_with_changed_permissions.size() > 0) {
        if (comma_needed) printf(", ");
        printf("%zu permissions", files_with_changed_permissions.size());
        changes_found = true;
        comma_needed = true;
    }
    if (files_added.size() > 0) {
        if (comma_needed) printf(", ");
        string hr = humanReadable(sum_of_all_added_files_sizes);
        printf("%zu added (%s)", files_added.size(), hr.c_str());
        changes_found = true;
        comma_needed = true;
    }
    if (files_removed.size() > 0) {
        if (comma_needed) printf(", ");
        string hr = humanReadable(sum_of_all_removed_files_sizes);
        printf("%zu removed (%s)", files_removed.size(), hr.c_str());
        changes_found = true;
    }

    if (changes_found) {
        printf("\n");
    }
    */
}
