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
#include "configuration.h"
#include "diff.h"
#include "log.h"
#include "filesystem.h"
#include "restore.h"
#include "index.h"
#include "origintool.h"
#include "prune.h"
#include "statistics.h"
#include "storagetool.h"
#include "system.h"
#include "tarfile.h"
#include "ui.h"
#include "util.h"
#include "version.h"

const char *autocomplete =
#include"generated_autocomplete.h"
    ;

#include <algorithm>
#include <assert.h>
#include <memory.h>
#include <limits.h>
#include <stddef.h>
#include <stdarg.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <map>
#include <string>
#include <utility>
#include <vector>

using namespace std;

static ComponentId COMMANDLINE = registerLogComponent("commandline");
static ComponentId FUSE = registerLogComponent("fuse");
static ComponentId PRUNE = registerLogComponent("prune");
static ComponentId FSCK = registerLogComponent("fsck");

unique_ptr<Beak> newBeak(ptr<Configuration> configuration,
                         ptr<System> sys,
                         ptr<FileSystem> local_fs,
                         ptr<StorageTool> storage_tool,
                         ptr<OriginTool> origin_tool)
{
    return unique_ptr<Beak>(new BeakImplementation(configuration, sys, local_fs, storage_tool, origin_tool));
}

CommandEntry command_entries_bootstrap_[] = {
#define X(name,cmdtype,info,expfrom,expto) { #name, cmdtype, name##_cmd, info, expfrom, expto } ,
LIST_OF_COMMANDS
#undef X
};

vector<CommandEntry> command_entries_;

OptionEntry option_entries_bootstrap_[] = {
#define X(optiontype,shortname,name,type,requiresvalue,info) { optiontype, #shortname, #name, name##_option, requiresvalue, info} ,
LIST_OF_OPTIONS
#undef X
};

vector<OptionEntry> option_entries_;
vector<CommandOption> command_options_;

void CommandOption::add_(int count, ...)
{
    va_list args;
    va_start(args, count);

    while (count>0) {
        int i = va_arg(args, int);
        count--;
        options.push_back((Option)i);
    }

    va_end(args);
}

void buildListOfCommandOptions()
{
#define X(beakcmd, beakoptions) { CommandOption co; co.cmd = beakcmd; co.add_ beakoptions; command_options_.push_back(co); }
LIST_OF_OPTIONS_PER_COMMAND
#undef X
}

const char* lookupCommandName(Command c)
{
    for (auto &e : command_entries_)
    {
        if (e.cmd == c)
        {
            return e.name;
        }
    }
    return NULL;
}

const char* lookupOptionName(Option o)
{
    for (auto &e : option_entries_)
    {
        if (e.option == o)
        {
            return e.name;
        }
    }
    return NULL;
}


bool hasCommandOption(Command cmd, Option option)
{
    for (auto & co : command_options_)
    {
        if (co.cmd == cmd) {
            for (auto o : co.options)
            {
                if (option == o) {
                    return true;
                }
            }
        }
    }
    return false;
}

BeakImplementation::BeakImplementation(ptr<Configuration> configuration,
                                       ptr<System> sys, ptr<FileSystem> local_fs,
                                       ptr<StorageTool> storage_tool,
                                       ptr<OriginTool> origin_tool) :
    configuration_(configuration),
    sys_(sys),
    local_fs_(local_fs),
    storage_tool_(storage_tool),
    origin_tool_(origin_tool)
{
    for (auto &e : command_entries_bootstrap_) {
        command_entries_.push_back(e);
        if (e.cmd != nosuch_cmd) {
            commands_[e.name] = &e;
            commands_from_cmd_[e.cmd] = &e;
        }
    }
    string m = "-";
    for (auto &e : option_entries_bootstrap_) {
        option_entries_.push_back(e);
        if (e.option != nosuch_option) {
            short_options_[m+e.shortname] = &e;
            long_options_[m+m+e.name] = &e;
        } else {
            nosuch_option_ = &e;
        }
    }
    buildListOfCommandOptions();
}

string BeakImplementation::argsToVector_(int argc, char **argv, vector<string> *args)
{
    args->resize(argc);
    // Skip the program name.
    for (int i=1; i<argc; ++i) {
        (*args)[i-1] = argv[i];
    }
    return argv[0];
}

unique_ptr<Restore> BeakImplementation::accessBackup_(Argument *storage,
                                                      string pointintime,
                                                      ProgressStatistics *progress,
                                                      FileSystem **out_backup_fs,
                                                      Path **out_root)
{
    RC rc = RC::OK;

    assert(storage->type == ArgStorage);
    FileSystem *backup_fs = local_fs_;
    if (storage->storage->type == RCloneStorage ||
        storage->storage->type == RSyncStorage) {
        backup_fs = storage_tool_->asCachedReadOnlyFS(storage->storage,
                                                      progress);
    }
    unique_ptr<Restore> restore  = newRestore(backup_fs);
    if (out_backup_fs) { *out_backup_fs = backup_fs; }
    if (out_root) { *out_root = storage->storage->storage_location; }

    rc = restore->lookForPointsInTime(PointInTimeFormat::absolute_point,
                                      storage->storage->storage_location);

    if (rc.isErr()) {
        error(COMMANDLINE, "no points in time found in storage: %s\n", storage->storage->storage_location->c_str());
        return NULL;
    }

    if (pointintime != "") {
        auto point = restore->setPointInTime(pointintime);
        if (!point) {
            error(COMMANDLINE, "no such point in time!\n");
            return NULL;
        }
    }

    rc = restore->loadBeakFileSystem(storage);
    if (rc.isErr()) {
        error(COMMANDLINE, "Could not load beak file system.\n");
        return NULL;
    }

    return restore;
}

RC BeakImplementation::shell(Settings *settings)
{
    RC rc = RC::OK;

    assert(settings->from.type == ArgStorage);

    string storage = "";
    if (settings->from.type == ArgStorage) {
        storage = settings->from.storage->storage_location->str();
    } else if (settings->from.type == ArgDir) {
        storage = settings->from.dir->str();
    }

    Path *mount = local_fs_->mkTempDir("beak_shell_");
    Path *stop = local_fs_->mkTempFile("beak_shell_stop_", "echo Unmounting backup "+storage);
    Path *start = local_fs_->mkTempFile("beak_shell_start_",
                                        "trap "+stop->str()+" EXIT"
                                        +"; cd "+mount->str()
                                        +"; echo Mounted "+storage
                                        +"; echo Exit shell to unmount backup.\n");
    FileStat fs;
    fs.setAsExecutable();
    local_fs_->chmod(start, &fs);
    local_fs_->chmod(stop, &fs);

    settings->to.type = ArgDir;
    settings->to.dir = mount;
    settings->fuse_args.push_back(mount->str());
    settings->updateFuseArgsArray();

    rc = mountRestore(settings);
    if (rc.isErr()) goto cleanup;

    rc = sys_->invokeShell(start);

    rc = umountRestore(settings);

cleanup:

    local_fs_->deleteFile(start);
    local_fs_->deleteFile(stop);
    local_fs_->rmDir(mount);

    return rc;
}

RC BeakImplementation::prune(Settings *settings)
{
    RC rc = RC::OK;

    assert(settings->from.type == ArgStorage);

    auto progress = newProgressStatistics(settings->progress);
    FileSystem *backup_fs;
    Path *root;
    auto restore = accessBackup_(&settings->from, "", progress.get(), &backup_fs, &root);
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
        if (rc.isErr()) {
            usageError(PRUNE, "Cannot parse date time \"%s\"\n", settings->now.c_str());
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
        }
        prune->addPointInTime(i.point());
        num_existing_points_in_time++;
    }

    map<uint64_t,bool> keeps;

    // Perform the prune calculation
    prune->prune(&keeps);

    int num_kept_points_in_time = 0;

    for (auto& i : restore->historyOldToNew())
    {
        if (keeps[i.point()]) {
            // We should keep this point in time, lets remember all the tars required.
            num_kept_points_in_time++;
            for (auto& t : *(i.tars())) {
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
    }

    if (settings->dryrun == false)
    {
        auto proceed = settings->yesprune ? UIYes : UINo;

        if (UI::isatty()) {
            proceed = UI::yesOrNo("Proceed?");
        }

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

RC BeakImplementation::diff(Settings *settings)
{
    RC rc = RC::OK;

    assert(settings->from.type == ArgOrigin || settings->from.type == ArgRule || settings->from.type == ArgStorage);
    assert(settings->to.type == ArgOrigin || settings->to.type == ArgRule || settings->to.type == ArgStorage);

    auto progress = newProgressStatistics(settings->progress);

    FileSystem *curr_fs = NULL;
    FileSystem *old_fs = NULL;
    Path *curr_path =NULL;
    Path *old_path = NULL;

    unique_ptr<Restore> restore_curr;

    // Setup the curr file system.
    if (settings->from.type == ArgOrigin) {
        curr_fs = origin_tool_->fs();
        curr_path = settings->from.origin;
    } else if (settings->from.type == ArgStorage) {
        restore_curr = accessBackup_(&settings->from, settings->from.point_in_time, progress.get());
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
    if (settings->to.type == ArgOrigin) {
        old_fs = origin_tool_->fs();
        old_path = settings->to.origin;
    } else if (settings->to.type == ArgStorage) {
        restore_old = accessBackup_(&settings->to, settings->to.point_in_time, progress.get());
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

RC BeakImplementation::fsck(Settings *settings)
{
    RC rc = RC::OK;

    assert(settings->from.type == ArgStorage);

    auto progress = newProgressStatistics(settings->progress);
    FileSystem *backup_fs;
    Path *root;
    auto restore = accessBackup_(&settings->from, "", progress.get(), &backup_fs, &root);

    set<Path*> required_beak_files;
    vector<pair<Path*,FileStat>> existing_beak_files;
    set<Path*> set_of_existing_beak_files;
    size_t total_files_size = 0;

    for (auto& i : restore->historyOldToNew())
    {
        Path *p = Path::lookup(i.filename);
        required_beak_files.insert(p);
        for (auto& t : *(i.tars()))
        {
            required_beak_files.insert(t);
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
        debug(FSCK, "existing: %s\n", p.first->c_str());
        set_of_existing_beak_files.insert(p.first);
        total_files_size += p.second.st_size;
        if (required_beak_files.count(p.first) == 0)
        {
            verbose(FSCK, "superfluous: %s\n", p.first->c_str());
            superfluous_files.push_back(p.first);
            superfluous_files_size += p.second.st_size;
        }
    }

    bool lost_file = false;
    for (auto p : required_beak_files)
    {
        if (set_of_existing_beak_files.count(p) == 0)
        {
            verbose(FSCK, "lost: %s\n", p->c_str());
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
                for (auto& t : *(i.tars()))
                {
                    if (set_of_existing_beak_files.count(t) == 0) {
                        missing = true;
                        break;
                    }
                }
            }
            if (missing) {
                warning(FSCK, "Broken %s\n", i.datetime.c_str());
                broken_points_in_time.push_back(Path::lookup(i.filename));
            } else {
                warning(FSCK, "OK     %s\n", i.datetime.c_str());
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

    return rc;
}

RC BeakImplementation::configure(Settings *settings)
{
    return configuration_->configure();
}

RC BeakImplementation::push(Settings *settings)
{
    return RC::ERR;
}

RC BeakImplementation::pull(Settings *settings)
{
    return RC::ERR;
}

RC BeakImplementation::umountDaemon(Settings *settings)
{
    return sys_->umountDaemon(settings->from.dir);
}

RC BeakImplementation::mountBackupDaemon(Settings *settings)
{
    Path *dir;
    ptr<FileSystem> origin_fs = origin_tool_->fs();
    origin_fs->enableWatch();
    if (settings->to.type == ArgOrigin) {
        dir = settings->to.origin;
    }
    assert(settings->to.type == ArgDir);
    dir = settings->to.dir;

    unique_ptr<Backup> backup  = newBackup(origin_fs);
    RC rc = backup->scanFileSystem(&settings->from, settings);

    if (rc.isErr()) {
        return RC::ERR;
    }

    return sys_->mountDaemon(dir, backup->asFuseAPI(), settings->foreground, settings->fusedebug);
}

RC BeakImplementation::mountBackup(Settings *settings, ProgressStatistics *progress)
{
    ptr<FileSystem> fs = origin_tool_->fs();

    unique_ptr<Backup> backup  = newBackup(fs);
    RC rc = backup->scanFileSystem(&settings->from, settings);

    if (rc.isErr()) {
        return RC::ERR;
    }

    backup_fuse_mount_ = sys_->mount(settings->to.dir, backup->asFuseAPI(), settings->fusedebug);
    return RC::OK;
}

RC BeakImplementation::umountBackup(Settings *settings)
{
    ptr<FileSystem> fs = origin_tool_->fs();
    int unpleasant_modifications = fs->endWatch();
    if (unpleasant_modifications > 0) {
        warning(COMMANDLINE, "Warning! Origin directory modified while being mounted for backup!\n");
    }
    sys_->umount(backup_fuse_mount_);
    return RC::OK;
}

RC BeakImplementation::mountRestoreDaemon(Settings *settings)
{
    return mountRestoreInternal_(settings, true, NULL);
}

RC BeakImplementation::mountRestore(Settings *settings, ProgressStatistics *progress)
{
    return mountRestoreInternal_(settings, false, progress);
}

RC BeakImplementation::mountRestoreInternal_(Settings *settings, bool daemon, ProgressStatistics *progress)
{
    auto restore  = accessBackup_(&settings->from, settings->from.point_in_time, progress);
    if (!restore) {
        return RC::ERR;
    }

    if (daemon) {
        return sys_->mountDaemon(settings->to.dir, restore->asFuseAPI(), settings->foreground, settings->fusedebug);
    } else {
        restore_fuse_mount_ = sys_->mount(settings->to.dir, restore->asFuseAPI(), settings->fusedebug);
    }

    return RC::OK;
}

RC BeakImplementation::umountRestore(Settings *settings)
{
    sys_->umount(restore_fuse_mount_);
    return RC::OK;
}

thread_local struct timespec mtim_max {};  /* time of last modification */
thread_local struct timespec ctim_max {};  /* time of last meta data modification */

void update_mctim_maxes(const struct stat *sb)
{
#if HAS_ST_MTIM
    const struct timespec *mt = &sb->st_mtim;
    const struct timespec *ct = &sb->st_ctim;
#elif HAS_ST_MTIME
    struct timespec smt {};
    smt.tv_sec = sb->st_mtime;
    const struct timespec *mt = &smt;
    struct timespec sct {};
    sct.tv_sec = sb->st_ctime;
    const struct timespec *ct = &sct;
#else
#error
#endif

    if (mt->tv_sec > mtim_max.tv_sec || (mt->tv_sec == mtim_max.tv_sec && mt->tv_nsec > mtim_max.tv_nsec))
    {
        // Found a more recent timestamp for mtime.
        mtim_max = *mt;
    }
    if (ct->tv_sec > ctim_max.tv_sec || (ct->tv_sec == ctim_max.tv_sec && ct->tv_nsec > ctim_max.tv_nsec))
    {
        // Found a more recent timestamp for mtime.
        ctim_max = *ct;
    }
}

RC BeakImplementation::status(Settings *settings)
{
    RC rc = RC::OK;

    assert(settings->from.type == ArgRule);

    Rule *rule = settings->from.rule;

    info(COMMANDLINE, "Scanning %s...", rule->origin_path->c_str());

    memset(&mtim_max, 0, sizeof(mtim_max));
    memset(&ctim_max, 0, sizeof(ctim_max));
    uint64_t start = clockGetTimeMicroSeconds();
    auto progress = newProgressStatistics(settings->progress);
    rc = origin_tool_->fs()->recurse(rule->origin_path,
                                     [=](const char *path, const struct stat *sb) {
                                         update_mctim_maxes(sb);
                                         return RecurseContinue;
                                     });

    uint64_t stop = clockGetTimeMicroSeconds();
    uint64_t store_time = stop - start;

    info(COMMANDLINE, "in %jdms.\n", store_time / 1000);

    char most_recent_mtime[20];
    memset(most_recent_mtime, 0, sizeof(most_recent_mtime));
    strftime(most_recent_mtime, 20, "%Y-%m-%d_%H:%M:%S", localtime(&mtim_max.tv_sec));

    char most_recent_ctime[20];
    memset(most_recent_ctime, 0, sizeof(most_recent_ctime));
    strftime(most_recent_ctime, 20, "%Y-%m-%d_%H:%M:%S", localtime(&ctim_max.tv_sec));

    info(COMMANDLINE, "mtime=%s ctime=%s\n", most_recent_mtime, most_recent_ctime);

    return rc;
}

void BeakImplementation::genAutoComplete(string filename)
{
    FILE *f = fopen(filename.c_str(),"wb");
    if (!f) {
        error(COMMANDLINE, "Could not open %s\n", filename.c_str());
    }
    fwrite(autocomplete, 1, strlen(autocomplete), f);
    fclose(f);
}

bool BeakImplementation::hasPointsInTime_(Path *path, FileSystem *fs)
{
    if (path == NULL) return false;

    vector<Path*> contents;
    if (!fs->readdir(path, &contents)) {
        return false;
    }
    for (auto f : contents)
    {
        TarFileName tfn;
        bool ok = tfn.parseFileName(f->str());

        if (ok && tfn.isIndexFile()) {
            return true;
        }
    }

    return false;
}

void Settings::updateFuseArgsArray()
{
    fuse_argc = fuse_args.size();
    fuse_argv = new char*[fuse_argc+1];
    int j = 0;
    if (fuse_args.size() > 0)
    {
        debug(FUSE, "call fuse:\n", fuse_argc);
        for (auto &s : fuse_args) {
            fuse_argv[j] = (char*)s.c_str();
            debug(FUSE, "arg \"%s\"\n", fuse_argv[j]);
            j++;
        }
    }
    fuse_argv[j] = 0;
}


Settings::~Settings()
{
    if (fuse_argv) { delete [] fuse_argv; }
}
