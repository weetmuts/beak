/*
 Copyright (C) 2018 Fredrik Öhrström

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

#include "origintool.h"

#include "log.h"
#include "system.h"

//static ComponentId ORIGINTOOL = registerLogComponent("origintool");

using namespace std;

struct OriginToolImplementation : public OriginTool
{
    OriginToolImplementation(ptr<System> sys, ptr<FileSystem> sys_fs, ptr<FileSystem> origin_fs);

    void addReverseWork(StoreStatistics *st, Path *path, FileStat *stat, Options *settings,
                        ReverseTarredFS *rfs, PointInTime *point);

    ptr<FileSystem> fs() { return origin_fs_; }

    ptr<System> sys_;
    ptr<FileSystem> sys_fs_;
    ptr<FileSystem> origin_fs_;
};

unique_ptr<OriginTool> newOriginTool(ptr<System> sys,
                                       ptr<FileSystem> sys_fs,
                                       ptr<FileSystem> origin_fs)
{
    return unique_ptr<OriginTool>(new OriginToolImplementation(sys, sys_fs, origin_fs));
}


OriginToolImplementation::OriginToolImplementation(ptr<System>sys,
                                                     ptr<FileSystem> sys_fs,
                                                     ptr<FileSystem> origin_fs)
    : sys_(sys), sys_fs_(sys_fs), origin_fs_(origin_fs)
{
}

void addReverseWork(StoreStatistics *st,
                    Path *path, FileStat *stat,
                    Options *settings,
                    ReverseTarredFS *rfs, PointInTime *point,
                    FileSystem *to_fs)
{
    Entry *entry = rfs->findEntry(point, path);
    Path *file_to_extract = path->prepend(settings->from.storage->storage_location);

    if (entry->is_hard_link) st->num_hard_links++;
    else if (stat->isRegularFile()) {
        stat->checkStat(to_fs, file_to_extract);
        if (stat->disk_update == Store) {
            st->num_files_to_store++;
            st->size_files_to_store += stat->st_size;
        }
        st->num_files++;
        st->size_files += stat->st_size;
    }
    else if (stat->isSymbolicLink()) st->num_symbolic_links++;
    else if (stat->isDirectory()) st->num_dirs++;
    else if (stat->isFIFO()) st->num_nodes++;
}

void OriginToolImplementation::addReverseWork(StoreStatistics *st,
                                              Path *path, FileStat *stat, Options *settings,
                                              ReverseTarredFS *rfs, PointInTime *point)
{
    ::addReverseWork(st, path, stat, settings, rfs, point, origin_fs_.get());
}
