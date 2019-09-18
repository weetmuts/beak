/*
 Copyright (C) 2018-2019 Fredrik Öhrström

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

#include "storagetool.h"

#include "backup.h"
#include "filesystem_helpers.h"
#include "log.h"
#include "monitor.h"
#include "system.h"
#include "storage_rclone.h"
#include "storage_rsync.h"

#include <algorithm>
#include <unistd.h>

static ComponentId STORAGETOOL = registerLogComponent("storagetool");
static ComponentId CACHE = registerLogComponent("cache");

using namespace std;

struct StorageToolImplementation : public StorageTool
{
    StorageToolImplementation(ptr<System> sys, ptr<FileSystem> local_fs);

    RC storeBackupIntoStorage(Backup *backup,
                              Storage *storage,
                              Settings *settings,
                              ProgressStatistics *progress);

    RC copyBackupIntoStorage(Backup *backup,
                             Path *backup_dir,
                             FileSystem *backup_fs,
                             Storage *storage,
                             Settings *settings,
                             ProgressStatistics *progress);

    RC listPointsInTime(Storage *storage, vector<pair<Path*,struct timespec>> *v,
                        ProgressStatistics *progress);

    RC removeBackupFiles(Storage *storage,
                         std::vector<Path*>& files,
                         ProgressStatistics *progress);

    FileSystem *asCachedReadOnlyFS(Storage *storage,
                                   Monitor *monitor);

    FileSystem *asStatOnlyFS(Storage *storage,
                             Monitor *monitor);

    System *sys_;
    FileSystem *local_fs_;
};

unique_ptr<StorageTool> newStorageTool(ptr<System> sys,
                                       ptr<FileSystem> local_fs)
{
    return unique_ptr<StorageTool>(new StorageToolImplementation(sys, local_fs));
}

StorageToolImplementation::StorageToolImplementation(ptr<System>sys,
                                                     ptr<FileSystem> local_fs)
    : sys_(sys), local_fs_(local_fs)
{

}

void add_backup_work(ProgressStatistics *progress,
                     vector<Path*> *files_to_backup,
                     Path *path,
                     FileStat *stat,
                     Path *storage_location,
                     FileSystem *to_fs)
{
    Path *file_to_extract = path->prepend(storage_location);

    // The backup fs has already wrapped any non-regular files inside tars.
    // Thus we only want to send those to the cloud storage site.
    if (stat->isRegularFile())
    {
        // Remember the size of this file. This is necessary to
        // know how many bytes has been transferred when
        // rclone/rsync later reports that a file has been successfully sent.
        assert(progress->stats.file_sizes.count(file_to_extract) == 0);
        progress->stats.file_sizes[file_to_extract] = stat->st_size;

        // Compare our local file with the stats of the one stored remotely.
        stat->checkStat(to_fs, file_to_extract);

        if (stat->disk_update == Store) {
            // Yep, we need to store the local file remotely.
            // The remote might be missing, or it has the wrong size/date.

            // Accumulate the count/size of files to be uploaded.
            progress->stats.num_files_to_store++;
            progress->stats.size_files_to_store += stat->st_size;
            // Remember the files to be uploaded.
            files_to_backup->push_back(path);
        }
        // Accumulate the total count of files.
        progress->stats.num_files++;
        progress->stats.size_files+=stat->st_size;
    }
    else if (stat->isDirectory())
    {
        // We count the directories, only for information.
        // Directories are created implicitly on the storage side anyway.
        progress->stats.num_dirs++;
    }
}

void store_local_backup_file(Backup *backup,
                             FileSystem *origin_fs,
                             FileSystem *storage_fs,
                             Path *path,
                             FileStat *stat,
                             Settings *settings,
                             ProgressStatistics *progress)
{
    if (!stat->isRegularFile()) return;

    uint partnr;
    TarFile *tarr = backup->findTarFromPath(path, &partnr);
    assert(tarr);

    Path *file_name = path->prepend(settings->to.storage->storage_location);
    storage_fs->mkDirpWriteable(file_name->parent());
    FileStat old_stat;
    RC rc = storage_fs->stat(file_name, &old_stat);
    if (rc.isOk() &&
        stat->samePermissions(&old_stat) &&
        stat->sameSize(&old_stat) &&
        stat->sameMTime(&old_stat))
    {
        verbose(STORAGETOOL, "up to date %s\n", file_name->c_str());
    }
    else
    {
        if (rc.isOk())
        {
            storage_fs->deleteFile(file_name);
        }
        // The size gets incrementally update while the tar file is written!
        auto func = [&progress](size_t n){ progress->stats.size_files_stored += n; };
        tarr->createFilee(file_name, stat, partnr, origin_fs, storage_fs, 0, func);

        storage_fs->utime(file_name, stat);
        progress->stats.num_files_stored++;
        progress->updateProgress();
        verbose(STORAGETOOL, "stored %s\n", file_name->c_str());
    }
}

void copy_local_backup_file(Path *relpath,
                            Path *source_location,
                            FileSystem *source_fs,
                            FileStat *stat,
                            Path *dest_location,
                            FileSystem *dest_fs,
                            ProgressStatistics *progress)
{
    debug(STORAGETOOL, "copy %s ## %s to %s ## %s\n",
          source_location->c_str(),
          relpath->c_str(),
          dest_location->c_str(),
          relpath->c_str());

    if (!stat->isRegularFile()) return;

    Path *from_file_name = relpath->prepend(source_location);
    Path *to_file_name = relpath->prepend(dest_location);
    dest_fs->mkDirpWriteable(to_file_name->parent());
    FileStat old_stat;
    RC rc = dest_fs->stat(to_file_name, &old_stat);
    if (rc.isOk() &&
        stat->samePermissions(&old_stat) &&
        stat->sameSize(&old_stat) &&
        stat->sameMTime(&old_stat))
    {
        verbose(STORAGETOOL, "up to date %s\n", to_file_name->c_str());
    }
    else
    {
        if (rc.isOk())
        {
            dest_fs->deleteFile(to_file_name);
        }
        // The size gets incrementally update while the tar file is written!
        auto update_progress = [&progress](size_t n){ progress->stats.size_files_stored += n; };
        dest_fs->createFile(to_file_name, stat,
                            [&] (off_t offset, char *buffer, size_t len) {
                                debug(STORAGETOOL,"Copy %ju bytes to file %s\n", len, to_file_name->c_str());
                                size_t n = source_fs->pread(from_file_name, buffer, len, offset);
                                debug(STORAGETOOL, "Copied %ju bytes from %ju.\n", n, offset);
                                update_progress(n);
                                return n;
                               });

        dest_fs->utime(to_file_name, stat);
        progress->stats.num_files_stored++;
        progress->updateProgress();
        verbose(STORAGETOOL, "copied %s\n", to_file_name->c_str());
    }
}

RC StorageToolImplementation::storeBackupIntoStorage(Backup  *backupp,
                                                     Storage *storage,
                                                     Settings *settings,
                                                     ProgressStatistics *progress)
{
    // The backup archive files (.tar .gz) are found here.
    FileSystem *backup_fs = backupp->asFileSystem();
    // The where the origin files can be found.
    FileSystem *origin_fs = backupp->originFileSystem();
    // Store the archive files here.
    FileSystem *storage_fs = NULL;
    // This is the list of files to be sent to the storage.
    vector<Path*> files_to_backup;

    map<Path*,FileStat> contents;
    unique_ptr<FileSystem> fs;
    if (storage->type == FileSystemStorage)
    {
        storage_fs = local_fs_;
    }
    else
    if (storage->type == RCloneStorage || storage->type == RSyncStorage)
    {
        vector<TarFileName> files, bad_files;
        vector<string> other_files;
        RC rc = RC::OK;
        if (storage->type == RCloneStorage)
        {
            rc = rcloneListBeakFiles(storage, &files, &bad_files, &other_files, &contents, sys_, progress);
        }
        else
        {
            rc = rsyncListBeakFiles(storage, &files, &bad_files, &other_files, &contents, sys_, progress);
        }
        if (rc.isErr())
        {
            error(STORAGETOOL, "Could not list files in rclone storage %s\n", storage->storage_location->c_str());
        }

        // Present the listed files at the storage site as a read only file system
        // that can only be listed and stated. This is enough to determine if
        // the storage site files need to be updated.
        fs = newStatOnlyFileSystem(sys_, contents);
        storage_fs = fs.get();
    }
    backup_fs->recurse(Path::lookupRoot(), [=,&files_to_backup]
                       (Path *path, FileStat *stat) {
                           add_backup_work(progress, &files_to_backup, path, stat,
                                           settings->to.storage->storage_location,
                                           storage_fs);
                           return RecurseContinue;
                       });

    debug(STORAGETOOL, "work to be done: num_files=%ju num_dirs=%ju\n", progress->stats.num_files, progress->stats.num_dirs);

    switch (storage->type) {
    case FileSystemStorage:
    {
        backup_fs->recurse(Path::lookupRoot(), [=]
                           (Path *path, FileStat *stat) {
                               store_local_backup_file(backupp,
                                                       origin_fs,
                                                       storage_fs,
                                                       path,
                                                       stat,
                                                       settings,
                                                       progress);
                               return RecurseContinue; });
        break;
    }
    case RSyncStorage:
    case RCloneStorage:
    {
        progress->updateProgress();
        Path *mount = local_fs_->mkTempDir("beak_send_");
        unique_ptr<FuseMount> fuse_mount = sys_->mount(mount, backupp->asFuseAPI(), settings->fusedebug);

        if (!fuse_mount) {
            error(STORAGETOOL, "Could not mount beak filesystem for rclone/rsync.\n");
        }

        RC rc = RC::OK;
        if (storage->type == RCloneStorage) {
            rc = rcloneSendFiles(storage,
                                 &files_to_backup,
                                 mount,
                                 local_fs_,
                                 sys_, progress);
        } else {
            rc = rsyncSendFiles(storage,
                                &files_to_backup,
                                mount,
                                local_fs_,
                                sys_, progress);
        }

        if (rc.isErr()) {
            error(STORAGETOOL, "Error when invoking rclone/rsync.\n");
        }

        // Unmount virtual filesystem.
        rc = sys_->umount(fuse_mount);
        if (rc.isErr()) {
            error(STORAGETOOL, "Could not unmount beak filesystem \"%s\".\n", mount->c_str());
        }
        rc = local_fs_->rmDir(mount);

        break;
    }
    case NoSuchStorage:
        assert(0);
    }

    progress->finishProgress();

    return RC::OK;
}

RC StorageToolImplementation::copyBackupIntoStorage(Backup  *backupp,
                                                    Path *backup_dir,
                                                    FileSystem *backup_fs,
                                                    Storage *storage,
                                                    Settings *settings,
                                                    ProgressStatistics *progress)
{
    // Store the archive files here.
    FileSystem *storage_fs = NULL;
    // This is the list of files to be sent to the storage.
    vector<Path*> files_to_backup;

    map<Path*,FileStat> contents;
    unique_ptr<FileSystem> fs;
    if (storage->type == FileSystemStorage)
    {
        storage_fs = local_fs_;
    }
    else
    if (storage->type == RCloneStorage || storage->type == RSyncStorage)
    {
        vector<TarFileName> files, bad_files;
        vector<string> other_files;
        RC rc = RC::OK;
        if (storage->type == RCloneStorage)
        {
            rc = rcloneListBeakFiles(storage, &files, &bad_files, &other_files, &contents, sys_, progress);
        }
        else
        {
            rc = rsyncListBeakFiles(storage, &files, &bad_files, &other_files, &contents, sys_, progress);
        }
        if (rc.isErr())
        {
            error(STORAGETOOL, "Could not list files in rclone storage %s\n", storage->storage_location->c_str());
        }

        // Present the listed files at the storage site as a read only file system
        // that can only be listed and stated. This is enough to determine if
        // the storage site files need to be updated.
        fs = newStatOnlyFileSystem(sys_, contents);
        storage_fs = fs.get();
    }
    backup_fs->recurse(backup_dir, [=,&files_to_backup]
                       (Path *path, FileStat *stat) {
                           Path *pp = path->subpath(backup_dir->depth());
                           add_backup_work(progress, &files_to_backup, pp, stat,
                                           storage->storage_location, storage_fs);
                           return RecurseContinue;
                       });

    debug(STORAGETOOL, "work to be done: num_files=%ju num_dirs=%ju\n", progress->stats.num_files, progress->stats.num_dirs);

    switch (storage->type) {
    case FileSystemStorage:
    {
        backup_fs->recurse(backup_dir, [=]
                           (Path *path, FileStat *stat) {
                               Path *pp = path->subpath(backup_dir->depth());
                               copy_local_backup_file(pp,
                                                      backup_dir,
                                                      backup_fs,
                                                      stat,
                                                      storage->storage_location,
                                                      storage_fs,
                                                      progress);
                               return RecurseContinue; });
        break;
    }
    case RSyncStorage:
    case RCloneStorage:
    {
        progress->updateProgress();

        RC rc = RC::OK;
        if (storage->type == RCloneStorage) {
            rc = rcloneSendFiles(storage,
                                 &files_to_backup,
                                 backup_dir,
                                 local_fs_,
                                 sys_, progress);
        } else {
            rc = rsyncSendFiles(storage,
                                &files_to_backup,
                                backup_dir,
                                local_fs_,
                                sys_, progress);
        }

        if (rc.isErr()) {
            error(STORAGETOOL, "Error when invoking rclone/rsync.\n");
        }
        break;
    }
    case NoSuchStorage:
        assert(0);
    }

    progress->finishProgress();

    return RC::OK;
}


RC StorageToolImplementation::listPointsInTime(Storage *storage, vector<pair<Path*,struct timespec>> *v,
                                               ProgressStatistics *progress)
{
    switch (storage->type) {
    case FileSystemStorage:
    {
        break;
    }
    case RSyncStorage:
    {
        break;
    }
    case RCloneStorage:
    {
        break;
    }
    case NoSuchStorage:
        assert(0);
    }

    return RC::OK;
}

RC StorageToolImplementation::removeBackupFiles(Storage *storage,
                                                std::vector<Path*>& files_to_remove,
                                                ProgressStatistics *progress)
{
    // This is the list of files to be sent to the storage.

    switch (storage->type) {
    case FileSystemStorage:
    {
        for (auto p : files_to_remove)
        {
            Path *pp = p->prepend(storage->storage_location);
            debug(STORAGETOOL, "removing backup file %s\n", pp->c_str());

            bool ok = local_fs_->deleteFile(pp);
            if (!ok) {
                error(STORAGETOOL, "Could not delete local backup file: %s\n", p->c_str());
            }
        }
        break;
    }
    case RSyncStorage:
    case RCloneStorage:
    {
        progress->updateProgress();
        RC rc = RC::OK;
        if (storage->type == RCloneStorage) {
            rc = rcloneDeleteFiles(storage,
                                   &files_to_remove,
                                   local_fs_,
                                   sys_, progress);
        } else {
            rc = rsyncDeleteFiles(storage,
                                  &files_to_remove,
                                  local_fs_,
                                  sys_, progress);
        }

        if (rc.isErr()) {
            error(STORAGETOOL, "Error when invoking rclone/rsync.\n");
        }

        break;
    }
    case NoSuchStorage:
        assert(0);
    }

    progress->finishProgress();

    return RC::OK;
}

struct CacheFS : ReadOnlyCacheFileSystemBaseImplementation
{
    CacheFS(ptr<FileSystem> cache_fs, Path *cache_dir, Storage *storage, System *sys, Monitor *monitor) :
        ReadOnlyCacheFileSystemBaseImplementation("CacheFS", cache_fs, cache_dir, storage->storage_location->depth(), monitor),
        sys_(sys), storage_(storage) {
    }

    void refreshCache();
    void addDirToParent(std::map<Path*,CacheEntry> *entries, Path *dir);
    RC loadDirectoryStructure(std::map<Path*,CacheEntry> *entries);
    RC fetchFile(Path *file);
    RC fetchFiles(vector<Path*> *files);

protected:

    System *sys_ {};
    Storage *storage_ {};
};

void CacheFS::refreshCache() {
    loadDirectoryStructure(&entries_);
}

// Add the dir to the parent and check that all parents to the parent
// are added as well.
void CacheFS::addDirToParent(std::map<Path*,CacheEntry> *entries, Path *dir)
{
    assert(dir);
    Path *parent = dir->parent();
    if (parent == NULL) return;
    // The dir must have been added to the cache entries.
    assert(entries->count(dir) == 1);
    if (entries->count(parent) == 0) {
        // The parent is not yet added to the cache entries. Add it!
        FileStat dir_stat;
        dir_stat.setAsDirectory();
        (*entries)[parent] = CacheEntry(dir_stat, parent, true);
    }
    CacheEntry *dir_entry = &(*entries)[dir];
    CacheEntry *parent_entry = &(*entries)[parent];
    if (parent_entry->direntries.count(dir) == 0) {
        // Add dir to its parent.
        parent_entry->direntries[dir] = dir_entry;
    }

    addDirToParent(entries, parent);
}

RC CacheFS::loadDirectoryStructure(map<Path*,CacheEntry> *entries)
{
    vector<TarFileName> files;
    vector<TarFileName> bad_files;
    vector<std::string> other_files;
    map<Path*,FileStat> contents;
    RC rc = RC::OK;

    auto progress = monitor_->newProgressStatistics("Loading directory structure...");

    switch (storage_->type) {
    case NoSuchStorage:
    case FileSystemStorage:
        break;
    case RSyncStorage:
        rc = rsyncListBeakFiles(storage_, &files, &bad_files, &other_files, &contents, sys_, progress.get());
        break;
    case RCloneStorage:
        rc = rcloneListBeakFiles(storage_, &files, &bad_files, &other_files, &contents, sys_, progress.get());
        break;
    }

    Path *prev_dir = NULL;
    CacheEntry *prev_dir_cache_entry = NULL;
    FileStat dir_stat;
    dir_stat.setAsDirectory();

    vector<Path*> index_files;

    for (auto &p : contents)
    {
        Path *dir = p.first->parent();
        CacheEntry *dir_entry = NULL;
        if (dir == prev_dir) {
            dir_entry = prev_dir_cache_entry;
        } else {
            if (entries->count(dir) == 0) {
                // Create a new directory cache entry.
                // Directories cache entries are always marked as cached.
                (*entries)[dir] = CacheEntry(dir_stat, dir, true);
                // Add this dir to its parent directory.
                addDirToParent(entries, dir);
            }
            dir_entry = prev_dir_cache_entry = &(*entries)[dir];
        }
        prev_dir = dir;
        // Create a new file cache entry.
        // Initially the cache entry is marked as not cached.
        (*entries)[p.first] = CacheEntry(p.second, p.first, false);
        CacheEntry *ce = &(*entries)[p.first];
        debug(CACHE, "adding %s to cache index\n", p.first->c_str());
        if (TarFileName::isIndexFile(p.first) && !ce->isCached(cache_fs_, cache_dir_, p.first))
        {
            index_files.push_back(p.first);
            debug(CACHE, "needs index %s\n", p.first->c_str());
        }
        // Add this file to its directory.
        dir_entry->direntries[p.first] = ce;
    }

    if (index_files.size() > 0) {
        info(CACHE, "Prefetching %zu index files...", index_files.size());
        rc = fetchFiles(&index_files);
        info(CACHE, "done.\n");
    }
    return rc;
}

RC CacheFS::fetchFile(Path *file)
{
    vector<Path*> files;
    files.push_back(file);
    return fetchFiles(&files);
}

RC CacheFS::fetchFiles(vector<Path*> *files)
{
    auto progress = monitor_->newProgressStatistics("Fetching files...");
    for (auto p : *files) {
        debug(CACHE, "fetch %s\n", p->c_str());
    }
    switch (storage_->type) {
    case NoSuchStorage:
    case FileSystemStorage:
        assert(0);
        break;
    case RSyncStorage:
    {
        debug(CACHE, "fetching %d files from rsync %s\n", files->size(), storage_->storage_location->c_str());
        return rsyncFetchFiles(storage_, files, cache_dir_, sys_, cache_fs_, progress.get());
    }
    case RCloneStorage:
    {
        debug(CACHE, "fetching %d files from rclone %s\n", files->size(), storage_->storage_location->c_str());
        return rcloneFetchFiles(storage_, files, cache_dir_, sys_, cache_fs_, progress.get());
    }
    }
    return RC::ERR;
}

FileSystem *StorageToolImplementation::asCachedReadOnlyFS(Storage *storage, Monitor *monitor)
{
    Path *cache_dir = cacheDir();
    local_fs_->mkDirpWriteable(cache_dir);
    CacheFS *fs = new CacheFS(local_fs_, cache_dir, storage, sys_, monitor);
    fs->refreshCache();
    return fs;
}

FileSystem *StorageToolImplementation::asStatOnlyFS(Storage *storage, Monitor *monitor)
{
    return NULL;
}
