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

#include "log.h"
#include "beak.h"
#include <string.h>

#include <fuse/fuse.h>
#include <memory.h>
#include <limits.h>
#include <regex.h>
#include <stddef.h>
#include <string.h>
#include <sys/stat.h>
#include <cassert>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <ctime>
#include <map>
#include <string>
#include <utility>
#include <vector>

#include "forward.h"
#include "log.h"
#include "reverse.h"

ComponentId MAIN = registerLogComponent("main");
ComponentId COMMANDLINE = registerLogComponent("commandline");

struct CommandEntry;
struct OptionEntry;

struct BeakImplementation : Beak {

    BeakImplementation();
    void printCommands();
    void printOptions();
    
    void captureStartTime() {  ::captureStartTime(); }
    string argsToVector(int argc, char **argv, vector<string> *args);
    int parseCommandLine(vector<string> *args, Command *cmd, Options *settings);

    int printInfo(Options *settings); 

    bool lookForPointsInTime(Options *settings);
    vector<PointInTime> &history();
    bool setPointInTime(string g);
    
    int push(Options *settings);
    int mountForward(Options *settings);
    int mountReverse(Options *settings);
    int status(Options *settings);

    private:
    
    ForwardTarredFS forward_fs;
    fuse_operations forward_tarredfs_ops;
    ReverseTarredFS reverse_fs;
    fuse_operations reverse_tarredfs_ops;
    
    map<string,CommandEntry*> commands_;
    map<string,OptionEntry*> short_options_;
    map<string,OptionEntry*> long_options_;

    OptionEntry *nosuch_option_;

    vector<PointInTime> history_;
    
    Command parseCommand(string s);
    OptionEntry *parseOption(string s, bool *has_value, string *value);
};

Beak *newBeak() {
    return new BeakImplementation();
}

struct CommandEntry {
    const char *name;
    Command cmd;
    const char *info;
};

CommandEntry command_entries_[] = {
#define X(name,info) { #name, name##_cmd, info } ,
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

BeakImplementation::BeakImplementation() {
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

Command BeakImplementation::parseCommand(string s)
{
    CommandEntry *ce = commands_[s];
    if (!ce) return nosuch_cmd;
    return ce->cmd;
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

void BeakImplementation::printCommands() {
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

void BeakImplementation::printOptions() {
    fprintf(stdout, "Options:\n");

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

int BeakImplementation::parseCommandLine(vector<string> *args, Command *cmd, Options *settings)
{
    memset(settings, 0, sizeof(*settings)); // Note! Contents can be set to zero!
    settings->help_me_on_this_cmd = nosuch_cmd;
    settings->fuse_args.push_back("beak"); // Application name
    settings->depth = 2; // Default value
    settings->pointintimeformat = both_point;
    
    if (args->size() < 1) return false;
    
    *cmd = parseCommand((*args)[0]);

    if (*cmd == nosuch_cmd) {
        fprintf(stderr, "No such command \"%s\"\n", (*args)[0].c_str());
        return false;        
    }

    auto i = args->begin();
    i = args->erase(i);

    if ((*i) == "help") {
        // beak push help
        // To push a directory "help" do:
        //     beak push -- help
        settings->help_me_on_this_cmd = *cmd;
        *cmd = help_cmd;
        return true;
    }
    
    bool options_completed = false;
    for (;i != args->end(); ++i)
    {
        int l = i->length();
        if (!l) continue;

        if (*i == "--") {
            options_completed = true;
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
                    } else {
                        error(COMMANDLINE,"Option \"%s\" requires a value to be specified.\n", ope->name);
                    }
                }
            }
            switch (op) {
            case depth_option:
                settings->depth = atoi(value.c_str());
                break;
            case foreground_option:
                settings->fuse_args.push_back("-f");
                break;
            case fusedebug_option:
                settings->fuse_args.push_back("-d");
                break;
            case forceforward_option:
                settings->forceforward = true;
                break;
            case include_option:
                settings->include.push_back(value);
                break;
            case log_option:
                settings->log = value;
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
            case targetsize_option:
            {
                size_t parsed_size;
                int rc = parseHumanReadable(value.c_str(), &parsed_size);
                if (rc)
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
                int rc = parseHumanReadable(value.c_str(), &parsed_size);
                if (rc)
                {
                    error(COMMANDLINE,
                          "Cannot set trigger size because \"%s\" is not a proper number (e.g. 1,2K,3M,4G,5T)\n",
                          value.c_str());
                }
                settings->triggersize = parsed_size;
                settings->triggersize_supplied = true;                
            }
            break;
            case triggerregex_option:
                settings->triggerregex.push_back(value);
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

        if (options_completed) {
            if (settings->src == NULL)
            {
                string src = *i;
                string point = "";
                size_t at = src.find_last_of('@');
                if (at != string::npos) {
                    point = src.substr(at);
                    debug(COMMANDLINE, "Found point in time (%s) after src dir.\n", point);
                    if (settings->pointintime == "") {
                        src = src.substr(0,at);
                        settings->pointintime = point;
                    } else {
                        debug(COMMANDLINE, "Since -p was specified, assume the directory actually contains an @ sign!\n", point);
                    }
                }
                char tmp[PATH_MAX];
                const char *rc = realpath(src.c_str(), tmp);
                if (!rc)
                {
                    error(COMMANDLINE, "Could not find real path for %s\n", src.c_str());
                }
                assert(rc == tmp);
                settings->src = Path::lookup(tmp);
            }
            else if (settings->dst == NULL)
            {
                char tmp[PATH_MAX];
                const char *rc = realpath((*i).c_str(), tmp);
                if (!rc)
                {
                    error(COMMANDLINE,
                          "Could not find real path for \"%s\"\nDo you have an existing mount here?\n",
                          (*i).c_str());
                }
                settings->dst = Path::lookup(tmp);
            }
        }
    }

    if (*cmd == mount_cmd) {
        if (!settings->src) {
            error(COMMANDLINE,"You have to specify a src directory.\n");
        }
        if (!settings->dst) {
            error(COMMANDLINE,"You have to specify a dst directory.\n");
        }
        settings->fuse_args.push_back(settings->dst->c_str());
    }
    
    settings->fuse_argc = settings->fuse_args.size();
    settings->fuse_argv = new char*[settings->fuse_argc+1];
    int j = 0;
    for (auto &s : settings->fuse_args) {
        settings->fuse_argv[j] = (char*)s.c_str();
        j++;
    }
    settings->fuse_argv[j] = 0;
    return OK;
}

int BeakImplementation::printInfo(Options *settings)
{
    if (reverse_fs.history().size() == 0) {
        fprintf(stdout, "Not a beak archive.\n");
        return 1;
    } else 
    if (reverse_fs.history().size() == 1) {
        fprintf(stdout, "Single point in time found:\n");
    } else {
        fprintf(stdout, "Multiple points in time:\n");
    }
    for (auto s : reverse_fs.history()) {
        printf("@%d   %-15s %s\n", s.key, s.ago.c_str(), s.datetime.c_str());
    }
    printf("\n");
    return 0;
}

bool BeakImplementation::lookForPointsInTime(Options *settings)
{
    return reverse_fs.lookForPointsInTime(settings->pointintimeformat, settings->src);
}

vector<PointInTime> &BeakImplementation::history()
{
    return reverse_fs.history();
}

bool BeakImplementation::setPointInTime(string p)
{
    return reverse_fs.setPointInTime(p);
}

int BeakImplementation::push(Options *settings)
{
    return 0;
}

static int forwardGetattr(const char *path, struct stat *stbuf)
{
    ForwardTarredFS *fs = (ForwardTarredFS*)fuse_get_context()->private_data;    
    return fs->getattrCB(path, stbuf);
}

static int forwardReaddir(const char *path, void *buf, fuse_fill_dir_t filler,
                          off_t offset, struct fuse_file_info *fi)
{
    ForwardTarredFS *fs = (ForwardTarredFS*)fuse_get_context()->private_data;    
    return fs->readdirCB(path, buf, filler, offset, fi);    
}

static int forwardRead(const char *path, char *buf, size_t size, off_t offset,
                       struct fuse_file_info *fi)
{
    ForwardTarredFS *fs = (ForwardTarredFS*)fuse_get_context()->private_data;    
    return fs->readCB(path, buf, size, offset, fi);
}

static int open_callback(const char *path, struct fuse_file_info *fi)
{
    return 0;
}

int BeakImplementation::mountForward(Options *settings)
{
    forward_tarredfs_ops.getattr = forwardGetattr;
    forward_tarredfs_ops.open = open_callback;
    forward_tarredfs_ops.read = forwardRead;
    forward_tarredfs_ops.readdir = forwardReaddir;
    
    forward_fs.root_dir_path = settings->src;
    forward_fs.root_dir = settings->src->str();
    forward_fs.mount_dir_path = settings->dst;
    forward_fs.mount_dir = settings->dst->str();

    for (auto &e : settings->include) {
        regex_t re;
        if (regcomp(&re, e.c_str(), REG_EXTENDED | REG_NOSUB) != 0)
        {
            error(COMMANDLINE, "Not a valid regexp \"%s\"\n", e.c_str());
        }
        forward_fs.filters.push_back(pair<Filter, regex_t>(Filter(e.c_str(), INCLUDE), re));
        debug(COMMANDLINE, "Includes \"%s\"\n", e.c_str());
    }
    for (auto &e : settings->exclude) {
        regex_t re;
        if (regcomp(&re, e.c_str(), REG_EXTENDED | REG_NOSUB) != 0)
        {
            error(COMMANDLINE, "Not a valid regexp \"%s\"\n", e.c_str());
        }
        forward_fs.filters.push_back(pair<Filter, regex_t>(Filter(e.c_str(), EXCLUDE), re));
        debug(COMMANDLINE, "Includes \"%s\"\n", e.c_str());
    }

    forward_fs.forced_tar_collection_dir_depth = settings->depth;

    if (settings->log.length() > 0) {
        setLogComponents(settings->log.c_str());
        setLogLevel(DEBUG);
    }

    if (!settings->targetsize_supplied) {
        // Default settings
        forward_fs.target_target_tar_size = 10*1024*1024;
    } else {
        forward_fs.target_target_tar_size = settings->targetsize;
    }        
    if (!settings->triggersize_supplied) {
        forward_fs.tar_trigger_size = forward_fs.target_target_tar_size * 2;
    } else {
        forward_fs.tar_trigger_size = settings->triggersize;
    }

    for (auto &e : settings->triggerregex) {
        regex_t re;
        if (regcomp(&re, e.c_str(), REG_EXTENDED | REG_NOSUB) != 0)
        {
            fprintf(stderr, "Not a valid regexp \"%s\"\n", e.c_str());
        }
        forward_fs.triggers.push_back(re);
        debug(COMMANDLINE, "Triggers on \"%s\"\n", e.c_str());
    }
    
    debug(COMMANDLINE, "main",
          "Target tar size \"%zu\"\nTarget trigger size %zu\n",
          forward_fs.target_target_tar_size,
          forward_fs.tar_trigger_size);
    
    info(MAIN, "Scanning %s\n", forward_fs.root_dir.c_str());
    uint64_t start = clockGetTime();
    forward_fs.recurse();
    uint64_t stop = clockGetTime();
    uint64_t scan_time = stop - start;
    start = stop;

    // Find suitable directories points where virtual tars will be created.
    forward_fs.findTarCollectionDirs();
    // Remove all other directories that will be hidden inside tars.
    forward_fs.pruneDirectories();
    // Add remaining dirs as dir entries to their parent directories.
    forward_fs.addDirsToDirectories();
    // Add content (files and directories) to the tar collection dirs.
    forward_fs.addEntriesToTarCollectionDirs();
    // Calculate the tarpaths and fix/move the hardlinks.
    forward_fs.fixTarPathsAndHardLinks();
    // Group the entries into tar files.
    size_t num_tars = forward_fs.groupFilesIntoTars();
    // Sort the entries in a tar friendly order.
    forward_fs.sortTarCollectionEntries();

    stop = clockGetTime();
    uint64_t group_time = stop - start;

    info(MAIN, "Mounted %s with %zu virtual tars with %zu entries.\n"
            "Time to scan %jdms, time to group %jdms.\n",
            forward_fs.mount_dir.c_str(), num_tars, forward_fs.files.size(),
            scan_time / 1000, group_time / 1000);

    return fuse_main(settings->fuse_argc, settings->fuse_argv, &forward_tarredfs_ops, &forward_fs);
}

static int reverseGetattr(const char *path, struct stat *stbuf)
{
    ReverseTarredFS *fs = (ReverseTarredFS*)fuse_get_context()->private_data;    
    return fs->getattrCB(path, stbuf);
}

static int reverseReaddir(const char *path, void *buf, fuse_fill_dir_t filler,
                          off_t offset, struct fuse_file_info *fi)
{
    ReverseTarredFS *fs = (ReverseTarredFS*)fuse_get_context()->private_data;    
    return fs->readdirCB(path, buf, filler, offset, fi);    
}

static int reverseRead(const char *path, char *buf, size_t size, off_t offset,
                       struct fuse_file_info *fi)
{
    ReverseTarredFS *fs = (ReverseTarredFS*)fuse_get_context()->private_data;    
    return fs->readCB(path, buf, size, offset, fi);
}

static int reverseReadlink(const char *path, char *buf, size_t size)
{
    ReverseTarredFS *fs = (ReverseTarredFS*)fuse_get_context()->private_data;
    return fs->readlinkCB(path, buf, size);
}

int BeakImplementation::mountReverse(Options *settings)
{
    reverse_tarredfs_ops.getattr = reverseGetattr;
    reverse_tarredfs_ops.open = open_callback;
    reverse_tarredfs_ops.read = reverseRead;
    reverse_tarredfs_ops.readdir = reverseReaddir;
    reverse_tarredfs_ops.readlink = reverseReadlink;

    reverse_fs.setRootDir(settings->src);
    reverse_fs.setMountDir(settings->dst);
    
    if (settings->log.length() > 0) {
        setLogComponents(settings->log.c_str());
        setLogLevel(DEBUG);
    }

    if (settings->pointintime != "") {
        int rc = setPointInTime(settings->pointintime);
        if (!rc) return ERR;
    }
    for (auto &point : reverse_fs.history()) {
        string name = point.filename;
        debug(MAIN,"Found backup for %s\n", point.ago.c_str());

        // Check that it is a proper file.
        struct stat sb;
        Path *gz = Path::lookup(reverse_fs.rootDir()->str() + "/" + name);

        int rc = stat(gz->c_str(), &sb);
        if (rc || !S_ISREG(sb.st_mode))
        {
            error(MAIN, "Not a regular file %s\n", gz->c_str());
        }

        // Populate the list of all tars from the root x01 gz file.
        bool ok = reverse_fs.loadGz(&point, gz, Path::lookupRoot());
        if (!ok) {
            error(MAIN, "Fatal error, could not load root x01 file for backup %s!\n", point.ago.c_str());
        }

        // Populate the root directory with its contents.
        reverse_fs.loadCache(&point, Path::lookupRoot());

        Entry *e = reverse_fs.findEntry(&point, Path::lookupRoot());
        assert(e != NULL);

        // Look for the youngest timestamp inside root to
        // be used as the timestamp for the root directory.
        // The root directory is by definition not defined inside gz file.
        time_t youngest_secs = 0, youngest_nanos = 0;        
        for (auto i : e->dir)
        {
            if (i->msecs > youngest_secs || (i->msecs == youngest_secs && i->mnanos > youngest_nanos))
            {
                youngest_secs = i->msecs;
                youngest_nanos = i->mnanos;
            }
        }
        e->msecs = youngest_secs;
        e->mnanos = youngest_nanos;
    }
    return fuse_main(settings->fuse_argc, settings->fuse_argv, &reverse_tarredfs_ops, &reverse_fs);
}

int BeakImplementation::status(Options *settings)
{
    int rc = 0;


    return rc;
}

