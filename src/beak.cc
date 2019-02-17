/*
 Copyright (C) 2016-2018 Fredrik Öhrström

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

const char *autocomplete =
#include"generated_autocomplete.h"
    ;

#include <algorithm>
#include <assert.h>
#include <memory.h>
#include <limits.h>
#include <stddef.h>
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
static ComponentId STORE = registerLogComponent("store");
static ComponentId FUSE = registerLogComponent("fuse");
static ComponentId PRUNE = registerLogComponent("prune");

struct CommandEntry;
struct OptionEntry;

struct BeakImplementation : Beak {

    BeakImplementation(ptr<Configuration> configuration,
                       ptr<System> sys,
                       ptr<FileSystem> local_fs,
                       ptr<StorageTool> storage_tool,
                       ptr<OriginTool> origin_tool);

    void printCommands();
    void printSettings();

    void captureStartTime() {  ::captureStartTime(); }
    Command parseCommandLine(int argc, char **argv, Settings *settings);

    void printHelp(Command cmd);
    void printVersion();
    void printLicense();

    RC configure(Settings *settings);
    RC diff(Settings *settings);
    RC fsck(Settings *settings);
    RC push(Settings *settings);
    RC pull(Settings *settings);
    RC prune(Settings *settings);

    RC umountDaemon(Settings *settings);

    RC mountBackupDaemon(Settings *settings);
    RC mountBackup(Settings *settings, ProgressStatistics *progress = NULL);
    RC umountBackup(Settings *settings);

    RC mountRestoreDaemon(Settings *settings);
    RC mountRestore(Settings *settings, ProgressStatistics *progress = NULL);
    RC umountRestore(Settings *settings);

    RC shell(Settings *settings);

    RC status(Settings *settings);
    RC store(Settings *settings);
    RC restore(Settings *settings);

    void genAutoComplete(string filename);

    private:

    string argsToVector_(int argc, char **argv, vector<string> *args);
    unique_ptr<Restore> accessBackup_(Argument *storage,
                                      string pointintime,
                                      ProgressStatistics *progress,
                                      FileSystem **out_backup_fs = NULL,
                                      Path **out_root = NULL);
    RC mountRestoreInternal_(Settings *settings, bool daemon, ProgressStatistics *progress);
    bool hasPointsInTime_(Path *path, FileSystem *fs);

    map<string,CommandEntry*> commands_;
    map<string,OptionEntry*> short_options_;
    map<string,OptionEntry*> long_options_;

    OptionEntry *nosuch_option_;

    vector<PointInTime> history_;

    CommandEntry *parseCommand(string s);
    OptionEntry *parseOption(string s, bool *has_value, string *value);
    Argument parseArgument(std::string arg, ArgumentType expected_type, Settings *settings, Command cmd);

    unique_ptr<FuseMount> backup_fuse_mount_;
    unique_ptr<FuseMount> restore_fuse_mount_;

    ptr<Configuration> configuration_;
    ptr<System> sys_;
    ptr<FileSystem> local_fs_;
    ptr<StorageTool> storage_tool_;
    ptr<OriginTool> origin_tool_;
};

unique_ptr<Beak> newBeak(ptr<Configuration> configuration,
                         ptr<System> sys,
                         ptr<FileSystem> local_fs,
                         ptr<StorageTool> storage_tool,
                         ptr<OriginTool> origin_tool)
{
    return unique_ptr<Beak>(new BeakImplementation(configuration, sys, local_fs, storage_tool, origin_tool));
}

struct CommandEntry {
    const char *name;
    Command cmd;
    const char *info;
    ArgumentType expected_from, expected_to;
};

CommandEntry command_entries_[] = {
#define X(name,info,expfrom,expto) { #name, name##_cmd, info, expfrom, expto } ,
LIST_OF_COMMANDS
#undef X
};

struct OptionEntry {
    const char *shortname;
    const char *name;
    Option option;
    bool requires_value;
    const char *info;
};

OptionEntry option_entries_[] = {
#define X(shortname,name,type,requiresvalue,info) { #shortname, #name, name##_option, requiresvalue, info} ,
LIST_OF_OPTIONS
#undef X
};

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
    for (auto &e : command_entries_) {
        if (e.cmd != nosuch_cmd) {
            commands_[e.name] = &e;
        }
    }
    string m = "-";
    for (auto &e : option_entries_) {
        if (e.option != nosuch_option) {
            short_options_[m+e.shortname] = &e;
            long_options_[m+m+e.name] = &e;
        } else {
            nosuch_option_ = &e;
        }
    }
}

CommandEntry *BeakImplementation::parseCommand(string s)
{
    if (commands_.count(s) == 0) return NULL;
    return commands_[s];
}

OptionEntry *BeakImplementation::parseOption(string s, bool *has_value, string *value)
{
    OptionEntry *ce = short_options_[s];
    if (ce) {
        *has_value = false;
        return ce;
    }
    size_t p = s.find("=");
    if (p == string::npos) {
        ce = long_options_[s];
        if (!ce) return nosuch_option_;
        *has_value = false;
        return ce;
    }
    string o = s.substr(0,p);
    ce = long_options_[o];
    if (!ce) return nosuch_option_;
    *has_value = true;
    *value = s.substr(p+1);
    return ce;
}

Argument BeakImplementation::parseArgument(string arg, ArgumentType expected_type, Settings *settings, Command cmd)
{
    Argument argument;

    assert(expected_type != ArgUnspecified && expected_type != ArgNone);

    auto at = arg.find_last_of('@');
    auto colon = arg.find(':');
    if (at != string::npos && (colon == string::npos || at > colon))
    {
        // An @ sign after the colon is a reference to a point in time.
        // Example @0 in: s3_work_crypt:@0
        //            in: user@backupserver:/backups@0
        //            in: /media/you/USBDevice@0
        auto point = arg.substr(at);
        arg = arg.substr(0,at);
        if (expected_type != ArgStorage && expected_type != ArgORS && expected_type != ArgNORS) {
            error(COMMANDLINE, "A point in time must only be suffixed to a storage.\n");
        }
        argument.point_in_time = point;
        debug(COMMANDLINE, "found point in time (%s) after storage %s\n", point.c_str(), arg.c_str());
    }

    if (expected_type == ArgDir) {
        Path *dir = Path::lookup(arg);
        Path *rp = dir->realpath();
        if (!rp)
        {
            error(COMMANDLINE, "Expected directory. Got \"%s\" instead.\n", arg.c_str());
        }
        argument.dir = rp;
        argument.type = ArgDir;
        debug(COMMANDLINE, "found directory arg \"%s\", as expected.\n", dir->c_str());
        return argument;
    }

    if (expected_type == ArgFile) {
        Path *file = Path::lookup(arg);
        Path *rp = file->realpath();
        if (!rp)
        {
            error(COMMANDLINE, "Expected file. Got \"%s\" instead.\n", arg.c_str());
        }
        argument.file = rp;
        argument.type = ArgFile;
        debug(COMMANDLINE, "found file arg \"%s\", as expected.\n", file->c_str());
        return argument;
    }

    if (expected_type == ArgORS || expected_type == ArgStorage) {
        Path *storage_location = Path::lookup(arg);
        Storage *storage = configuration_->findStorageFrom(storage_location);
        if (!storage && cmd == store_cmd) {
            // If we are storing, then try to create a missing directory.
            storage = configuration_->createStorageFrom(storage_location);
        }
        if (storage) {
            argument.type = ArgStorage;
            argument.storage = storage;

            switch (storage->type) {
            case FileSystemStorage: debug(COMMANDLINE, "storage \"%s\" parsed as directory.\n", arg.c_str()); break;
            case RCloneStorage: debug(COMMANDLINE, "storage \"%s\" parsed as rclone.\n", arg.c_str()); break;
            case RSyncStorage: debug(COMMANDLINE, "storage \"%s\" parsed as rsync.\n", arg.c_str()); break;
            case NoSuchStorage: break;
            }
            debug(COMMANDLINE, "found storage arg \"%s\", as expected.\n", storage_location->c_str());
            return argument;
        }

        if (expected_type == ArgStorage) {
            error(COMMANDLINE, "Expected storage, but \"%s\" is not a storage location.\n", arg.c_str());
        }

        // ArgORS will pass through here.
    }

    if (expected_type == ArgORS || expected_type == ArgRule || expected_type == ArgOrigin) {
        Rule *rule = configuration_->rule(arg);

        if (rule) {
            argument.rule = rule;
            argument.origin = argument.rule->origin_path;
            argument.type = ArgRule;
            debug(COMMANDLINE, "found rule arg %s pointing to origin %s\n",
                  arg.c_str(), argument.rule->origin_path->c_str());
            return argument;
        }

        if (expected_type == ArgRule) {
            // We expected a rule, but there was none....
            error(COMMANDLINE, "Expected a rule. Got \"%s\" instead.\n", arg.c_str());
        }

        // If there is no rule, then we expect an origin directory.
        Path *origin = Path::lookup(arg);
        Path *rp = origin->realpath();
        if (rp) {
            if (hasPointsInTime_(rp, origin_tool_->fs()) && !settings->yesorigin) {
                error(COMMANDLINE, "You passed a storage location as an origin. If this is what you want add --yes-origin\n");
            }
            argument.origin = rp;
            argument.type = ArgOrigin;
            debug(COMMANDLINE, "found origin arg \"%s\".\n", origin->c_str());
            return argument;
        }

        if (expected_type == ArgOrigin)
        {
            error(COMMANDLINE, "Expected rule or origin directory. Got \"%s\" instead.\n", arg.c_str());
        }

        // ArgORS will pass through here.
    }

    error(COMMANDLINE, "Expected rule, origin directory or storage location. Got \"%s\" instead.\n", arg.c_str());

    return argument;
}

void BeakImplementation::printCommands()
{
    fprintf(stdout, "Available Commands:\n");

    size_t max = 0;
    for (auto &e : command_entries_) {
        size_t l = strlen(e.name);
        if (l > max) max = l;
    }

    for (auto &e : command_entries_) {
        if (e.cmd == nosuch_cmd) continue;
        size_t l = strlen(e.name);
        fprintf(stdout, "  %s%-.*s%s\n",
                e.name,
                (int)(max-l+4),
                "                                        ",
                e.info);
    }
}

bool isExperimental(OptionEntry &e)
{
    const char *exp = "Experimental!";
    size_t len = strlen(e.info);
    size_t explen = strlen(exp);

    if (len > strlen(exp)) {
        return 0 == strncmp(exp, e.info+len-explen, explen);
    }
    return false;
}

void BeakImplementation::printSettings()
{
    fprintf(stdout, "Settings:\n");

    size_t max = 0;
    for (auto &e : option_entries_)
    {
        if (isExperimental(e)) continue;
        size_t l = strlen(e.name);
        if (l > max) max = l;
    }

    for (auto &e : option_entries_)
    {
        if (e.option == nosuch_option) continue;
        if (isExperimental(e)) continue;

        string sn = e.shortname;
        size_t sl = strlen(e.shortname);
        if (sl > 0) {
            sn = string("-")+e.shortname;
            sl++;
        }

        string n = e.name;
        size_t l = strlen(e.name);
        if (n[n.length()-1] == '_')
        {
            n = "";
            l = 0;
        }
        else
        {
            n = string("--")+e.name;
            l += 2;
        }
        fprintf(stdout, "  %s"
                        "%-.*s"
                        "%s"
                        "%-.*s"
                        "%s\n",
                sn.c_str(),
                (int)(4-sl),
                "                                        ",
                n.c_str(),
                (int)(max-l+4),
                "                                        ",
                e.info);
    }
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

const char *arg_name_(ArgumentType at) {

    switch (at) {

    case ArgUnspecified:
        return "?";
    case ArgNone:
        return "no";
    case ArgOrigin:
        return "origin";
    case ArgRule:
        return "rule";
    case ArgStorage:
        return "storage";
    case ArgDir:
        return "dir";
    case ArgFile:
        return "file";
    case ArgORS:
        return "origin, rule or storage";
    case ArgNORS:
        return "?";
    }
    return "?";
}

Command BeakImplementation::parseCommandLine(int argc, char **argv, Settings *settings)
{
    vector<string> args;
    argsToVector_(argc, argv, &args);

    settings->help_me_on_this_cmd = nosuch_cmd;
    settings->fuse_args.push_back("beak"); // Application name
    settings->pointintimeformat = both_point;

    if (args.size() < 1) return nosuch_cmd;

    CommandEntry *cmde = parseCommand(args[0]);
    Command cmd = nosuch_cmd;
    if (cmde != NULL) cmd = cmde->cmd;

    if (cmd == nosuch_cmd) {
        if (args[0] == "") {
            cmd = help_cmd;
            return cmd;
        }
        if (args[0] == "") {
            cmd = help_cmd;
            return cmd;
        }
        fprintf(stderr, "No such command \"%s\"\n", args[0].c_str());
        return cmd;
    }

    settings->depth = 2; // Default value

    auto i = args.begin();
    i = args.erase(i);

    if ((*i) == "help") {
        // beak push help
        // To push a directory "help" do:
        //     beak push -- help
        settings->help_me_on_this_cmd = cmd;
        cmd = help_cmd;
        return cmd;
    }

    bool options_completed = false;
    for (;i != args.end(); ++i)
    {
        int l = i->length();
        if (!l) continue;

        if (*i == "--") {
            options_completed = true;
            continue;
        }

        if (!options_completed)
        {
            bool contains_value;
            string value;
            OptionEntry *ope = parseOption(*i, &contains_value, &value);
            Option op = ope->option;
            if (op != nosuch_option) {
                if (!ope->requires_value && contains_value) {
                    error(COMMANDLINE,"Option \"%s\" should not have a value specified.\n", ope->name);
                }
                if (ope->requires_value) {
                    if (!contains_value) {
                        // The value was not encoded in the option string (eg --targetsize=10M)
                        // thus pick the next arg as the value instead.
                        i++;
                        value = *i;
                    }
                }
            }
            switch (op) {
            case cache_option:
                settings->cache = value;
                break;
            case contentsplit_option:
                settings->contentsplit.push_back(value);
                break;
            case depth_option:
                settings->depth = atoi(value.c_str());
                settings->depth_supplied = true;
                if ((cmd == store_cmd || cmd == bmount_cmd) && settings->depth < 1) {
                    error(COMMANDLINE, "For store/bmount depth (-d) cannot be set to "
                          "less than 1, ie the root.\n");
                }
                if ((cmd == diff_cmd) && settings->depth < 0) {
                    error(COMMANDLINE, "For diff depth (-d) cannot be set to "
                          "less than 0, ie the root.\n");
                }
                break;
            case dryrun_option:
                settings->dryrun = true;
                settings->dryrun_supplied = true;
                break;
            case foreground_option:
                settings->foreground = true;
                break;
            case fusedebug_option:
                settings->fusedebug = true;
                break;
            case include_option:
                settings->include.push_back(value);
                break;
            case keep_option:
                settings->keep = value;
                settings->keep_supplied = true;
                break;
            case license_option:
                settings->license = true;
                break;
            case log_option:
                settings->log = value;
                setLogComponents(settings->log.c_str());
                setLogLevel(DEBUG);
                break;
            case listlog_option:
                listLogComponents();
                exit(0);
                break;
            case pointintimeformat_option:
                if (value == "absolute") settings->pointintimeformat = absolute_point;
                else if (value == "relative") settings->pointintimeformat = relative_point;
                else if (value == "both") settings->pointintimeformat = both_point;
                else {
                    error(COMMANDLINE, "No such point in time format \"%s\".", value.c_str());
                }
                break;
            case progress_option:
                if (value == "none") settings->progress = ProgressDisplayNone;
                else if (value == "terminal") settings->progress = ProgressDisplayTerminal;
                else if (value == "ansi") settings->progress = ProgressDisplayTerminalAnsi;
                else if (value == "notifications") settings->progress = ProgressDisplayNotification;
                else {
                    error(COMMANDLINE, "No such progress display type \"%s\".", value.c_str());
                }
                break;
            case robot_option:
                settings->robot = true;
                break;
            case tarheader_option:
            {
                if (value == "none") settings->tarheader = TarHeaderStyle::None;
                else if (value == "simple") settings->tarheader = TarHeaderStyle::Simple;
                else if (value == "full") settings->tarheader = TarHeaderStyle::Full;
                else {
                    error(COMMANDLINE, "No such tar header style \"%s\".", value.c_str());
                }
                settings->tarheader_supplied = true;
            }
            break;
            case targetsize_option:
            {
                size_t parsed_size;
                RC rc = parseHumanReadable(value.c_str(), &parsed_size);
                if (rc.isErr())
                {
                    error(COMMANDLINE,
                          "Cannot set target size because \"%s\" is not a proper number (e.g. 1,2K,3M,4G,5T)\n",
                          value.c_str());
                }
                settings->targetsize = parsed_size;
                settings->targetsize_supplied = true;
            }
            break;
            case triggersize_option:
            {
                size_t parsed_size;
                RC rc = parseHumanReadable(value.c_str(), &parsed_size);
                if (rc.isErr())
                {
                    error(COMMANDLINE,
                          "Cannot set trigger size because \"%s\" is not a proper number (e.g. 1,2K,3M,4G,5T)\n",
                          value.c_str());
                }
                settings->triggersize = parsed_size;
                settings->triggersize_supplied = true;
            }
            break;
            case splitsize_option:
            {
                size_t parsed_size;
                RC rc = parseHumanReadable(value.c_str(), &parsed_size);
                if (rc.isErr())
                {
                    error(COMMANDLINE,
                          "Cannot set split size because \"%s\" is not a proper number (e.g. 1,2K,3M,4G,5T)\n",
                          value.c_str());
                }
                // Mask the lowest 9 bits to make the split size a multiple of 512.
                size_t masked_size = parsed_size & ~((size_t)0x1ff);
                if (masked_size != parsed_size) {
                    fprintf(stderr, " FROM %zu to %zu\n", parsed_size, masked_size);
                }
                settings->splitsize = parsed_size;
                settings->splitsize_supplied = true;
            }
            break;
            case triggerglob_option:
                settings->triggerglob.push_back(value);
                break;
            case verbose_option:
                settings->verbose = true;
                setLogLevel(VERBOSE);
                break;
            case quite_option:
                settings->quite = true;
                setLogLevel(QUITE);
                break;
            case exclude_option:
                settings->exclude.push_back(value);
                break;
            case yesorigin_option:
                settings->yesorigin = true;
                break;
            case nosuch_option:
                if ((*i)[0] == '-' && !options_completed) {
                    // It looks like an option, but we could not find it!
                    // And we have not yet stopped looking for options!
                    // Thus an error!
                    error(COMMANDLINE, "No such option \"%s\"\n", (*i).c_str());
                }
                options_completed = true;
            }
        }

        if (options_completed)
        {
            if (settings->from.type == ArgUnspecified)
            {
                settings->from = parseArgument(*i, cmde->expected_from, settings, cmd);
            }
            else if (settings->to.type == ArgUnspecified)
            {
                settings->to = parseArgument(*i, cmde->expected_to, settings, cmd);
                if (settings->to.type == ArgOrigin)
                {
                    settings->fuse_args.push_back(settings->to.origin->c_str());
                }
                if (settings->to.type == ArgDir)
                {
                    settings->fuse_args.push_back(settings->to.dir->c_str());
                }
            }
            else {
                error(COMMANDLINE, "Superfluous argument %s\n", (*i).c_str());
            }
        }
    }


    if (cmde->expected_from != ArgNone)
    {
        if (settings->from.type == ArgUnspecified)
        {
            error(COMMANDLINE, "Command expects %s as first argument.\n", arg_name_(cmde->expected_from));
        }
        if (cmde->expected_to != ArgNone)
        {
            if (settings->to.type == ArgUnspecified)
            {
                error(COMMANDLINE, "Command expects %s as second argument.\n", arg_name_(cmde->expected_to));
            }
        }
    }
    settings->updateFuseArgsArray();

    return cmd;
}

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
        error(COMMANDLINE, "No points in time found!\n");
        return NULL;
    }

    if (pointintime != "") {
        auto point = restore->setPointInTime(pointintime);
        if (!point) {
            error(STORE, "No such point in time!\n");
            return NULL;
        }
    }

    rc = restore->loadBeakFileSystem(storage);
    if (rc.isErr()) {
        error(STORE, "Could not load beak file system.\n");
        return NULL;
    }

    return restore;
}

RC BeakImplementation::restore(Settings *settings)
{
    uint64_t start = clockGetTimeMicroSeconds();
    auto progress = newProgressStatistics(settings->progress);
    progress->startDisplayOfProgress();

    umask(0);
    RC rc = RC::OK;

    auto restore  = accessBackup_(&settings->from, settings->to.point_in_time, progress.get());
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

    debug(STORE, "work to be done: num_files=%ju num_hardlinks=%ju num_symlinks=%ju num_nodes=%ju num_dirs=%ju\n",
          progress->stats.num_files, progress->stats.num_hard_links, progress->stats.num_symbolic_links,
          progress->stats.num_nodes, progress->stats.num_dirs);

    origin_tool_->restoreFileSystem(backup_fs, backup_contents_fs, restore.get(), point, settings, progress.get());

    uint64_t stop = clockGetTimeMicroSeconds();
    uint64_t store_time = stop - start;

    progress->finishProgress();

    if (progress->stats.num_files_stored == 0 && progress->stats.num_symbolic_links_stored == 0 &&
        progress->stats.num_dirs_updated == 0) {
        info(STORE, "No stores needed, everything was up to date.\n");
    } else {
        if (progress->stats.num_files_stored > 0) {
            string file_sizes = humanReadable(progress->stats.size_files_stored);
            info(STORE, "Stored %ju files for a total size of %s.\n", progress->stats.num_files_stored, file_sizes.c_str());
        }
        if (progress->stats.num_symbolic_links_stored > 0) {
            info(STORE, "Stored %ju symlinks.\n", progress->stats.num_symbolic_links_stored);
        }
        if (progress->stats.num_hard_links_stored > 0) {
            info(STORE, "Stored %ju hard links.\n", progress->stats.num_hard_links_stored);
        }
        if (progress->stats.num_dirs_updated > 0) {
            info(STORE, "Updated %ju dirs.\n", progress->stats.num_dirs_updated);
        }
        info(STORE, "Time to store %jdms.\n", store_time / 1000);
    }
    return rc;
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
    auto prune = newPrune(clockGetUnixTimeNanoSeconds(), keep);

    set<Path*> all_tars;

    for (auto pit = restore->history().rbegin(); pit != restore->history().rend(); pit++)
    {
        prune->addPointInTime(pit->point());
    }

    map<uint64_t,bool> keeps;
    prune->prune(&keeps);

    for (auto pit = restore->history().rbegin(); pit != restore->history().rend(); pit++)
    {
        if (keeps[pit->point()]) {
            // We should keep this point in time, lets remember all the tars required.
            for (auto& t : *(pit->tars())) {
                all_tars.insert(t);
            }
            Path *p = Path::lookup(pit->filename);
            p = p->prepend(Path::lookupRoot());
            all_tars.insert(p);
        }
    }

    vector<Path*> files;
    backup_fs->listFilesBelow(root, &files, SortOrder::Unspecified);

    vector<Path*> files_to_remove;
    for (auto p : files)
    {
        if (all_tars.count(p) == 0)
        {
            files_to_remove.push_back(p);
        }
        debug(PRUNE,"keeping %s\n", p->c_str());
    }

    for (auto p : files_to_remove)
    {
        debug(PRUNE, "removing %s\n", p->c_str());
    }
    return RC::OK;
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
        curr_path = Path::lookupRoot();
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
        old_path = Path::lookupRoot();
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

    auto progress = newProgressStatistics(settings->progress);
    auto restore  = accessBackup_(&settings->from, settings->from.point_in_time, progress.get());
    if (!restore) {
        return RC::ERR;
    }
    //FileSystem *backup_fs = restore->backupFileSystem(); // Access the archive files storing content.

    //CacheFS *cache_fs = dynamic_cast<CacheFS>(backup_fs); // Get more information about the storage location.
    /*
    vector<TarFileName> existing_files, bad_files;
    vector<string> other_files;
    map<Path*,FileStat> contents;
    //rc = storage_tool_->listBeakFiles(settings->from.storage,
    //&existing_files, &bad_files, &other_files, &contents);

    if (bad_files.size() > 0) {
        UI::output("Found %ju files with the wrong sizes!\n", bad_files.size());
        for (auto& f : bad_files) {
            verbose(CHECK, "Wrong size: %s\n", f.path->c_str());
        }
    }
    if (other_files.size() > 0) {
        UI::output("Found %ju non-beak files!\n", other_files.size());
        for (auto& f : other_files) {
            verbose(CHECK, "Non-beak: %s\n", f.c_str());
        }
    }

    size_t total_size = 0;
    set<Path*> set_of_existing_files;
    vector<TarFileName*> indexes;
    for (auto& f : existing_files) {
        total_size += f.size;
        if (f.type == REG_FILE && f.path->depth() == 1) {
            // This is one of the index files in the root directory.
            indexes.push_back(&f);
        } else {
            // The root index files are not listed inside themselves. Which is a side
            // effect of the index file being named with the hash of the index contents.
            // It cannot have the hash of itself. Well, perhaps it could with a fixpoint
            // calculation/solving, but that is probably not feasable, nor interesting. :-)
            set_of_existing_files.insert(f.path);
        }
    }

    string total = humanReadable(total_size);
    UI::output("Backup location uses %s in %ju beak files.\n", total.c_str(), existing_files.size());

    verbose(CHECK, "Using cache location %s\n", rule->cache_path->c_str());
    //rc = storage_tool_->fetchBeakFilesFromStorage(storage, &indexes, rule->cache_path);

    if (rc.isErr()) return rc;

    set<Path*> set_of_beak_files;
    for (auto& f : indexes) {
        Path *gz = f->path->prepend(rule->cache_path);
        rc = Index::listFilesReferencedInIndex(local_fs_, gz, &set_of_beak_files);
        if (rc.isErr()) {
            failure(CHECK, "Cannot read index file %s\n", gz->c_str());
            return rc;
        }
        set_of_existing_files.erase(f->path);
    }

     UI::output("Found %d points in time.\n", indexes.size());

    vector<Path*> missing_files;
    for (auto p : set_of_existing_files) {
        if (set_of_beak_files.count(p) > 0)
        {
            set_of_beak_files.erase(p);
        }
        else
        {
            missing_files.push_back(p);
        }
    }

    if (set_of_beak_files.size() > 0) {
        UI::output("Found %d superflous files!\n", set_of_beak_files.size());
        for (auto& f : set_of_beak_files) {
            verbose(CHECK, "Superflouous: %s\n", f->c_str());
        }
    }

    if (missing_files.size() > 0) {
        UI::output("Warning! %d missing files!\n", missing_files.size());
        for (auto& f : missing_files) {
            verbose(CHECK, "Missing: %s\n", f->c_str());
        }
    } else {
        UI::output("OK\n");
    }

    */
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
        warning(STORE, "Warning! Origin directory modified while being mounted for backup!\n");
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

void BeakImplementation::printHelp(Command cmd)
{
    switch (cmd) {
    case nosuch_cmd:
        fprintf(stdout,
                "Beak is a backup-mirroring-sharing-rotation-pruning tool\n"
                "\n"
                "Usage:\n"
                "  beak [command] [options] [from] [to]\n"
                "\n");
        printCommands();
        fprintf(stdout,"\n");
        printSettings();
        fprintf(stdout,"\n");
        fprintf(stdout,"Beak is licensed to you under the GPLv3. For details do: "
                "beak help --license\n");
        break;
    default:
        fprintf(stdout, "Sorry, no help for that command yet.\n");
        break;
    }
}

void BeakImplementation::printVersion()
{
    fprintf(stdout, "beak " XSTR(BEAK_VERSION) "\n");
}

void BeakImplementation::printLicense()
{
    fprintf(stdout, "Beak contains software developed:\n"
            "by Fredrik Öhrström Copyright (C) 2016-2018\n"
            "licensed to you under the GPLv3 or later.\n"
            "https://github.com/weetmuts/beak\n\n"
            "This build of beak also includes third party code:\n"
            "openssl-1.0.2l - Many authors, see https://www.openssl.org/community/thanks.html\n"
            "https://github.com/openssl/openssl\n\n"
            "zlib-1.2.11 - Jean-loup Gailly and Mark Adler\n"
            "https://www.zlib.net/\n\n"
            #ifdef PLATFORM_WINAPI
            "WinFsp - Windows File System Proxy, Copyright (C) Bill Zissimopoulos\n"
            "https://github.com/billziss-gh/winfsp\n"
            #endif
            "\n");
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
