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
#include "version.h"

static ComponentId STORE = registerLogComponent("store");

RC BeakImplementation::store(Settings *settings)
{
    RC rc = RC::OK;

    assert(settings->from.type == ArgOrigin || settings->from.type == ArgRule);
    assert(settings->to.type == ArgStorage);

    unique_ptr<ProgressStatistics> progress = newProgressStatistics(settings->progress);

    // Watch the origin file system to detect if it is being changed while doing the store.
    origin_tool_->fs()->enableWatch();

    unique_ptr<Backup> backup  = newBackup(origin_tool_->fs());

    // This command scans the origin file system and builds
    // an in memory representation of the backup file system,
    // with tar files,index files and directories.
    rc = backup->scanFileSystem(&settings->from, settings);

    // Now store the beak file system into the selected storage.
    storage_tool_->storeBackupIntoStorage(backup.get(),
                                          settings->to.storage,
                                          settings,
                                          progress.get());

    int unpleasant_modifications = origin_tool_->fs()->endWatch();
    if (progress->stats.num_files_stored == 0 && progress->stats.num_dirs_updated == 0) {
        info(STORE, "No stores needed, everything was up to date.\n");
    }
    if (unpleasant_modifications > 0) {
        warning(STORE, "Warning! Origin directory modified while doing backup!\n");
    }

    return rc;
}
