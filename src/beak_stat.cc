/*
 Copyright (C) 2016-2021 Fredrik Öhrström

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
#include "diff.h"
#include "log.h"
#include "origintool.h"
#include "storagetool.h"

static ComponentId STATS = registerLogComponent("stats");

RC BeakImplementation::stat(Settings *settings, Monitor *monitor)
{
    RC rc = RC::OK;

    assert(settings->from.type == ArgOrigin || settings->from.type == ArgRule || settings->from.type == ArgStorage);

    auto progress = monitor->newProgressStatistics(buildJobName("stats", settings), "stat");

    FileSystem *curr_fs = NULL;
    FileSystem *old_fs = NULL;
    Path *curr_path =NULL;
    Path *old_path = NULL;

    unique_ptr<Restore> restore_curr;

    // Setup the curr file system.
    if (settings->from.type == ArgOrigin)
    {
        curr_fs = origin_tool_->fs();
        curr_path = settings->from.origin;
    }
    else if (settings->from.type == ArgStorage)
    {
        restore_curr = accessSingleStorageBackup_(&settings->from, settings->from.point_in_time, monitor);
        auto point = restore_curr->singlePointInTime();
        if (!point) {
            // The settings did not specify a point in time, lets use the most recent for the restore.
            point = restore_curr->setPointInTime("@0");
        }

        if (!restore_curr) {
            return RC::ERR;
        }
        curr_fs = restore_curr->asFileSystem();
        curr_path = NULL;
    }

    unique_ptr<Restore> restore_old;

    // Setup an empty old file system.
    map<Path*,FileStat> contents;
    auto ofs = newStatOnlyFileSystem(sys_, contents);
    old_fs = ofs.get();
    old_path = Path::lookupRoot();

    auto d = newDiff(settings->verbose, settings->depth);
    rc = d->diff(old_fs, old_path,
                 curr_fs, curr_path,
                 progress.get());
    d->report(true);
    return rc;
}
