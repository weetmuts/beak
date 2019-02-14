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

enum class Action : short
{
    NoChange, Changed, Permission, Added, Removed
};

const char *actionName(Action a);

// How many files found for a given file type
// ie 723 Source files á 12374 KiB with
// suffixes (c,cc,h,perl)
struct TypeSummary
{
    size_t count {};
    size_t size {};
    set<const char *> suffixes;

    void add(const char *suffix, const FileStat *stat)
    {
        size += stat->st_size;
        count++;
        suffixes.insert(suffix);
        assert(suffix);
    }
};

class DirSummary
{
public:

    void removeDir();
    void addDir();

    void add(Action a, const FileInfo &fi, const FileStat *stat);
    void addChildren(Action a, const FileInfo &fi, const FileStat *stat);

    bool isChanged();
    bool isRemoved();
    bool isAdded();

    void print(Path *p, bool hide_content);

private:

    map<pair<Action,FileType>,TypeSummary> content_;
    map<pair<Action,FileType>,TypeSummary> all_content_;
    bool dir_removed_ = false;
    bool dir_added_ = false;
};

class DiffImplementation : public Diff
{
public:
    RC diff(FileSystem *old_fs, Path *old_path,
            FileSystem *curr_fs, Path *curr_path,
            ProgressStatistics *progress);

    void report();

    DiffImplementation() {
        dotgit_ = Atom::lookup(".git");
    }
    ~DiffImplementation() = default;

private:
    map<Path*,FileStat,TarSort> old;
    map<Path*,FileStat,TarSort> curr;
    map<Path*,DirSummary,TarSort> dirs;
    Atom *dotgit_;

    void addStats(Action a, Path *p, FileStat *stat);
    void addToDirSummary(Action a, Path *file_or_dir, FileStat *stat);

    bool should_hide_(Path *p)
    {
        p = p->parent();
        if (p && dirs.count(p)) {
            DirSummary *ds = &dirs[p];
            if (ds->isRemoved() || ds->isAdded()) {
                return true;
            }
        }
        while (p)
        {
            if (p->name() == dotgit_)
            {
                return true;
            }
            p = p->parent();
        }
        return false;
    }

    bool should_hide_content_(Path *p)
    {
        while (p) {
            if (p->name() == dotgit_) {
                return true;
            }
            p = p->parent();
        }
        return false;
    }

};

/////////////////////////////

const char *actionName(Action a)
{
    switch (a) {
    case Action::NoChange: return "unchanged";
    case Action::Changed: return "changed";
    case Action::Permission: return "permission changed";
    case Action::Added: return "added";
    case Action::Removed: return "removed";
    };
    assert(0);
}

unique_ptr<Diff> newDiff()
{
    return unique_ptr<Diff>(new DiffImplementation());
}


void DiffImplementation::addStats(Action a, Path *p, FileStat *stat)
{
    Path *dir = p->parent();
    DirSummary *dir_summary = &dirs[dir]; // Remember, [] will add missing entry automatically.
    FileInfo fi = fileInfo(p);
    dir_summary->add(a, fi, stat);

    while (dir->parent())
    {
        dir = dir->parent();
        DirSummary *parent_dir_summary = &dirs[dir];
        parent_dir_summary->addChildren(a, fi, stat);
    }
}

void DiffImplementation::addToDirSummary(Action a, Path *p, FileStat *stat)
{
    if (stat->isRegularFile())
    {
        addStats(a, p, stat);
    }
    else if (stat->isDirectory())
    {
        DirSummary *ds = &dirs[p]; // Remember, [] will add missing entry automatically.
        if (a == Action::Added)
        {
            ds->addDir();
        }
        else if (a == Action::Removed)
        {
            ds->removeDir();
        }
    }
}

/*
   beak diff work: s3_work_crypt:

   HomeAutomation/Docs

       7 documents changed (.docx)

   HomeAutomation/Development

       32 sources added (java,c,perl)
       2 sources changed (Makefile,c)
       32 object files added (class,o)
       1 library file changed (so)
       1 virtual image changed (vmk)
       3 other files changed (pof,xml,...)

   Media

       7 pictures changed (jpg)
       1 video added (mp4)

   Foobar/... removed (1287 files 8.5 GiB)

       32 sources removed (java,c,perl,bas)

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
        Path *entry = p.first;

        if (old.count(entry) == 1)
        {
            // File exists in curr and old, lets compare the stats.
            FileStat *newstat = &p.second;
            FileStat *oldstat = &old[entry];
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
                    debug(DIFF, "content diff %s\n", p.first->c_str());
                    addToDirSummary(Action::Changed, p.first, newstat);
                }
                if (!newstat->samePermissions(oldstat))
                {
                    debug(DIFF, "permission diff %s\n", p.first->c_str());
                    addToDirSummary(Action::Permission, p.first, newstat);
                }
            }
        } else {
            debug(DIFF, "new entry found %s\n", p.first->c_str());
            addToDirSummary(Action::Added, p.first, &p.second);
        }
    }

    for (auto& p : old)
    {
        Path *entry = p.first;

        if (curr.count(entry) == 0)
        {
            debug(DIFF, "removed entry found %s\n", p.first->c_str());
            addToDirSummary(Action::Removed, p.first, &p.second);
        }
    }

    return rc;
}

void DiffImplementation::report()
{
    for (auto &d : dirs)
    {
        DirSummary &ds = d.second;
        Path *p = d.first;
        if (!should_hide_(p)) {
            bool hc = should_hide_content_(p);
            ds.print(p, hc);
        }
    }

}


void DirSummary::add(Action a, const FileInfo &fi, const FileStat *stat)
{
    // Set the current dir summary
    TypeSummary *ts = &content_[{a,fi.type}];
    ts->add(fi.identifier, stat);
    // Add to the accumulated summary
    TypeSummary *tsall = &all_content_[{a,fi.type}];
    tsall->add(fi.identifier, stat);
}

void DirSummary::addChildren(Action a, const FileInfo &fi, const FileStat *stat)
{
    // Only to the accumulated summary
    TypeSummary *tsall = &all_content_[{a,fi.type}];
    tsall->add(fi.identifier, stat);
}

void DirSummary::removeDir()
{
    dir_removed_ = true;
}

void DirSummary::addDir()
{
    dir_added_ = true;
}

void DirSummary::print(Path *p, bool hide_content)
{
    map<pair<Action,FileType>,TypeSummary> *infos;

    if (dir_removed_)
    {
        if (all_content_.size() > 0)
        {
            printf("%s/... dir removed\n", p->c_str_nls());
        }
        else
        {
            printf("%s/ dir removed\n", p->c_str_nls());
            infos = &all_content_;
        }
        infos = &all_content_;
    }
    else if (dir_added_ )
    {
        if (all_content_.size() > 0)
        {
            printf("%s/... dir added\n", p->c_str_nls());
        }
        else
        {
            printf("%s/ dir added\n", p->c_str_nls());
            infos = &all_content_;
        }
        infos = &all_content_;
    }
    else if (hide_content)
    {
        // Hide the contents of this directory
        // and print a summary of all content.
        printf("%s/... \n", p->c_str_nls());
        infos = &all_content_;
    }
    else
    {
        if (content_.size() == 0)
        {
            debug(DIFF, "hiding dir with only timestamp change %s\n", p->c_str_nls());
            return;
        }
        // Print this directory only.
        printf("%s/\n", p->c_str_nls());
        infos = &content_;
    }
    debug(DIFF, "%s content.size=%zu allcontent.size=%zu dir_removed=%d dir_added=%d\n",
          p->c_str_nls(),
          content_.size(), all_content_.size(), dir_removed_, dir_added_);

    for (auto& a : *infos)
    {
        Action act = a.first.first;
        FileType ft = a.first.second;
        TypeSummary *st = &a.second;
        // 32 sources added (java,c,perl)
        printf("    %zu %s %s (", st->count, fileTypeName(ft, st->count > 1), actionName(act));
        bool comma = false;
        bool dotdotdot = false;
        int count = 0;
        for (auto s: st->suffixes) {
            if (++count > 10) {
                dotdotdot = true;
                break;
            }
            if (s[0] == 0) {
                // Found a file with no suffix, or unrecognizable suffix.
                // Render this as three dots last.
                dotdotdot = true;
            } else {
                if (comma) {
                    printf(",");
                } else {
                    comma = true;
                }
                printf("%s", s);
            }
        }
        if (dotdotdot) {
            if (comma) {
                printf(",");
            }
            printf("...");
        }
        printf(")\n");
    }
    printf("\n");
}

bool DirSummary::isRemoved()
{
    return dir_removed_;
}

bool DirSummary::isAdded()
{
    return dir_added_;
}
