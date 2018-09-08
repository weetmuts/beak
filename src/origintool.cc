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

static ComponentId ORIGINTOOL = registerLogComponent("origintool");

using namespace std;

struct OriginToolImplementation : public OriginTool
{
    OriginToolImplementation(ptr<System> sys, ptr<FileSystem> sys_fs, ptr<FileSystem> origin_fs);

    RC restoreFileSystemIntoOrigin(FileSystem *fs);

    void addRestoreWork(ptr<StoreStatistics> st, Path *path, FileStat *stat, Options *settings,
                        ReverseTarredFS *rfs, PointInTime *point);

    void restoreFileSystem(FileSystem *view, ReverseTarredFS *rfs, PointInTime *point,
                           Options *settings, ptr<StoreStatistics> st, FileSystem *storage_fs);

    ptr<FileSystem> fs() { return origin_fs_; }

    bool extractHardLink(Path *target,
                         Path *dst_root, Path *file_to_extract, FileStat *stat,
                         ptr<StoreStatistics> statistics);
    void handleHardLinks(Path *path, FileStat *stat,
                         ReverseTarredFS *rfs, PointInTime *point,
                         Options *settings, ptr<StoreStatistics> st);
    bool extractFile(Entry *entry,
                     FileSystem *storage_fs, Path *tar_file, off_t tar_file_offset,
                     Path *file_to_extract, FileStat *stat,
                     ptr<StoreStatistics> statistics);

    void handleRegularFiles(Path *path, FileStat *stat,
                            ReverseTarredFS *rfs, PointInTime *point,
                            Options *settings, ptr<StoreStatistics> st,
                            FileSystem *storage_fs);

    bool extractSymbolicLink(string target,
                             Path *file_to_extract, FileStat *stat,
                             ptr<StoreStatistics> statistics);

    bool extractNode(Path *file_to_extract, FileStat *stat,
                     ptr<StoreStatistics> statistics);

    bool chmodDirectory(Path *file_to_extract, FileStat *stat,
                        ptr<StoreStatistics> statistics);

    void handleNodes(Path *path, FileStat *stat,
                     ReverseTarredFS *rfs, PointInTime *point,
                     Options *settings, ptr<StoreStatistics> st,
                     FileSystem *storage_fs);

    void handleSymbolicLinks(Path *path, FileStat *stat,
                             ReverseTarredFS *rfs, PointInTime *point,
                             Options *settings, ptr<StoreStatistics> st,
                             FileSystem *storage_fs);

    void handleDirs(Path *path, FileStat *stat,
                    ReverseTarredFS *rfs, PointInTime *point,
                    Options *settings, ptr<StoreStatistics> st);

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

RC OriginToolImplementation::restoreFileSystemIntoOrigin(FileSystem *fs)
{
    return RC::OK;
}

void OriginToolImplementation::addRestoreWork(ptr<StoreStatistics> st,
                                              Path *path, FileStat *stat, Options *settings,
                                              ReverseTarredFS *rfs, PointInTime *point)
{
    Entry *entry = rfs->findEntry(point, path);
    Path *file_to_extract = path->prepend(settings->to.origin);
    if (entry->is_hard_link) st->stats.num_hard_links++;
    else if (stat->isRegularFile()) {
        stat->checkStat(origin_fs_.get(), file_to_extract);
        if (stat->disk_update == Store) {
            st->stats.num_files_to_store++;
            st->stats.size_files_to_store += stat->st_size;
        }
        st->stats.num_files++;
        st->stats.size_files += stat->st_size;
    }
    else if (stat->isSymbolicLink()) st->stats.num_symbolic_links++;
    else if (stat->isDirectory()) st->stats.num_dirs++;
    else if (stat->isFIFO()) st->stats.num_nodes++;
}

bool OriginToolImplementation::extractHardLink(Path *target,
                                               Path *dst_root, Path *file_to_extract, FileStat *stat,
                                               ptr<StoreStatistics> statistics)
{
    target = target->prepend(dst_root);
    FileStat target_stat;
    RC rc = origin_fs_->stat(target, &target_stat);
    if (rc.isErr()) {
        error(ORIGINTOOL, "Cannot extract hard link %s because target %s does not exist!\n",
              file_to_extract->c_str(), target->c_str());
    }
    if (!stat->samePermissions(&target_stat)) {
        error(ORIGINTOOL, "Hard link target must have same permissions as hard link definition!\n"
              "Expected %s to have permissions %s\n", target->c_str(), permissionString(&target_stat).c_str());
    }
    if (!stat->sameMTime(&target_stat)) {
        error(ORIGINTOOL, "Hard link target must have same MTime as hard link definition!\n"
              "Expected %s to have mtime xxx\n", target->c_str());
    }
    FileStat old_stat;
    rc = origin_fs_->stat(file_to_extract, &old_stat);
    if (rc.isOk()) {
        if (stat->samePermissions(&old_stat) &&
            target_stat.sameSize(&old_stat) && // The hard link definition does not have size.
            stat->sameMTime(&old_stat)) {
            debug(ORIGINTOOL, "Skipping hard link \"%s\"\n", file_to_extract->c_str());
            return false;
        }
    }

    debug(ORIGINTOOL, "Storing hard link %s to %s\n", file_to_extract->c_str(), target->c_str());

    origin_fs_->mkDirpWriteable(file_to_extract->parent());
    origin_fs_->createHardLink(file_to_extract, stat, target);
    origin_fs_->utime(file_to_extract, stat);
    statistics->stats.num_hard_links_stored++;
    verbose(ORIGINTOOL, "Stored hard link %s\n", file_to_extract->c_str());
    statistics->updateProgress();
    return true;
}

bool OriginToolImplementation::extractFile(Entry *entry,
                                           FileSystem *storage_fs, Path *tar_file, off_t tar_file_offset,
                                           Path *file_to_extract, FileStat *stat,
                                           ptr<StoreStatistics> statistics)
{
    if (stat->disk_update == NoUpdate) {
        debug(ORIGINTOOL, "Skipping file \"%s\"\n", file_to_extract->c_str());
        return false;
    }
    if (stat->disk_update == UpdatePermissions) {
        origin_fs_->chmod(file_to_extract, stat);
        verbose(ORIGINTOOL, "Updating permissions for file \"%s\" to %o\n", file_to_extract->c_str(), stat->st_mode);
        return false;
    }

    debug(ORIGINTOOL, "Storing file \"%s\" size %ju permissions %s\n   using tar \"%s\" offset %ju\n",
          file_to_extract->c_str(), stat->st_size, permissionString(stat).c_str(),
          tar_file->c_str(), tar_file_offset);

    origin_fs_->mkDirpWriteable(file_to_extract->parent());
    origin_fs_->createFile(file_to_extract, stat,
        [storage_fs,tar_file_offset,file_to_extract,tar_file] (off_t offset, char *buffer, size_t len)
        {
            debug(ORIGINTOOL,"Extracting %ju bytes to file %s\n", len, file_to_extract->c_str());
            ssize_t n = storage_fs->pread(tar_file, buffer, len, tar_file_offset + offset);
            debug(ORIGINTOOL, "Extracted %ju bytes from %ju to %ju.\n", n,
                  tar_file_offset+offset, offset);
            return n;
        });

    origin_fs_->utime(file_to_extract, stat);
    statistics->stats.num_files_stored++;
    statistics->stats.size_files_stored+=stat->st_size;
    verbose(ORIGINTOOL, "Stored %s (%ju %s %06o)\n",
            file_to_extract->c_str(), stat->st_size, permissionString(stat).c_str(), stat->st_mode);
    statistics->updateProgress();
    return true;
}

bool OriginToolImplementation::extractSymbolicLink(string target,
                                                   Path *file_to_extract, FileStat *stat,
                                                   ptr<StoreStatistics> statistics)
{
    string old_target;
    FileStat old_stat;
    RC rc = origin_fs_->stat(file_to_extract, &old_stat);
    bool found = rc.isOk();
    if (found) {
        if (stat->samePermissions(&old_stat) &&
            stat->sameSize(&old_stat) &&
            stat->sameMTime(&old_stat)) {
            if (origin_fs_->readLink(file_to_extract, &old_target)) {
                if (target == old_target) {
                    debug(ORIGINTOOL, "Skipping existing link %s\n", file_to_extract->c_str());
                    return false;
                }
            }
        }
    }

    debug(ORIGINTOOL, "Storing symlink %s to %s\n", file_to_extract->c_str(), target.c_str());

    origin_fs_->mkDirpWriteable(file_to_extract->parent());
    if (found) {
        origin_fs_->deleteFile(file_to_extract);
    }
    origin_fs_->createSymbolicLink(file_to_extract, stat, target);
    origin_fs_->utime(file_to_extract, stat);
    statistics->stats.num_symbolic_links_stored++;
    verbose(ORIGINTOOL, "Stored symlink %s\n", file_to_extract->c_str());
    statistics->updateProgress();
    return true;
}

bool OriginToolImplementation::extractNode(Path *file_to_extract, FileStat *stat,
                                           ptr<StoreStatistics> statistics)
{
    FileStat old_stat;
    RC rc = origin_fs_->stat(file_to_extract, &old_stat);
    if (rc.isOk()) {
        if (stat->samePermissions(&old_stat) &&
            stat->sameMTime(&old_stat)) {
            // Compare of size is ignored since the nodes have no size...
            debug(ORIGINTOOL, "Skipping mknod of \"%s\"\n", file_to_extract->c_str());
            return false;
        }
    }

    if (stat->isFIFO()) {
        debug(ORIGINTOOL, "Storing FIFO %s\n", file_to_extract->c_str());
        origin_fs_->mkDirpWriteable(file_to_extract->parent());
        origin_fs_->createFIFO(file_to_extract, stat);
        origin_fs_->utime(file_to_extract, stat);
        verbose(ORIGINTOOL, "Stored fifo %s\n", file_to_extract->c_str());
        statistics->updateProgress();
    }
    return true;
}

bool OriginToolImplementation::chmodDirectory(Path *dir_to_extract, FileStat *stat,
                                              ptr<StoreStatistics> statistics)
{
    FileStat old_stat;
    RC rc = origin_fs_->stat(dir_to_extract, &old_stat);
    if (rc.isOk()) {
        if (stat->samePermissions(&old_stat) &&
            stat->sameMTime(&old_stat)) {
            // Compare of directory size is ignored since the size differ between
            // different file systems.
            debug(ORIGINTOOL, "Skipping chmod of dir \"%s\"\n", dir_to_extract->c_str());
            return false;
        }
    }

    debug(ORIGINTOOL, "Chmodding directory %s %s\n", dir_to_extract->c_str(),
          permissionString(stat).c_str());

    origin_fs_->mkDirpWriteable(dir_to_extract);
    origin_fs_->chmod(dir_to_extract, stat);
    origin_fs_->utime(dir_to_extract, stat);
    statistics->stats.num_dirs_updated++;
    verbose(ORIGINTOOL, "Updated dir %s\n", dir_to_extract->c_str());
    statistics->updateProgress();
    return true;
}

void OriginToolImplementation::handleHardLinks(Path *path, FileStat *stat,
                                               ReverseTarredFS *rfs, PointInTime *point,
                                               Options *settings, ptr<StoreStatistics> st)
{
    auto entry = rfs->findEntry(point, path);

    if (entry->is_hard_link) {
        auto file_to_extract = path->prepend(settings->to.origin);
        // Special case since hard links are not encoded in stat structure.
        extractHardLink(entry->hard_link,
                        settings->to.origin,
                        file_to_extract, stat, st);
    }
}

void OriginToolImplementation::handleRegularFiles(Path *path, FileStat *stat,
                                                  ReverseTarredFS *rfs, PointInTime *point,
                                                  Options *settings, ptr<StoreStatistics> st,
                                                  FileSystem *storage_fs)
{
    auto entry = rfs->findEntry(point, path);
    auto tar_file = entry->tar->prepend(settings->from.storage->storage_location);
    auto tar_file_offset = entry->offset;
    auto file_to_extract = path->prepend(settings->to.origin);

    if (!entry->is_hard_link && stat->isRegularFile()) {
        extractFile(entry, storage_fs, tar_file, tar_file_offset,
                    file_to_extract, stat, st);
        //st->num_files_handled++;
        //st->size_files_handled += stat->st_size;
    }
}

void OriginToolImplementation::handleNodes(Path *path, FileStat *stat,
                                           ReverseTarredFS *rfs, PointInTime *point,
                                           Options *settings, ptr<StoreStatistics> st,
                                           FileSystem *storage_fs)
{
    auto entry = rfs->findEntry(point, path);
    auto file_to_extract = path->prepend(settings->to.origin);

    if (!entry->is_hard_link && stat->isFIFO()) {
        extractNode(file_to_extract, stat, st);
    }
}

void OriginToolImplementation::handleSymbolicLinks(Path *path, FileStat *stat,
                                                   ReverseTarredFS *rfs, PointInTime *point,
                                                   Options *settings, ptr<StoreStatistics> st,
                                                   FileSystem *storage_fs)
{
    auto entry = rfs->findEntry(point, path);
    auto file_to_extract = path->prepend(settings->to.origin);

    if (!entry->is_hard_link && stat->isSymbolicLink()) {
        extractSymbolicLink(entry->symlink, file_to_extract, stat, st);
    }
}

void OriginToolImplementation::handleDirs(Path *path, FileStat *stat,
                                          ReverseTarredFS *rfs, PointInTime *point,
                                          Options *settings, ptr<StoreStatistics> st)
{
    auto file_to_extract = path->prepend(settings->to.origin);

    if (stat->isDirectory()) {
        chmodDirectory(file_to_extract, stat, st);
    }
}


void OriginToolImplementation::restoreFileSystem(FileSystem *view, ReverseTarredFS *rfs, PointInTime *point,
                                                 Options *settings, ptr<StoreStatistics> st, FileSystem *storage_fs)
{
    // First restore the files,nodes and symlinks and their contents, set the utimes properly for the files.
    Path *r = Path::lookupRoot();
    view->recurse(r, [=](Path *path, FileStat *stat) {handleRegularFiles(path,stat,rfs,point,settings,st,storage_fs); });
    view->recurse(r, [=](Path *path, FileStat *stat) {handleNodes(path,stat,rfs,point,settings,st,storage_fs); });
    view->recurse(r, [=](Path *path, FileStat *stat) {handleSymbolicLinks(path,stat,rfs,point,settings,st,storage_fs); });
    // Then restore the hard links
    view->recurse(r, [=](Path *path, FileStat *stat) {handleHardLinks(path,stat,rfs,point,settings,st); });
    // Then recreate any missing not yet created directories and set the utimes of all the dirs.
    view->recurse(r, [=](Path *path, FileStat *stat) {handleDirs(path,stat,rfs,point,settings,st); });
}
