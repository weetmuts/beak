/*
 Copyright (C) 2024 Fredrik Öhrström

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

static ComponentId DELTA = registerLogComponent("delta");

RC BeakImplementation::delta(Settings *settings, Monitor *monitor)
{
    RC rc = RC::OK;
    /*
    // From is usually the remote that has fewer beak files.
    assert(settings->from.type == ArgStorage);
    // To is the local storage with more beak files.
    // We want to write delta files to the local storage that can be sent
    // to the remote instead of the full beak files.
    // The delta files can be used to recreate the new beak files from existing beak files
    // already on the remote.
    assert(settings->to.type == ArgStorage);

    auto progress = monitor->newProgressStatistics(buildJobName("delta", settings));

    FileSystem *remote_fs;
    Path *remote_root;
    auto remote = accessSingleStorageBackup_(&settings->from, "", monitor, &remote_fs, &remote_root);

    FileSystem *local_fs;
    Path *local_root;
    auto local = accessSingleStorageBackup_(&settings->to, "", monitor, &local_fs, &local_root);

    set<Path*> remote_beak_files;
    vector<pair<Path*,FileStat>> existing_remote_beak_files;
    set<Path*> set_of_existing_beak_files;
    size_t total_files_size = 0;

    for (auto& i : remote->historyOldToNew())
    {
        Path *p = Path::lookup(i.filename);
        remote_beak_files.insert(p);
        for (auto& t : *(i.tarfiles()))
        {
            required_remote_beak_files.insert(t);
        }
    }

    vector<Path*> superfluous_files;
    size_t superfluous_files_size = 0;
    //vector<Path*> lost_files;
    //size_t lost_files_size = 0;
    vector<Path*> broken_points_in_time;

    backup_fs->listFilesBelow(root, &existing_beak_files, SortOrder::Unspecified);
    for (auto& p : existing_beak_files)
    {
        debug(DELTA, "existing: %s\n", p.first->c_str());
        set_of_existing_beak_files.insert(p.first);
        total_files_size += p.second.st_size;
        if (required_beak_files.count(p.first) == 0)
        {
            verbose(DELTA, "superfluous: %s\n", p.first->c_str());
            superfluous_files.push_back(p.first);
            superfluous_files_size += p.second.st_size;
        }
    }

    bool lost_file = false;
    for (auto p : required_beak_files)
    {
        if (set_of_existing_beak_files.count(p) == 0)
        {
            verbose(DELTA, "lost: %s\n", p->c_str());
            lost_file = true;
            //lost_files.push_back(p);
            //lost_files_size += p.second.st_size;
        }
    }

    if (lost_file) {
        // Ouch, a backup file was lost. Are there any ok points in time?
        for (auto& i : restore->historyOldToNew())
        {
            Path *p = Path::lookup(i.filename);
            bool missing = 0 == set_of_existing_beak_files.count(p);
            if (!missing)
            {
                for (auto& t : *(i.tarfiles()))
                {
                    if (set_of_existing_beak_files.count(t) == 0) {
                        missing = true;
                        break;
                    }
                }
            }
            if (missing) {
                info(DELTA, "Broken %s\n", i.datetime.c_str());
                broken_points_in_time.push_back(Path::lookup(i.filename));
            } else {
                info(DELTA, "OK     %s\n", i.datetime.c_str());
            }
        }
    } else {
        string last_size = humanReadableTwoDecimals(restore->historyOldToNew().back().size);
        string kept_size = humanReadableTwoDecimals(total_files_size);
        UI::output("OK! Last backup %s, all backups %s (%d points in time).\n",
                   last_size.c_str(),
                   kept_size.c_str(),
                   restore->historyOldToNew().size());
    }

    int sn = superfluous_files.size();
    if (sn > 0) {
        string ss = humanReadableTwoDecimals(superfluous_files_size);
        UI::output("Found %d superfluous file(s) with a total size of %s \n", sn, ss.c_str());
        auto proceed = UINo;
        if (UI::isatty()) {
            proceed = UI::yesOrNo("Delete?");
        }

        if (proceed == UIYes)
        {
            storage_tool_->removeBackupFiles(settings->from.storage,
                                             superfluous_files,
                                             progress.get());
            UI::output("Superflous files are now deleted.\n");
        }
    }
    int bn = broken_points_in_time.size();
    if (bn > 0) {
        //string ss = humanReadableTwoDecimals(superfluous_files_size);
        UI::output("Found %d broken points in time\n", bn);
        auto proceed = UINo;
        if (UI::isatty()) {
            proceed = UI::yesOrNo("Delete?");
        }

        if (proceed == UIYes)
        {
            storage_tool_->removeBackupFiles(settings->from.storage,
                                             broken_points_in_time,
                                             progress.get());
            UI::output("Broken points in time are now deleted. Run fsck again.\n");
        }
    }
    */
    return rc;
}
