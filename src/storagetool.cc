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

#include "storagetool.h"

#include "backup.h"
#include "filesystem_helpers.h"
#include "log.h"
#include "system.h"
#include "storage_rclone.h"
#include "storage_rsync.h"

#include <algorithm>

static ComponentId STORAGETOOL = registerLogComponent("storagetool");
static ComponentId CACHE = registerLogComponent("cache");

using namespace std;

struct StorageToolImplementation : public StorageTool
{
    StorageToolImplementation(ptr<System> sys, ptr<FileSystem> local_fs);

    RC storeBackupIntoStorage(Backup *backup,
                              Storage *storage,
                              StoreStatistics *st,
                              Settings *settings);

    RC listPointsInTime(Storage *storage, vector<pair<Path*,struct timespec>> *v);

    FileSystem *asCachedReadOnlyFS(Storage *storage);

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

void add_backup_work(ptr<StoreStatistics> st,
                     vector<Path*> *files_to_backup,
                     Path *path, FileStat *stat,
                     Settings *settings,
                     FileSystem *to_fs)
{
    Path *file_to_extract = path->prepend(settings->to.storage->storage_location);

    // The backup fs has already wrapped any non-regular files inside tars.
    // Thus we only want to send those to the cloud storage site.
    if (stat->isRegularFile()) {

        // Remember the size of this file. This is necessary to
        // know how many bytes has been transferred when
        // rclone/rsync later reports that a file has been successfully sent.
        assert(st->stats.file_sizes.count(file_to_extract) == 0);
        st->stats.file_sizes[file_to_extract] = stat->st_size;

        // Compare our local file with the stats of the one stored remotely.
        stat->checkStat(to_fs, file_to_extract);

        if (stat->disk_update == Store) {
            // Yep, we need to store the local file remotely.
            // The remote might be missing, or it has the wrong size/date.

            // Accumulate the count/size of files to be uploaded.
            st->stats.num_files_to_store++;
            st->stats.size_files_to_store += stat->st_size;
            // Remember the files to be uploaded.
            files_to_backup->push_back(path);
        }
        // Accumulate the total count of files.
        st->stats.num_files++;
        st->stats.size_files+=stat->st_size;
    }
    else if (stat->isDirectory())
    {
        // We count the directories, only for information.
        // Directories are created implicitly on the storage side anyway.
        st->stats.num_dirs++;
    }
}

void store_local_backup_file(Backup *backup,
                             FileSystem *backup_fs,
                             FileSystem *origin_fs,
                             FileSystem *storage_fs,
                             Path *path,
                             FileStat *stat,
                             Settings *settings,
                             ptr<StoreStatistics> st)
{
    if (!stat->isRegularFile()) return;

    TarFile *tar = backup->findTarFromPath(path);
    assert(tar);

    debug(STORAGETOOL, "PATH %s\n", path->c_str());
    Path *file_name = path->prepend(settings->to.storage->storage_location);
    storage_fs->mkDirpWriteable(file_name->parent());
    FileStat old_stat;
    RC rc = storage_fs->stat(file_name, &old_stat);
    if (rc.isOk() &&
        stat->samePermissions(&old_stat) &&
        stat->sameSize(&old_stat) &&
        stat->sameMTime(&old_stat)) {

        debug(STORAGETOOL, "Skipping %s\n", file_name->c_str());
    } else {
        if (rc.isOk()) {
            storage_fs->deleteFile(file_name);
        }
        // The size gets incrementally update while the tar file is written!
        auto func = [&st](size_t n){ st->stats.size_files_stored += n; };
        tar->createFile(file_name, stat, origin_fs, storage_fs, 0, func);

        storage_fs->utime(file_name, stat);
        st->stats.num_files_stored++;
        verbose(STORAGETOOL, "Stored %s\n", file_name->c_str());
    }
//    st->num_files_handled++;
//    st->size_files_handled += stat->st_size;
    st->updateProgress();
}

RC StorageToolImplementation::storeBackupIntoStorage(Backup  *backup,
                                                     Storage *storage,
                                                     StoreStatistics *st,
                                                     Settings *settings)
{
    st->startDisplayOfProgress();

    // The backup archive files (.tar .gz) are found here.
    FileSystem *backup_fs = backup->asFileSystem();
    // The where the origin files can be found.
    FileSystem *origin_fs = backup->originFileSystem();
    // Store the archive files here.
    FileSystem *storage_fs = NULL;
    // This is the list of files to be sent to the storage.
    vector<Path*> files_to_backup;

    map<Path*,FileStat> contents;
    unique_ptr<FileSystem> fs;
    if (storage->type == FileSystemStorage) {
        storage_fs = local_fs_;
    } else
    if (storage->type == RCloneStorage || storage->type == RSyncStorage) {
        vector<TarFileName> files, bad_files;
        vector<string> other_files;
        RC rc = RC::OK;
        if (storage->type == RCloneStorage) {
            rc = rcloneListBeakFiles(storage, &files, &bad_files, &other_files, &contents, sys_);
        } else {
            rc = rsyncListBeakFiles(storage, &files, &bad_files, &other_files, &contents, sys_);
        }
        if (rc.isErr()) {
            error(STORAGETOOL, "Could not list files in rclone storage %s\n", storage->storage_location->c_str());
        }

        // Present the listed files at the storage site as a read only file system
        // that can only be listed and stated. This is enough to determine if
        // the storage site files need to be updated.
        fs = newStatOnlyFileSystem(contents);
        storage_fs = fs.get();
    }
    backup_fs->recurse(Path::lookupRoot(), [=,&files_to_backup]
                       (Path *path, FileStat *stat) {
                           add_backup_work(st, &files_to_backup, path, stat, settings, storage_fs);
                       });

    debug(STORAGETOOL, "Work to be done: num_files=%ju num_dirs=%ju\n", st->stats.num_files, st->stats.num_dirs);

    switch (storage->type) {
    case FileSystemStorage:
    {
        backup_fs->recurse(Path::lookupRoot(), [=]
                           (Path *path, FileStat *stat) {store_local_backup_file(backup,
                                                                                 backup_fs,
                                                                                 origin_fs,
                                                                                 storage_fs,
                                                                                 path,
                                                                                 stat,
                                                                                 settings,
                                                                                 st); });
        break;
    }
    case RSyncStorage:
    case RCloneStorage:
    {
        Path *mount = local_fs_->mkTempDir("beak_send_");
        unique_ptr<FuseMount> fuse_mount = sys_->mount(mount, backup->asFuseAPI(), settings->fusedebug);

        if (!fuse_mount) {
            error(STORAGETOOL, "Could not mount beak filesystem for rclone/rsync.\n");
        }

        RC rc = RC::OK;
        if (storage->type == RCloneStorage) {
            rc = rcloneSendFiles(storage,
                                 &files_to_backup,
                                 mount,
                                 st,
                                 local_fs_,
                                 sys_);
        } else {
            rc = rsyncSendFiles(storage,
                                &files_to_backup,
                                mount,
                                st,
                                local_fs_,
                                sys_);
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

    st->finishProgress();

    return RC::OK;
}


RC StorageToolImplementation::listPointsInTime(Storage *storage, vector<pair<Path*,struct timespec>> *v)
{
    switch (storage->type) {
    case FileSystemStorage:
    {
        break;
    }
    case RSyncStorage:
    {
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

struct CacheFS : ReadOnlyCacheFileSystemBaseImplementation
{
    CacheFS(ptr<FileSystem> cache_fs, Path *cache_dir, Storage *storage, System *sys) :
        ReadOnlyCacheFileSystemBaseImplementation("CacheFS", cache_fs, cache_dir, storage->storage_location->depth()),
        sys_(sys), storage_(storage) {



    }

    void refreshCache();
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

RC CacheFS::loadDirectoryStructure(map<Path*,CacheEntry> *entries)
{
    vector<TarFileName> files;
    vector<TarFileName> bad_files;
    vector<std::string> other_files;
    map<Path*,FileStat> contents;

    switch (storage_->type) {
    case NoSuchStorage:
    case FileSystemStorage:
    {
        break;
    }
    case RSyncStorage:
    case RCloneStorage:
        RC rc = RC::OK;
        if (storage_->type == RCloneStorage) {
            rc = rcloneListBeakFiles(storage_, &files, &bad_files, &other_files, &contents, sys_);
        } else {
            rc = rsyncListBeakFiles(storage_, &files, &bad_files, &other_files, &contents, sys_);
        }
        if (rc.isErr()) return RC::ERR;
    }

    vector<Path*> index_files;
    for (auto &p : files) {
        if (p.isIndexFile()) {
            index_files.push_back(p.path);
//            fprintf(stderr, "Found index %s\n", p.path->c_str());
        }
    }

    Path *prev_dir = NULL;
    CacheEntry *prev_dir_cache_entry = NULL;
    FileStat dir_stat;
    dir_stat.setAsDirectory();

    for (auto &p : contents) {
        Path *dir = p.first->parent();
        CacheEntry *dir_entry = NULL;
        if (dir == prev_dir) {
            dir_entry = prev_dir_cache_entry;
        } else {
            if (entries->count(dir) == 0) {
                // Create a new directory cache entry.
                // Directories cache entries are always marked as cached.
                (*entries)[dir] = CacheEntry(dir_stat, dir, true);
            }
            dir_entry = prev_dir_cache_entry = &(*entries)[dir];
        }
        // Create a new file cache entry.
        // Initially the cache entry is marked as not cached.
        (*entries)[p.first] = CacheEntry(p.second, p.first, false);
        CacheEntry *ce = &(*entries)[p.first];
        // Add this file to its directory.
        dir_entry->direntries.push_back(ce);
    }

    return RC::ERR;
}

RC CacheFS::fetchFile(Path *file)
{
    vector<Path*> files;
    files.push_back(file);
    return fetchFiles(&files);
}

RC CacheFS::fetchFiles(vector<Path*> *files)
{
    switch (storage_->type) {
    case NoSuchStorage:
    case FileSystemStorage:
    case RSyncStorage:
    {
        debug(CACHE,"Fetching %d files from %s.\n", files->size(), storage_->storage_location->c_str());
        return rsyncFetchFiles(storage_, files, cache_dir_, sys_, cache_fs_);
    }
    case RCloneStorage:
    {
        debug(CACHE,"Fetching %d files from %s.\n", files->size(), storage_->storage_location->c_str());
        return rcloneFetchFiles(storage_, files, cache_dir_, sys_, cache_fs_);
    }
    }
    return RC::ERR;
}

FileSystem *StorageToolImplementation::asCachedReadOnlyFS(Storage *storage)
{
    Path *cache_dir = cacheDir();
    local_fs_->mkDirpWriteable(cache_dir);
    CacheFS *fs = new CacheFS(local_fs_, cache_dir, storage, sys_);
    fs->refreshCache();
    return fs;
}
