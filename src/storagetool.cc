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

#include "forward.h"
#include "log.h"
#include "system.h"

static ComponentId STORAGETOOL = registerLogComponent("storagetool");

using namespace std;

struct StorageToolImplementation : public StorageTool
{
    StorageToolImplementation(ptr<System> sys, ptr<FileSystem> sys_fs, ptr<FileSystem> storage_fs);

    RC storeFileSystemIntoStorage(FileSystem *beaked_fs,
                                  FileSystem *origin_fs,
                                  Storage *storage,
                                  ptr<StoreStatistics> st,
                                  ptr<ForwardTarredFS> ffs,
                                  Options *settings);

    RC listBeakFiles(Storage *storage,
                     std::vector<TarFileName> *files,
                     std::vector<TarFileName> *bad_files,
                     std::vector<std::string> *other_files);
    RC sendBeakFilesToStorage(Path *dir, Storage *storage, std::vector<TarFileName*> *files);
    RC fetchBeakFilesFromStorage(Storage *storage, std::vector<TarFileName*> *files, Path *dir);

    ptr<FileSystem> fs() { return storage_fs_; }

    ptr<System> sys_;
    ptr<FileSystem> sys_fs_;
    ptr<FileSystem> storage_fs_;
};

unique_ptr<StorageTool> newStorageTool(ptr<System> sys,
                                       ptr<FileSystem> sys_fs,
                                       ptr<FileSystem> storage_fs)
{
    return unique_ptr<StorageTool>(new StorageToolImplementation(sys, sys_fs, storage_fs));
}


StorageToolImplementation::StorageToolImplementation(ptr<System>sys,
                                                     ptr<FileSystem> sys_fs,
                                                     ptr<FileSystem> storage_fs)
    : sys_(sys), sys_fs_(sys_fs), storage_fs_(storage_fs)
{

}

void add_forward_work(ptr<StoreStatistics> st,
                      Path *path, FileStat *stat,
                      Options *settings,
                      ptr<FileSystem> to_fs)
{
    Path *file_to_extract = path->prepend(settings->to.storage->storage_location);

    if (stat->isRegularFile()) {
        stat->checkStat(to_fs.get(), file_to_extract);
        if (stat->disk_update == Store) {
            st->stats.num_files_to_store++;
            st->stats.size_files_to_store += stat->st_size;
        }
        st->stats.num_files++;
        st->stats.size_files+=stat->st_size;
    }
    else if (stat->isDirectory()) st->stats.num_dirs++;
}

void handle_tar_file(Path *path,
                     FileStat *stat,
                     ptr<ForwardTarredFS> ffs,
                     Options *settings,
                     ptr<StoreStatistics> st,
                     FileSystem *origin_fs,
                     FileSystem *storage_fs)
{
    if (!stat->isRegularFile()) return;

    debug(STORAGETOOL, "PATH %s\n", path->c_str());
    TarFile *tar = ffs->findTarFromPath(path);
    assert(tar);
    Path *file_name = tar->path()->prepend(settings->to.storage->storage_location);
    storage_fs->mkDirp(file_name->parent());
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
        auto func = [&st](size_t n){st->stats.size_files_stored += n;};
        tar->createFile(file_name, stat, origin_fs, storage_fs, 0, func);

        storage_fs->utime(file_name, stat);
        st->stats.num_files_stored++;
        verbose(STORAGETOOL, "Stored %s\n", file_name->c_str());
    }
//    st->num_files_handled++;
//    st->size_files_handled += stat->st_size;
    st->updateProgress();
}

RC StorageToolImplementation::storeFileSystemIntoStorage(FileSystem *beaked_fs,
                                                         FileSystem *origin_fs,
                                                         Storage *storage,
                                                         ptr<StoreStatistics> st,
                                                         ptr<ForwardTarredFS> ffs,
                                                         Options *settings)
{
    st->startDisplayOfProgress();

    beaked_fs->recurse([=]
                       (Path *path, FileStat *stat) {add_forward_work(st,path,stat,settings, storage_fs_); });

    debug(STORAGETOOL, "Work to be done: num_files=%ju num_dirs=%ju\n", st->stats.num_files, st->stats.num_dirs);

    beaked_fs->recurse([=]
                       (Path *path, FileStat *stat) {handle_tar_file(path,
                                                                     stat,
                                                                     ffs,
                                                                     settings,
                                                                     st,
                                                                     origin_fs,
                                                                     storage_fs_.get()); });
    st->finishProgress();

    return RC::OK;
}


RC StorageToolImplementation::sendBeakFilesToStorage(Path *dir, Storage *storage, vector<TarFileName*> *files)
{
    assert(storage->type == RCloneStorage);

    string files_to_fetch;
    Path *tmp;
    if (files) {
        for (auto& tfn : *files) {
            files_to_fetch.append(tfn->path->str());
            files_to_fetch.append("\n");
        }
    }

    tmp = sys_fs_->mkTempFile("beak_fetching", files_to_fetch);

    RC rc = RC::OK;
    vector<char> out;
    vector<string> args;
    args.push_back("copy");
    if (files) {
        args.push_back("--include-from");
        args.push_back(tmp->c_str());
    }
    args.push_back(dir->c_str());
    args.push_back(storage->storage_location->c_str());
    rc = sys_->invoke("rclone", args, &out);

    return rc;
}

RC StorageToolImplementation::fetchBeakFilesFromStorage(Storage *storage, vector<TarFileName*> *files, Path *dir)
{
    assert(storage->type == RCloneStorage);

    string files_to_fetch;
    for (auto& tfn : *files) {
        files_to_fetch.append(tfn->path->str());
        files_to_fetch.append("\n");
    }

    Path *tmp = sys_fs_->mkTempFile("beak_fetching", files_to_fetch);

    RC rc = RC::OK;
    vector<char> out;
    vector<string> args;
    args.push_back("copy");
    args.push_back("--include-from");
    args.push_back(tmp->c_str());
    args.push_back(storage->storage_location->c_str());
    args.push_back(dir->c_str());
    rc = sys_->invoke("rclone", args, &out);

    return rc;
}

RC StorageToolImplementation::listBeakFiles(Storage *storage,
                                            vector<TarFileName> *files,
                                            vector<TarFileName> *bad_files,
                                            vector<string> *other_files)
{
    assert(storage->type == RCloneStorage);

    RC rc = RC::OK;
    vector<char> out;
    vector<string> args;
    args.push_back("ls");
    args.push_back(storage->storage_location->c_str());
    rc = sys_->invoke("rclone", args, &out);

    if (rc.isErr()) return RC::ERR;
    auto i = out.begin();
    bool eof = false, err = false;

    for (;;) {
	// Example line:
	// 12288 z01_001506595429.268937346_0_7eb62d8e0097d5eaa99f332536236e6ba9dbfeccf0df715ec96363f8ddd495b6_0.gz
        eatWhitespace(out, i, &eof);
        if (eof) break;
        string size = eatTo(out, i, ' ', 64, &eof, &err);
        if (eof || err) break;
        string file_name = eatTo(out, i, '\n', 4096, &eof, &err);
        if (err) break;
        TarFileName tfn;
        bool ok = TarFile::parseFileName(file_name, &tfn);
        // Only files that have proper beakfs names are included.
        if (ok) {
            // Check that the remote size equals the content. If there is a mismatch,
            // then for sure the file must be overwritte/updated. Perhaps there was an earlier
            // transfer interruption....
            if ( (tfn.type != REG_FILE && tfn.size == (size_t)atol(size.c_str())) ||
                 (tfn.type == REG_FILE && tfn.size == 0) )
            {
                files->push_back(tfn);
            }
            else
            {
                bad_files->push_back(tfn);
            }
        } else {
            other_files->push_back(file_name);
        }
    }
    if (err) return RC::ERR;

    return RC::OK;
}
