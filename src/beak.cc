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
#include "log.h"
#include "origintool.h"
#include "storagetool.h"
#include "system.h"
#include "tarfile.h"
#include "ui.h"
#include "util.h"

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
                                                      Monitor *monitor,
                                                      FileSystem **out_backup_fs,
                                                      Path **out_root)
{
    RC rc = RC::OK;

    assert(storage->type == ArgStorage);
    FileSystem *backup_fs = local_fs_;
    if (storage->storage->type == RCloneStorage ||
        storage->storage->type == RSyncStorage) {
        backup_fs = storage_tool_->asCachedReadOnlyFS(storage->storage, monitor);
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

RC BeakImplementation::configure(Settings *settings)
{
    return configuration_->configure();
}

RC BeakImplementation::pull(Settings *settings, Monitor *monitor)
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
    if (settings->to.type == ArgOrigin) {
        dir = settings->to.origin;
    }
    assert(settings->to.type == ArgDir);
    dir = settings->to.dir;

    unique_ptr<Backup> backup  = newBackup(origin_fs);
    RC rc = backup->scanFileSystem(&settings->from, settings, NULL);

    if (rc.isErr()) {
        return RC::ERR;
    }

    return sys_->mountDaemon(dir, backup->asFuseAPI(), settings->foreground, settings->fusedebug);
}

RC BeakImplementation::mountBackup(Settings *settings, Monitor *monitor)
{
    ptr<FileSystem> fs = origin_tool_->fs();

    unique_ptr<Backup> backup  = newBackup(fs);
    RC rc = backup->scanFileSystem(&settings->from, settings, NULL);

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

RC BeakImplementation::mountRestoreDaemon(Settings *settings, Monitor *monitor)
{
    return mountRestoreInternal_(settings, true, monitor);
}

RC BeakImplementation::mountRestore(Settings *settings, Monitor *monitor)
{
    return mountRestoreInternal_(settings, false, monitor);
}

RC BeakImplementation::mountRestoreInternal_(Settings *settings, bool daemon, Monitor *monitor)
{
    auto restore  = accessBackup_(&settings->from, settings->from.point_in_time, monitor);
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

string buildJobName(const char *cmd, Settings *s)
{
    string r = cmd;

    if (s->from.type == ArgOrigin)
    {
        r += " ";
        r += s->from.origin->str();
    }
    if (s->from.type == ArgStorage)
    {
        r += " ";
        r += s->from.storage->storage_location->str();
    }
    if (s->from.type == ArgRule)
    {
        r += " ";
        r += s->from.rule->name;
    }
    if (s->to.type == ArgOrigin)
    {
        r += " ";
        r += s->to.origin->str();
    }
    if (s->to.type == ArgStorage)
    {
        r += " ";
        r += s->to.storage->storage_location->str();
    }
    return r;
}
