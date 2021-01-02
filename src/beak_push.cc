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

#include "beak.h"
#include "beak_implementation.h"
#include "backup.h"
#include "log.h"
#include "origintool.h"
#include "storagetool.h"

static ComponentId PUSH = registerLogComponent("push");

RC BeakImplementation::push(Settings *settings, Monitor *monitor)
{
    RC rc = RC::OK;

    assert(settings->from.type == ArgRule);

    Rule *rule = configuration_->rule(settings->from.rule->name);

    assert(rule != NULL);

    switch (rule->type)
    {
    case RuleType::RemoteMount:

        usageError(PUSH, "The rule \"%s\" can only be used to mount backups.\n", rule->name.c_str());
        assert(0);
        break;

    case RuleType::LocalThenRemoteBackup:

        rc = storeRuleLocallyThenRemotely(rule, settings, monitor);
        break;

    case RuleType::RemoteBackup:

        rc = storeRuleRemotely(rule, settings, monitor);
        break;
    }

    return rc;
}

RC BeakImplementation::storeRuleLocallyThenRemotely(Rule *rule, Settings *settings, Monitor *monitor)
{
    RC rc = RC::OK;

    info(PUSH, "Storing origin into %s\n", rule->local.storage_location->c_str());
    bool ok = local_fs_->mkDirpWriteable(rule->local.storage_location);
    if (!ok)
    {
        warning(PUSH, "Could not write to local storage directory %s\n", rule->local.storage_location->c_str());
        return RC::ERR;
    }

    unique_ptr<ProgressStatistics> progress = monitor->newProgressStatistics(buildJobName("store", settings));

    unique_ptr<Backup> backup  = newBackup(origin_tool_->fs());

    // This command scans the origin file system and builds
    // an in memory representation of the backup file system,
    // with tar files,index files and directories.
    progress->startDisplayOfProgress();
    rc = backup->scanFileSystem(&settings->from, settings, progress.get());

    settings->to.storage = &rule->local;
    // Now store the beak file system into the selected storage.
    storage_tool_->storeBackupIntoStorage(backup.get(),
                                          &rule->local,
                                          settings,
                                          progress.get(),
                                          monitor);

    if (progress->stats.num_files_stored == 0 && progress->stats.num_dirs_updated == 0) {
        info(PUSH, "No stores needed, local backup is up to date.\n");
    }

    uint64_t start = clockGetTimeMicroSeconds();
    int unpleasant_modifications = backup->checkIfFilesHaveChanged();
    uint64_t stop = clockGetTimeMicroSeconds();
    uint64_t scan_time = stop - start;
    if (scan_time > 2000000)
    {
        info(PUSH, "Rescanned indexed files. (%jdms)\n", scan_time / 1000);
    }
    if (unpleasant_modifications > 0) {
        warning(PUSH, "Warning! Origin directory modified while doing local backup!\n");
    }

    info(PUSH, "Local backup copy is now complete. It is now safe to work in your origin directory.\n");

    for (auto & p : rule->storages)
    {
        unique_ptr<ProgressStatistics> progress = monitor->newProgressStatistics(buildJobName("copy", settings));
        progress->startDisplayOfProgress();
        info(PUSH, "Copying local backup into %s\n", p.second.storage_location->c_str());
        storage_tool_->copyBackupIntoStorage(backup.get(),
                                             rule->local.storage_location, // copy from here
                                             local_fs_,
                                             &p.second, // copy to here
                                             settings,
                                             progress.get());

        if (progress->stats.num_files_stored == 0 && progress->stats.num_dirs_updated == 0) {
            info(PUSH, "No copying needed, remote backup is up to date.\n");
        }
    }

    return rc;
}

RC BeakImplementation::storeRuleRemotely(Rule *rule, Settings *settings, Monitor *monitor)
{
    RC rc = RC::OK;

    unique_ptr<ProgressStatistics> progress = monitor->newProgressStatistics(buildJobName("store", settings));

    unique_ptr<Backup> backup  = newBackup(origin_tool_->fs());

    // This command scans the origin file system and builds
    // an in memory representation of the backup file system,
    // with tar files,index files and directories.
    progress->startDisplayOfProgress();
    rc = backup->scanFileSystem(&settings->from, settings, progress.get());

    for (auto & p : rule->storages)
    {
        info(PUSH, "Pushing to: %s\n", p.second.storage_location->c_str());

        settings->to.storage = &p.second;
        // Now store the beak file system into the selected storage.
        storage_tool_->storeBackupIntoStorage(backup.get(),
                                          settings->to.storage,
                                          settings,
                                              progress.get(),
            monitor);

        if (progress->stats.num_files_stored == 0 && progress->stats.num_dirs_updated == 0) {
            info(PUSH, "No stores needed, everything was up to date.\n");
        }

        uint64_t start = clockGetTimeMicroSeconds();
        int unpleasant_modifications = backup->checkIfFilesHaveChanged();
        uint64_t stop = clockGetTimeMicroSeconds();
        uint64_t scan_time = stop - start;
        if (scan_time > 2000000)
        {
            info(PUSH, "Rescanned indexed files. (%jdms)\n", scan_time / 1000);
        }
        if (unpleasant_modifications > 0) {
            warning(PUSH, "Warning! Origin directory modified while doing backup!\n");
        }
    }
    return RC::OK;
}
