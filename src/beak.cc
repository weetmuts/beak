/*
 Copyright (C) 2016-2017 Fredrik Öhrström

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
#include "log.h"
#include "filesystem.h"
#include "restore.h"
#include "index.h"
#include "origintool.h"
#include "statistics.h"
#include "storagetool.h"
#include "system.h"
#include "tarfile.h"
#include "ui.h"
#include "util.h"

const char *autocomplete =
#include"generated_autocomplete.h"
    ;

#ifdef FUSE_USE_VERSION
#include <fuse/fuse.h>
#else
#include "nofuse.h"
#endif

#include <algorithm>
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
static ComponentId CHECK = registerLogComponent("check");

struct CommandEntry;
struct OptionEntry;

struct BeakImplementation : Beak {

    BeakImplementation(ptr<Configuration> configuration,
                       ptr<System> sys, ptr<FileSystem> sys_fs,
                       ptr<StorageTool> storage_tool,
                       ptr<OriginTool> origin_tool);

    void printCommands();
    void printSettings();

    void captureStartTime() {  ::captureStartTime(); }
    string argsToVector(int argc, char **argv, vector<string> *args);
    Command parseCommandLine(int argc, char **argv, Settings *settings);

    void printHelp(Command cmd);
    void printVersion();
    void printLicense();

    vector<PointInTime> history();
    RC findPointsInTime(string remote, vector<struct timespec> *v);
    RC fetchPointsInTime(string remote, Path *cache);

    RC check(Settings *settings);
    RC configure(Settings *settings);
    RC push(Settings *settings);
    RC prune(Settings *settings);

    RC umountDaemon(Settings *settings);

    RC mountBackupDaemon(Settings *settings);
    RC mountBackup(Settings *settings);
    RC umountBackup(Settings *settings);

    RC mountRestoreDaemon(Settings *settings);
    RC mountRestore(Settings *settings);

    RC status(Settings *settings);
    RC store(Settings *settings);
    RC restore(Settings *settings);

    void genAutoComplete(string filename);

    private:

    RC mountRestoreInternal(Settings *settings, bool daemon);
    bool hasPointsInTime(Path *path, FileSystem *fs);

    fuse_operations restore_fuse_ops;

    map<string,CommandEntry*> commands_;
    map<string,OptionEntry*> short_options_;
    map<string,OptionEntry*> long_options_;

    OptionEntry *nosuch_option_;

    vector<PointInTime> history_;

    CommandEntry *parseCommand(string s);
    OptionEntry *parseOption(string s, bool *has_value, string *value);
    Argument parseArgument(std::string arg, ArgumentType expected_type, Settings *settings);

    unique_ptr<FuseMount> fuse_mount_;
    struct fuse_chan *chan_;
    struct fuse *fuse_;
    pid_t loop_pid_;

    ptr<Configuration> configuration_;
    ptr<System> sys_;
    ptr<FileSystem> sys_fs_;
    ptr<StorageTool> storage_tool_;
    ptr<OriginTool> origin_tool_;
};

unique_ptr<Beak> newBeak(ptr<Configuration> configuration,
                         ptr<System> sys,
                         ptr<FileSystem> sys_fs,
                         ptr<StorageTool> storage_tool,
                         ptr<OriginTool> origin_tool) {
    return unique_ptr<Beak>(new BeakImplementation(configuration, sys, sys_fs, storage_tool, origin_tool));
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
                                       ptr<System> sys, ptr<FileSystem> sys_fs,
                                       ptr<StorageTool> storage_tool,
                                       ptr<OriginTool> origin_tool) :
    configuration_(configuration),
    sys_(sys),
    sys_fs_(sys_fs),
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

Argument BeakImplementation::parseArgument(string arg, ArgumentType expected_type, Settings *settings)
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
        if (expected_type != ArgStorage) {
            error(COMMANDLINE, "A point in time must only be suffixed to a storage.\n");
        }
        argument.point_in_time = point;
        debug(COMMANDLINE, "Found point in time (%s) after storage %s\n", point.c_str(), arg.c_str());
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
        debug(COMMANDLINE, "Found directory arg \"%s\", as expected.\n", dir->c_str());
        return argument;
    }

    if (expected_type == ArgORS || expected_type == ArgStorage) {
        Path *storage_location = Path::lookup(arg);
        Storage *storage = configuration_->findStorageFrom(storage_location);

        if (storage) {
            argument.type = ArgStorage;
            argument.storage = storage;

            switch (storage->type) {
            case FileSystemStorage: debug(COMMANDLINE, "Storage \"%s\" parsed as directory.\n", arg.c_str()); break;
            case RCloneStorage: debug(COMMANDLINE, "Storage \"%s\" parsed as rclone.\n", arg.c_str()); break;
            case RSyncStorage: debug(COMMANDLINE, "Storage \"%s\" parsed as rsync.\n", arg.c_str()); break;
            case NoSuchStorage: break;
            }
            debug(COMMANDLINE, "Found storage arg \"%s\", as expected.\n", storage_location->c_str());
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
            debug(COMMANDLINE, "Found rule arg %s pointing to origin %s\n",
                  arg.c_str(), argument.rule->origin_path->c_str());
            return argument;
        }

        if (expected_type == ArgRule) {
            // We expected a rule, but there was none....
            error(COMMANDLINE, "Expected origin directory or rule. Got \"%s\" instead.\n", arg.c_str());
        }

        // If there is no rule, then we expect an origin directory.
        Path *origin = Path::lookup(arg);
        Path *rp = origin->realpath();
        if (rp) {
            if (hasPointsInTime(rp, origin_tool_->fs().get()) && !settings->yesorigin) {
                error(COMMANDLINE, "You passed a storage location as an origin. If this is what you want add --yes-origin\n");
            }
            argument.origin = rp;
            argument.type = ArgOrigin;
            debug(COMMANDLINE, "Found origin arg \"%s\".\n", origin->c_str());
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

void BeakImplementation::printSettings()
{
    fprintf(stdout, "Settings:\n");

    size_t max = 0;
    for (auto &e : option_entries_) {
        size_t l = strlen(e.name);
        if (l > max) max = l;
    }

    for (auto &e : option_entries_) {
        if (e.option == nosuch_option) continue;

        string sn = e.shortname;
        size_t sl = strlen(e.shortname);
        if (sl > 0) {
            sn = string("-")+e.shortname;
            sl++;
        }

        string n = e.name;
        size_t l = strlen(e.name);
        if (n[n.length()-1] == '_') {
            n = "";
            l = 0;
        } else {
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

string BeakImplementation::argsToVector(int argc, char **argv, vector<string> *args)
{
    args->resize(argc);
    // Skip the program name.
    for (int i=1; i<argc; ++i) {
        (*args)[i-1] = argv[i];
    }
    return argv[0];
}

Command BeakImplementation::parseCommandLine(int argc, char **argv, Settings *settings)
{
    vector<string> args;
    argsToVector(argc, argv, &args);

    settings->help_me_on_this_cmd = nosuch_cmd;
    settings->fuse_args.push_back("beak"); // Application name
    settings->depth = 2; // Default value
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
            case depth_option:
                settings->depth = atoi(value.c_str());
                if (settings->depth < 1) {
                    error(COMMANDLINE, "Option depth (-d) cannot be set to "
                          "less than 1, ie the root.\n");
                }
                break;
            case foreground_option:
                settings->fuse_args.push_back("-f");
                break;
            case fusedebug_option:
                settings->fuse_args.push_back("-d");
                break;
            case include_option:
                settings->include.push_back(value);
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
            case pointintime_option:
                settings->pointintime = value;
                break;
            case pointintimeformat_option:
                if (value == "absolute") settings->pointintimeformat = absolute_point;
                else if (value == "relative") settings->pointintimeformat = relative_point;
                else if (value == "both") settings->pointintimeformat = both_point;
                else {
                    error(COMMANDLINE, "No such point in time format \"%s\".", value.c_str());
                }
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
                settings->from = parseArgument(*i, cmde->expected_from, settings);
                if (settings->from.point_in_time != "") {
                    settings->pointintime = settings->from.point_in_time;
                }
            }
            else if (settings->to.type == ArgUnspecified)
            {
                settings->to = parseArgument(*i, cmde->expected_to, settings);
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

    settings->fuse_argc = settings->fuse_args.size();
    settings->fuse_argv = new char*[settings->fuse_argc+1];
    int j = 0;
    for (auto &s : settings->fuse_args) {
        settings->fuse_argv[j] = (char*)s.c_str();
        j++;
    }
    settings->fuse_argv[j] = 0;

    return cmd;
}

vector<PointInTime> BeakImplementation::history()
{
    vector<PointInTime> tmp; // {}; //reverse_fs.history();
    return tmp;
}

RC BeakImplementation::check(Settings *settings)
{
    RC rc = RC::OK;

    Rule *rule = configuration_->findRuleFromStorageLocation(settings->from.storage->storage_location);
    if (rule == NULL) {
        failure(COMMANDLINE,"You have to specify an storage location.\n");
        return RC::ERR;
    }
    Storage *storage = rule->storage(settings->from.storage->storage_location);
    assert(storage);

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
        rc = Index::listFilesReferencedInIndex(sys_fs_, gz, &set_of_beak_files);
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


    return rc;
}

RC BeakImplementation::configure(Settings *settings)
{
    return configuration_->configure();
}

RC BeakImplementation::push(Settings *settings)
{
    RC rc = RC::OK;

    assert(settings->from.type == ArgRule);

    Rule *rule = settings->from.rule;
    Settings backup_settings = settings->copy();
    Path *mount = sys_fs_->mkTempDir("beak_push_");
    backup_settings.to.dir = mount;
    rc = mountBackup(&backup_settings);
    if (rc.isErr()) return RC::ERR;

    vector<Storage*> storages = rule->sortedStorages();
    Storage *storage = NULL;
    for (auto s : storages) {
        if (s != rule->local) storage = s;
    }
    if (storage == NULL) {
        error(COMMANDLINE, "No remote storage configured for rule \"%s\"\n", rule->name.c_str());
    }

    // Beak file system is now mounted, lets store it into a storage location.
    vector<string> args;
    args.push_back("copy");
    args.push_back("-v");
    args.push_back(mount->c_str());
    args.push_back(storage->storage_location->str());
    vector<char> output;
    rc = sys_->invoke("rclone", args, &output, CaptureBoth,
                      [=](char *buf, size_t len) {
                          fprintf(stderr, "RCLONE COPY: >%*s<\n", (int)len, buf);
                      });
    // Parse verbose output and look for:
    // 2018/01/29 20:05:36 INFO  : code/src/s01_001517180913.689221661_11659264_b6f526ca4e988180fe6289213a338ab5a4926f7189dfb9dddff5a30ab50fc7f3_0.tar: Copied (new)

    // Unmount virtual filesystem.
    rc = umountBackup(&backup_settings);
    rmdir(mount->c_str());

    return rc;
}

RC BeakImplementation::prune(Settings *settings)
{
    return RC::OK;
}

static int open_callback(const char *path, struct fuse_file_info *fi)
{
    return 0;
}

RC BeakImplementation::umountDaemon(Settings *settings)
{
    vector<string> args;
    args.push_back("-u");
    args.push_back(settings->from.dir->str());
    return sys_->invoke("fusermount", args);
}

RC BeakImplementation::mountBackupDaemon(Settings *settings)
{
    Path *dir;
    ptr<FileSystem> fs = origin_tool_->fs();
    fs->enableWatch();
    if (settings->to.type == ArgOrigin) {
        dir = settings->to.origin;
    }
    assert(settings->to.type == ArgDir);
    dir = settings->to.dir;

    auto backup  = newBackup(fs);
    RC rc = backup->scanFileSystem(settings);

    if (rc.isErr()) {
        return RC::ERR;
    }

    return origin_tool_->fs()->mountDaemon(dir, backup.get(), settings->foreground, settings->fusedebug);
}

RC BeakImplementation::mountBackup(Settings *settings)
{
    ptr<FileSystem> fs = origin_tool_->fs();

    auto backup  = newBackup(fs);
    RC rc = backup->scanFileSystem(settings);

    if (rc.isErr()) {
        return RC::ERR;
    }

    fuse_mount_ = origin_tool_->fs()->mount(settings->to.dir, backup.get(), settings->fusedebug);
    return RC::OK;
}

RC BeakImplementation::umountBackup(Settings *settings)
{
    ptr<FileSystem> fs = origin_tool_->fs();
    int unpleasant_modifications = fs->endWatch();
    if (unpleasant_modifications > 0) {
        warning(STORE, "Warning! Origin directory modified while being mounted for backup!\n");
    }
    fs->umount(fuse_mount_);
    return RC::OK;
}

static int reverseGetattr(const char *path, struct stat *stbuf)
{
    Restore *fs = (Restore*)fuse_get_context()->private_data;
    return fs->getattrCB(path, stbuf);
}

static int reverseReaddir(const char *path, void *buf, fuse_fill_dir_t filler,
                          off_t offset, struct fuse_file_info *fi)
{
    Restore *fs = (Restore*)fuse_get_context()->private_data;
    return fs->readdirCB(path, buf, filler, offset, fi);
}

static int reverseRead(const char *path, char *buf, size_t size, off_t offset,
                       struct fuse_file_info *fi)
{
    Restore *fs = (Restore*)fuse_get_context()->private_data;
    return fs->readCB(path, buf, size, offset, fi);
}

static int reverseReadlink(const char *path, char *buf, size_t size)
{
    Restore *fs = (Restore*)fuse_get_context()->private_data;
    return fs->readlinkCB(path, buf, size);
}

RC BeakImplementation::mountRestoreDaemon(Settings *settings)
{
    return mountRestoreInternal(settings, true);
}

RC BeakImplementation::mountRestore(Settings *settings)
{
    return mountRestoreInternal(settings, false);
}

RC BeakImplementation::mountRestoreInternal(Settings *settings, bool daemon)
{
    memset(&restore_fuse_ops, 0, sizeof(restore_fuse_ops));
    restore_fuse_ops.getattr = reverseGetattr;
    restore_fuse_ops.open = open_callback;
    restore_fuse_ops.read = reverseRead;
    restore_fuse_ops.readdir = reverseReaddir;
    restore_fuse_ops.readlink = reverseReadlink;

    auto restore  = newRestore(origin_tool_->fs());

    RC rc = restore->lookForPointsInTime(PointInTimeFormat::absolute_point, settings->from.storage->storage_location);
    if (rc.isErr()) {
        error(COMMANDLINE, "No points in time found!\n");
    }

    if (settings->pointintime != "") {
        auto point = restore->setPointInTime(settings->pointintime);
        if (!point) return RC::ERR;
    }

    rc = restore->loadBeakFileSystem(settings);
    if (rc.isErr()) return RC::ERR;

    if (daemon) {
        int rc = fuse_main(settings->fuse_argc, settings->fuse_argv, &restore_fuse_ops,
                           restore.get()); // The reverse fs structure is passed as private data.
                                       // It is then extracted with
                                       // (Restore*)fuse_get_context()->private_data;
                                       // in the static fuse getters/setters.
        if (rc) return RC::ERR;
        return RC::OK;
    }

    struct fuse_args args;
    args.argc = settings->fuse_argc;
    args.argv = settings->fuse_argv;
    args.allocated = 0;
    struct fuse_chan *chan = fuse_mount(settings->to.dir->c_str(), &args);
    fuse_ = fuse_new(chan,
                     &args,
                     &restore_fuse_ops,
                     sizeof(restore_fuse_ops),
                     restore.get()); // Passed as private data to fuse context.

    loop_pid_ = fork();

    if (loop_pid_ == 0) {
        // This is the child process. Serve the virtual file system.
        fuse_loop_mt (fuse_);
        exit(0);
    }
    return RC::OK;
}

RC BeakImplementation::fetchPointsInTime(string remote, Path *cache)
{
    vector<char> out;
    vector<string> args;

    args.push_back("copy");
    args.push_back("--include");
    args.push_back("/z01*");
    args.push_back(remote);
    args.push_back(cache->str());
    UI::clearLine();
    UI::output("Copying index files from %s", remote.c_str());
    fflush(stdout);
    RC rc = sys_->invoke("rclone", args, &out);

    out.clear();
    args.clear();
    args.push_back("ls");
    args.push_back(remote);
    UI::clearLine();
    UI::output("Listing files in %s", remote.c_str());
    fflush(stdout);
    rc = sys_->invoke("rclone", args, &out);

    Path *p = cache;
    string r = remote;
    r.pop_back();
    p = p->appendName(Atom::lookup(r+".ls"));
    sys_fs_->createFile(p, &out);
    UI::clearLine();
    fflush(stdout);

    return rc;
}

RC BeakImplementation::status(Settings *settings)
{
    RC rc = RC::OK;

    for (auto rule : configuration_->sortedRules()) {
	UI::output("%-20s %s\n", rule->name.c_str(), rule->origin_path->c_str());
	{
	    vector<pair<Path*,struct timespec>> points;
	    rc = storage_tool_->listPointsInTime(rule->local, &points);
            /*
	    if (points.size() > 0) {
		string ago = timeAgo(&points.front());
		UI::output("%-20s %s\n", ago.c_str(), "local");
	    } else {
		UI::output("%-20s %s\n", "No backup!", "local");
                }*/
	}

	for (auto storage : rule->sortedStorages()) {
	    rc = fetchPointsInTime(storage->storage_location->str(), rule->cache_path);
	    if (rc.isErr()) continue;

	    vector<struct timespec> points;
	    //rc = findPointsInTime(storage->storage_location->str(), &points);
	    if (points.size() > 0) {
		string ago = timeAgo(&points.front());
		UI::output("%-20s %s\n", ago.c_str(), storage->storage_location->c_str());
	    } else {
		UI::output("%-20s %s\n", "No backup!", storage->storage_location->c_str());
	    }
	}
	UI::output("\n");
    }

    return rc;
}

RC BeakImplementation::store(Settings *settings)
{
    RC rc = RC::OK;

    assert(settings->from.type == ArgOrigin || settings->from.type == ArgRule);
    assert(settings->to.type == ArgStorage);

    ptr<FileSystem> ofs = origin_tool_->fs();
    // Watch the origin file system to detect if it is being changed while doing the store.
    ofs->enableWatch();

    auto backup  = newBackup(origin_tool_->fs());

    uint64_t start = clockGetTime();
    // This command scans the origin file system and builds
    // an in memory representation of the beak file system,
    // with tar files,index files and directories.
    rc = backup->scanFileSystem(settings);

    // Acquire a file system api to the virtual beak file system.
    auto beaked_fs = backup->asFileSystem();

    // The store statistics maintin the progress data for the store.
    auto st = newStoreStatistics();

    // Now store the beak file system into the selected storage.
    storage_tool_->storeFileSystemIntoStorage(beaked_fs,
                                              origin_tool_->fs().get(),
                                              settings->to.storage,
                                              st,
                                              backup,
                                              settings);

    uint64_t stop = clockGetTime();
    uint64_t store_time = stop - start;

    int unpleasant_modifications = ofs->endWatch();
    if (st->stats.num_files_stored == 0 && st->stats.num_dirs_updated == 0) {
        info(STORE, "No stores needed, everything was up to date.\n");
    } else {
        if (st->stats.num_files_stored > 0) {
            string file_sizes = humanReadable(st->stats.size_files_stored);
            double secs = ((double)((store_time)/1000))/1000.0;
            string seconds = humanReadableTwoDecimals(secs);
            double bytes = (double)st->stats.size_files_stored;
            double bps = bytes/secs;
            string average_speed = humanReadableTwoDecimals(bps);
        }
    }
    if (unpleasant_modifications > 0) {
        warning(STORE, "Warning! Origin directory modified while doing backup!\n");
    }

    return rc;
}

RC BeakImplementation::restore(Settings *settings)
{
    RC rc = RC::OK;

    if (settings->from.type != ArgStorage) {
        failure(COMMANDLINE,"You have to specify a backup directory that will be extracted.\n");
    }

    if (settings->to.type != ArgOrigin) {
        failure(COMMANDLINE,"You have to specify where to store the backup.\n");
    }

    umask(0);
    auto restore  = newRestore(origin_tool_->fs());
    auto view = restore->asFileSystem();

    restore->lookForPointsInTime(PointInTimeFormat::absolute_point, settings->from.storage->storage_location);

    auto point = restore->setPointInTime("@0");
    if (settings->pointintime != "") {
        point = restore->setPointInTime(settings->pointintime);
    }
    if (!point) {
        error(STORE, "No such point in time!\n");
    }
    assert(restore->singlePointInTime());

    uint64_t start = clockGetTime();
    rc = restore->loadBeakFileSystem(settings);
    if (rc.isErr()) rc = RC::ERR;

    auto st = newStoreStatistics();

    st->startDisplayOfProgress();

    view->recurse(Path::lookupRoot(),
                  [&restore,this,point,settings,&st]
                  (Path *path, FileStat *stat) {origin_tool_->addRestoreWork(st,path,stat,settings,
                                                                             restore.get(),point); });

    debug(STORE, "Work to be done: num_files=%ju num_hardlinks=%ju num_symlinks=%ju num_nodes=%ju num_dirs=%ju\n",
          st->stats.num_files, st->stats.num_hard_links, st->stats.num_symbolic_links,
          st->stats.num_nodes, st->stats.num_dirs);

    origin_tool_->restoreFileSystem(view,restore.get(),point,settings,st,storage_tool_->fs().get());

    uint64_t stop = clockGetTime();
    uint64_t store_time = stop - start;

    st->finishProgress();

    if (st->stats.num_files_stored == 0 && st->stats.num_symbolic_links_stored == 0 &&
        st->stats.num_dirs_updated == 0) {
        info(STORE, "No stores needed, everything was up to date.\n");
    } else {
        if (st->stats.num_files_stored > 0) {
            string file_sizes = humanReadable(st->stats.size_files_stored);
            info(STORE, "Stored %ju files for a total size of %s.\n", st->stats.num_files_stored, file_sizes.c_str());
        }
        if (st->stats.num_symbolic_links_stored > 0) {
            info(STORE, "Stored %ju symlinks.\n", st->stats.num_symbolic_links_stored);
        }
        if (st->stats.num_hard_links_stored > 0) {
            info(STORE, "Stored %ju hard links.\n", st->stats.num_hard_links_stored);
        }
        if (st->stats.num_dirs_updated > 0) {
            info(STORE, "Updated %ju dirs.\n", st->stats.num_dirs_updated);
        }
        info(STORE, "Time to store %jdms.\n", store_time / 1000);
    }
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

bool BeakImplementation::hasPointsInTime(Path *path, FileSystem *fs)
{
    if (path == NULL) return false;

    vector<Path*> contents;
    if (!fs->readdir(path, &contents)) {
        return false;
    }
    for (auto f : contents)
    {
        TarFileName tfn;
        bool ok = TarFile::parseFileName(f->str(), &tfn);

        if (ok && tfn.type == REG_FILE) {
            return true;
        }
    }

    return false;
}
