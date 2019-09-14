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
#include "diff.h"
#include "log.h"
#include "origintool.h"
#include "storagetool.h"

static ComponentId DIFF = registerLogComponent("diff");

RC BeakImplementation::diff(Settings *settings, Monitor *monitor)
{
    RC rc = RC::OK;

    assert(settings->from.type == ArgOrigin || settings->from.type == ArgRule || settings->from.type == ArgStorage);
    assert(settings->to.type == ArgOrigin || settings->to.type == ArgRule || settings->to.type == ArgStorage);

    auto progress = monitor->newProgressStatistics(buildJobName("diff", settings));

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
        restore_curr = accessBackup_(&settings->from, settings->from.point_in_time, monitor);
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

    // Setup the old file system.
    if (settings->to.type == ArgOrigin)
    {
        old_fs = origin_tool_->fs();
        old_path = settings->to.origin;
    }
    else if (settings->to.type == ArgStorage)
    {
        restore_old = accessBackup_(&settings->to, settings->to.point_in_time, monitor);
        auto point = restore_old->singlePointInTime();
        if (!point) {
            // The settings did not specify a point in time, lets use the most recent for the restore.
            point = restore_old->setPointInTime("@0");
        }

        if (!restore_old) {
            return RC::ERR;
        }
        old_fs = restore_old->asFileSystem();
        old_path = NULL;
    }

    auto d = newDiff(settings->verbose, settings->depth);
    rc = d->diff(old_fs, old_path,
                 curr_fs, curr_path,
                 progress.get());
    d->report();
    return rc;
}
