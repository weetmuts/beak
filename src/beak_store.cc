/*
 Copyright (C) 2016-2019 Fredrik Öhrström

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

#include "beak.h"
#include "beak_implementation.h"
#include "backup.h"
#include "log.h"
#include "origintool.h"
#include "storagetool.h"

static ComponentId STORE = registerLogComponent("store");

RC BeakImplementation::store(Settings *settings, Monitor *monitor)
{
    RC rc = RC::OK;

    assert(settings->from.type == ArgOrigin || settings->from.type == ArgRule);
    assert(settings->to.type == ArgStorage);

    FileSystem *storage_fs = local_fs_;
    Storage *storage = settings->to.storage;
    if (storage->type == RCloneStorage ||
        storage->type == RSyncStorage) {
        storage_fs = storage_tool_->asCachedReadOnlyFS(storage, monitor);
    }

    storage_fs->recurse(Path::lookupRoot(),
                        [](Path *path, FileStat *stat) { return RecurseContinue; });

    unique_ptr<ProgressStatistics> progress = monitor->newProgressStatistics(buildJobName("store", settings), "store");
    progress->startDisplayOfProgress();

    unique_ptr<Backup> backup  = newBackup(origin_tool_->fs());

    // This command scans the origin file system and builds
    // an in memory representation of the backup file system,
    // with tar files,index files and directories.
    rc = backup->scanFileSystem(&settings->from, settings, progress.get());

    // Now store the beak file system into the selected storage.
    storage_tool_->storeBackupIntoStorage(backup->asFileSystem(),
                                          backup->originFileSystem(),
                                          backup.get(),
                                          settings->to.storage,
                                          settings,
                                          progress.get(),
                                          monitor);

    if (progress->stats.num_files_stored == 0 && progress->stats.num_dirs_updated == 0) {
        info(STORE, "No stores needed, everything was up to date.\n");
    }

    uint64_t start = clockGetTimeMicroSeconds();
    int unpleasant_modifications = backup->checkIfFilesHaveChanged();
    uint64_t stop = clockGetTimeMicroSeconds();
    uint64_t scan_time = stop - start;
    if (scan_time > 2000000)
    {
        info(STORE, "Rescanned indexed files. (%jdms)\n", scan_time / 1000);
    }
    if (unpleasant_modifications > 0) {
        warning(STORE, "Warning! Origin directory modified while doing backup!\n");
    }

    return rc;
}
