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

static ComponentId RESTORE = registerLogComponent("restore");

RC BeakImplementation::restore(Settings *settings, Monitor *monitor)
{
    uint64_t start = clockGetTimeMicroSeconds();
    auto progress = monitor->newProgressStatistics(buildJobName("restore", settings));
    progress->startDisplayOfProgress();

    umask(0);
    RC rc = RC::OK;

    auto restore  = accessBackup_(&settings->from, settings->to.point_in_time, monitor);

    if (!restore) {
        return RC::ERR;
    }

    auto point = restore->singlePointInTime();
    if (!point) {
        // The settings did not specify a point in time, lets use the most recent for the restore.
        point = restore->setPointInTime("@0");
    }

    FileSystem *backup_fs = restore->backupFileSystem(); // Access the archive files storing content.
    FileSystem *backup_contents_fs = restore->asFileSystem(); // Access the files inside archive files.

    backup_contents_fs->recurse(Path::lookupRoot(),
                                [&restore,this,point,settings,&progress]
                                (Path *path, FileStat *stat) {
                                    origin_tool_->addRestoreWork(progress.get(),
                                                                 path,
                                                                 stat,
                                                                 settings,
                                                                 restore.get(),
                                                                 point);
                                    return RecurseContinue; });

    bool work_to_do = false;
    if (progress->stats.num_files_to_store > 0) {
        string file_sizes = humanReadable(progress->stats.size_files_to_store);
        info(RESTORE, "Restore %ju files for a total size of %s.\n", progress->stats.num_files_to_store, file_sizes.c_str());
        work_to_do = true;
    }
    if (progress->stats.num_symbolic_links_to_store > 0) {
        info(RESTORE, "Restore %ju symlinks.\n", progress->stats.num_symbolic_links_to_store);
        work_to_do = true;
    }
    if (progress->stats.num_hard_links_to_store > 0) {
        info(RESTORE, "Restore %ju hard links.\n", progress->stats.num_hard_links_to_store);
        work_to_do = true;
    }
    if (progress->stats.num_device_nodes_to_store > 0) {
        info(RESTORE, "Restore %ju fifo nodes.\n", progress->stats.num_device_nodes_to_store);
        work_to_do = true;
    }
    if (progress->stats.num_dirs_updated > 0) {
        info(RESTORE, "Update %ju dirs.\n", progress->stats.num_dirs_to_update);
        work_to_do = true;
    }
    if (progress->stats.num_newer_files_to_skip > 0)
    {
        if (settings->forceoverwritefiles)
        {
            info(RESTORE, "Overwriting %d newer files with backup files!\n", progress->stats.num_newer_files_to_skip);
            work_to_do = true;
        }
        else
        {
            info(RESTORE, "NOT restoring %d files with newer timestamps than the backup!\n", progress->stats.num_newer_files_to_skip);
        }
    }

    if (!work_to_do)
    {
        info(RESTORE, "No restores are needed, everything is up to date.\n");
        return RC::OK;
    }

    auto proceed = settings->yesrestore ? UIYes : UINo;

    if (proceed == UINo && UI::isatty())
    {
        proceed = UI::yesOrNo("Proceed?");
    }
    if (proceed == UINo) return RC::ERR;

    origin_tool_->restoreFileSystem(backup_fs, backup_contents_fs, restore.get(), point, settings, progress.get());

    uint64_t stop = clockGetTimeMicroSeconds();
    uint64_t restore_time = stop - start;

    progress->finishProgress();

    if (progress->stats.num_files_stored == 0 && progress->stats.num_symbolic_links_stored == 0 &&
        progress->stats.num_device_nodes_stored == 0 && progress->stats.num_dirs_updated == 0) {
        info(RESTORE, "No restores were needed, everything was up to date.\n");
    } else {
        if (progress->stats.num_files_stored > 0) {
            string file_sizes = humanReadable(progress->stats.size_files_stored);
            info(RESTORE, "Restored %ju files for a total size of %s.\n", progress->stats.num_files_stored, file_sizes.c_str());
        }
        if (progress->stats.num_symbolic_links_stored > 0) {
            info(RESTORE, "Restored %ju symlinks.\n", progress->stats.num_symbolic_links_stored);
        }
        if (progress->stats.num_hard_links_stored > 0) {
            info(RESTORE, "Restored %ju hard links.\n", progress->stats.num_hard_links_stored);
        }
        if (progress->stats.num_device_nodes_stored > 0) {
            info(RESTORE, "Restored %ju fifo nodes.\n", progress->stats.num_device_nodes_stored);
        }
        if (progress->stats.num_dirs_updated > 0) {
            info(RESTORE, "Updated %ju dirs.\n", progress->stats.num_dirs_updated);
        }
        info(RESTORE, "Time to restore %jdms.\n", restore_time / 1000);
    }
    return rc;
}
