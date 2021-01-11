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
#include "prune.h"
#include "storagetool.h"

static ComponentId PRUNE = registerLogComponent("prune");

RC BeakImplementation::prune(Settings *settings, Monitor *monitor)
{
    RC rc = RC::OK;

    assert(settings->from.type == ArgStorage);

    auto progress = monitor->newProgressStatistics(buildJobName("prune", settings));
    FileSystem *backup_fs;
    Path *root;
    auto restore = accessBackup_(&settings->from, "", monitor, &backup_fs, &root);
    Keep keep("all:2d daily:2w weekly:2m monthly:2y");
    if (settings->keep_supplied) {
        bool ok = keep.parse(settings->keep);
        if (!ok) {
            error(PRUNE, "Not a valid keep rule: \"%s\"\n", settings->keep.c_str());
        }
    }

    uint64_t now_nanos = clockGetUnixTimeNanoSeconds();
    if (settings->now_supplied) {
        time_t nowt;
        RC rc = parseDateTime(settings->now, &nowt);
        if (rc.isErr())
        {
            usageError(PRUNE, "Cannot parse date time \"%s\"\n", settings->now.c_str());
            assert(0);
        }
        now_nanos = ((uint64_t)nowt)*1000000000ull;
    }

    auto prune = newPrune(now_nanos, keep);

    set<Path*> required_beak_files;
    int num_existing_points_in_time = 0;

    // Iterate over the points in time, from the oldest to the newest!
    for (auto &i : restore->historyOldToNew())
    {
        if (i.point() > now_nanos) {
            verbose(PRUNE, "Found point in time \"%s\" which is in the future.\n", i.datetime.c_str());
            usageError(PRUNE, "Cowardly refusing to prune a storage with point in times from the future!\n");
            assert(0);
        }
        prune->addPointInTime(i.point());
        num_existing_points_in_time++;
    }

    map<uint64_t,bool> keeps;

    // Perform the prune calculation
    prune->prune(&keeps);

    int num_kept_points_in_time = 0;

    for (PointInTime& i : restore->historyOldToNew())
    {
        if (keeps[i.point()]) {
            // We should keep this point in time, lets remember all the tars required.
            num_kept_points_in_time++;
            for (auto& t : *(i.tarfiles())) {
                required_beak_files.insert(t);
            }
            Path *p = Path::lookup(i.filename);
            required_beak_files.insert(p);
        }
    }

    vector<pair<Path*,FileStat>> existing_beak_files;
    backup_fs->listFilesBelow(root, &existing_beak_files, SortOrder::Unspecified);

    set<Path*> set_of_existing_beak_files;
    for (auto& p : existing_beak_files)
    {
        set_of_existing_beak_files.insert(p.first);
    }

    vector<Path*> beak_files_to_delete;
    size_t total_size_removed = 0;
    size_t total_size_kept = 0;

    int num_lost = 0;
    // Check that all expected tars actually exist in the storage location.
    for (auto p : required_beak_files)
    {
        if (set_of_existing_beak_files.count(p) == 0)
        {
            warning(PRUNE, "storage lost: %s\n", p->c_str());
            num_lost++;
        }
    }

    for (auto &p : existing_beak_files)
    {
        // Should we delete this file, check if the file is found in required_beak_files...
        if (required_beak_files.count(p.first) > 0)
        {
            total_size_kept += p.second.st_size;
        }
        else
        {
            // Not found! Ie, it is no longer needed.
            // Lets queue it up for deletion.
            beak_files_to_delete.push_back(p.first);
            total_size_removed += p.second.st_size;

            if (settings->dryrun == true) {
                verbose(PRUNE, "would remove %s\n", p.first->c_str());
            } else {
                debug(PRUNE, "removing %s\n", p.first->c_str());
            }
        }
    }

    string removed_size = humanReadableTwoDecimals(total_size_removed);
    string last_size = humanReadableTwoDecimals(restore->historyOldToNew().back().size);
    string kept_size = humanReadableTwoDecimals(total_size_kept);

    if (total_size_removed == 0)
    {
        UI::output("No pruning needed. Last backup %s, all backups %s (%d points in time).\n",
                   last_size.c_str(),
                   kept_size.c_str(),
                   num_kept_points_in_time);
        return RC::OK;
    }
    else
    {
        UI::output("Prune will delete %s (%d points in time) and keep %s (%d).\n",
                   removed_size.c_str(),
                   num_existing_points_in_time - num_kept_points_in_time,
                   kept_size.c_str(),
                   num_kept_points_in_time);
    }

    if (num_lost > 0)
    {
        usageError(PRUNE, "Warning! Lost %d backup files! First run fsck.\n", num_lost);
        assert(0);
    }

    if (settings->dryrun == false)
    {
        auto proceed = settings->yesprune ? UIYes : UINo;

        if (UI::isatty()) {
            proceed = UI::yesOrNo("Proceed?");
        }

        progress->startDisplayOfProgress();
        if (proceed == UIYes)
        {
            storage_tool_->removeBackupFiles(settings->from.storage,
                                             beak_files_to_delete,
                                             progress.get());
            UI::output("Backup is now pruned.\n");
        }
    }

    return rc;
}
