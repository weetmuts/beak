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

#include "configuration.h"
#include "log.h"
#include "filesystem.h"
#include "forward.h"
#include "reverse.h"
#include "system.h"
#include "tarfile.h"
#include "ui.h"

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

static ComponentId MAIN = registerLogComponent("main");
static ComponentId COMMANDLINE = registerLogComponent("commandline");

struct CommandEntry;
struct OptionEntry;

struct BeakImplementation : Beak {

    BeakImplementation(FileSystem *fs);
    void printCommands();
    void printOptions();

    void captureStartTime() {  ::captureStartTime(); }
    string argsToVector(int argc, char **argv, vector<string> *args);
    int parseCommandLine(int argc, char **argv, Command *cmd, Options *settings);

    void printHelp(Command cmd);
    void printVersion();
    void printLicense();
    int printInfo(Options *settings);

    bool lookForPointsInTime(Options *settings);
    vector<PointInTime> &history();
    bool setPointInTime(string g);
    RC findPointsInTime(string path, vector<struct timespec> *v);
    RC fetchPointsInTime(string remote, string local);

    int configure(Options *settings);
    int push(Options *settings);

    int umountDaemon(Options *settings);

    int mountForwardDaemon(Options *settings);
    int mountForward(Options *settings);
    int umountForward(Options *settings);

    int mountReverseDaemon(Options *settings);
    int mountReverse(Options *settings);
    int umountReverse(Options *settings);

    int shell(Options *settings);
    int status(Options *settings);
    int storeForward(Options *settings);
    int storeReverse(Options *settings);

    void genAutoComplete(string filename);

    private:

    int mountForwardInternal(Options *settings, bool daemon);
    int mountReverseInternal(Options *settings, bool daemon);

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
    unique_ptr<Configuration> configuration_;

    struct fuse_chan *chan_;
    struct fuse *fuse_;
    pid_t loop_pid_;

    unique_ptr<System> sys_;
    FileSystem *file_system_;
};

unique_ptr<Beak> newBeak(FileSystem *fs) {
    return unique_ptr<Beak>(new BeakImplementation(fs));
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

BeakImplementation::BeakImplementation(FileSystem *fs) :
    forward_fs(fs),
    reverse_fs(fs),
    file_system_(fs)
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

    configuration_ = newConfiguration(fs);
    configuration_->load();

    sys_ = newSystem();
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

int BeakImplementation::parseCommandLine(int argc, char **argv, Command *cmd, Options *settings)
{
    vector<string> args;
    argsToVector(argc, argv, &args);

    memset(settings, 0, sizeof(*settings)); // Note! Contents can be set to zero!
    settings->help_me_on_this_cmd = nosuch_cmd;
    settings->fuse_args.push_back("beak"); // Application name
    // settings->fuse_args.push_back("-o"); // Application name
    // settings->fuse_args.push_back("nonempty"); // Application name
    settings->depth = 2; // Default value
    settings->pointintimeformat = both_point;

    if (args.size() < 1) return false;

    *cmd = parseCommand(args[0]);

    if (*cmd == nosuch_cmd) {
        if (args[0] == "") {
            *cmd = help_cmd;
            return false;
        }
        if (args[0] == "") {
            *cmd = help_cmd;
            return false;
        }
        fprintf(stderr, "No such command \"%s\"\n", args[0].c_str());
        return false;
    }

    auto i = args.begin();
    i = args.erase(i);

    if ((*i) == "help") {
        // beak push help
        // To push a directory "help" do:
        //     beak push -- help
        settings->help_me_on_this_cmd = *cmd;
        *cmd = help_cmd;
        return true;
    }

    bool options_completed = false;
    for (;i != args.end(); ++i)
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
            case forceforward_option:
                settings->forceforward = true;
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

                Rule *rule = NULL;
		if (src.back() == ':') {
		    src.pop_back();
		    rule = configuration_->rule(src);
		}
                if (rule) {
                    debug(COMMANDLINE, "Translating %s to %s\n", src.c_str(), rule->path.c_str());
                    src = rule->path;
                }
                if (src.find(':') != string::npos) {
                    // Assume this is an rclone remote target.
                    settings->remote = src;
                    settings->src = NULL;
                } else {
                    Path *rp = Path::lookup(src)->realpath();
                    if (!rp)
                    {
                        error(COMMANDLINE, "Could not find real path for %s\n", src.c_str());
                    }
                    settings->src = rp;
                }
            }
            else if (settings->dst == NULL)
            {
                string dst = *i;
                Path *rp = Path::lookup(dst)->realpath();
                if (rp) {
                    // Path exists.
                    settings->dst = rp;
                }
                else
                {
                    // Path does not exist...
                    if (dst.find(':') != string::npos) {
                        // Assume this is an rclone remote target.
                        settings->remote = dst;
                        settings->dst = NULL;
                    } else {
                        error(COMMANDLINE,
                              "Could not find real path for \"%s\"\nDo you have an existing mount here?\n",
                              (*i).c_str());
                    }
                }
            }
        }
    }

    if (*cmd == mount_cmd || *cmd == push_cmd) {
        if (!settings->src) {
            error(COMMANDLINE,"You have to specify a src directory.\n");
        }
    }
    if (*cmd == mount_cmd) {
        if (!settings->dst) {
            error(COMMANDLINE,"You have to specify a dst directory.\n");
        }
        settings->fuse_args.push_back(settings->dst->c_str());
    }
    if (*cmd == push_cmd) {
        if (!settings->dst && settings->remote == "") {
            error(COMMANDLINE,"You have to specify a remote or a dst directory.\n");
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

int BeakImplementation::configure(Options *settings)
{
    return configuration_->configure();
}

int BeakImplementation::push(Options *settings)
{
    Path *mount = file_system_->mkTempDir("beak_push_");
    Options forward_settings = *settings;
    forward_settings.dst = mount;
    forward_settings.fuse_argc = 1;
    char *arg;
    char **argv = &arg;
    argv[0] = new char[16];
    strcpy(*argv, "beak");
    forward_settings.fuse_argv = argv;

    // Spawn virtual filesystem.
    int rc = mountForward(&forward_settings);

    vector<string> args;
    args.push_back("copy");
    args.push_back("-v");
    args.push_back(mount->c_str());
    if (settings->dst) {
        args.push_back(settings->dst->str());
    } else {
        args.push_back(settings->remote);
    }
    rc = sys_->invoke("rclone", args, NULL);

    // Unmount virtual filesystem.
    rc = umountForward(&forward_settings);
    rmdir(mount->c_str());

    return rc;
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

int BeakImplementation::umountDaemon(Options *settings)
{
    vector<string> args;
    args.push_back("-u");
    args.push_back(settings->src->str());
    return sys_->invoke("fusermount", args, NULL);
}

int BeakImplementation::mountForwardDaemon(Options *settings)
{
    return mountForwardInternal(settings, true);
}

int BeakImplementation::mountForward(Options *settings)
{
    return mountForwardInternal(settings, false);
}

int BeakImplementation::umountForward(Options *settings)
{
    fuse_exit(fuse_);
    fuse_unmount (settings->dst->c_str(), chan_);
    return 0;
}

int BeakImplementation::mountForwardInternal(Options *settings, bool daemon)
{
    bool ok;

    forward_tarredfs_ops.getattr = forwardGetattr;
    forward_tarredfs_ops.open = open_callback;
    forward_tarredfs_ops.read = forwardRead;
    forward_tarredfs_ops.readdir = forwardReaddir;

    ok = forward_fs.scanFileSystem(settings);

    if (ok) {
        return ERR;
    }

    if (daemon) {
        return fuse_main(settings->fuse_argc, settings->fuse_argv, &forward_tarredfs_ops, &forward_fs);
    }

    struct fuse_args args;
    args.argc = settings->fuse_argc;
    args.argv = settings->fuse_argv;
    args.allocated = 0;
    struct fuse_chan *chan = fuse_mount(settings->dst->c_str(), &args);
    fuse_ = fuse_new(chan,
                     &args,
                     &forward_tarredfs_ops,
                     sizeof(forward_tarredfs_ops),
                     &forward_fs);

    loop_pid_ = fork();

    if (loop_pid_ == 0) {
        // This is the child process. Serve the virtual file system.
        fuse_loop_mt (fuse_);
        exit(0);
    }
    return 0;
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

int BeakImplementation::mountReverseDaemon(Options *settings)
{
    return mountReverseInternal(settings, true);
}

int BeakImplementation::mountReverse(Options *settings)
{
    return mountReverseInternal(settings, false);
}

int BeakImplementation::umountReverse(Options *settings)
{
    fuse_exit(fuse_);
    fuse_unmount(settings->dst->c_str(), chan_);
    return 0;
}

int BeakImplementation::mountReverseInternal(Options *settings, bool daemon)
{
    reverse_tarredfs_ops.getattr = reverseGetattr;
    reverse_tarredfs_ops.open = open_callback;
    reverse_tarredfs_ops.read = reverseRead;
    reverse_tarredfs_ops.readdir = reverseReaddir;
    reverse_tarredfs_ops.readlink = reverseReadlink;

    if (settings->pointintime != "") {
        int rc = setPointInTime(settings->pointintime);
        if (!rc) return ERR;
    }

    RC rc = reverse_fs.scanFileSystem(settings);
    if (rc != OK) return ERR;

    if (daemon) {
        return fuse_main(settings->fuse_argc, settings->fuse_argv, &reverse_tarredfs_ops,
                         &reverse_fs); // The reverse fs structure is passed as private data.
                                       // It is then extracted with
                                       // (ReverseTarredFS*)fuse_get_context()->private_data;
                                       // in the static fuse getters/setters.
    }

    struct fuse_args args;
    args.argc = settings->fuse_argc;
    args.argv = settings->fuse_argv;
    args.allocated = 0;
    struct fuse_chan *chan = fuse_mount(settings->dst->c_str(), &args);
    fuse_ = fuse_new(chan,
                     &args,
                     &reverse_tarredfs_ops,
                     sizeof(reverse_tarredfs_ops),
                     &reverse_fs); // Passed as private data to fuse context.

    loop_pid_ = fork();

    if (loop_pid_ == 0) {
        // This is the child process. Serve the virtual file system.
        fuse_loop_mt (fuse_);
        exit(0);
    }
    return 0;
}

int BeakImplementation::shell(Options *settings)
{
    int rc = 0;

    vector<char> out;
    vector<string> args;
    args.push_back("ls");
    args.push_back(settings->remote);
    rc = sys_->invoke("rclone", args, &out);

    auto i = out.begin();
    bool eof, err;

    for (;;) {
        eatWhitespace(out, i, &eof);
        if (eof) break;
        string size = eatTo(out, i, ' ', 64, &eof, &err);
        if (eof || err) break;
        string name = eatTo(out, i, '\n', 4096, &eof, &err);
        if (err) break;
    }
    return rc;
}

// Copy the remote index files to the local storage.
RC BeakImplementation::fetchPointsInTime(string remote, string cache)
{
    RC rc = OK;
    vector<char> out;
    vector<string> args;
    args.push_back("copy");
    args.push_back("--include");
    args.push_back("/z01*");
    args.push_back(remote);
    args.push_back(cache);
    UI::clearLine();
    UI::output("Copying index files from %s", remote.c_str());
    fflush(stdout);
    rc = sys_->invoke("rclone", args, &out);

    out.clear();
    args.clear();
    args.push_back("ls");
    args.push_back(remote);
    UI::clearLine();
    UI::output("Listing files in %s", remote.c_str());
    fflush(stdout);
    rc = sys_->invoke("rclone", args, &out);
    Path *p = Path::lookup(cache);
    string r = remote;
    r.pop_back();
    p = p->appendName(Atom::lookup(r+".ls"));
    writeVector(&out, p);
    UI::clearLine();
    fflush(stdout);

    return rc;
}

// List the remote index files.
RC BeakImplementation::findPointsInTime(string path, vector<struct timespec> *v)
{
    RC rc = OK;
    vector<char> out;
    vector<string> args;
    args.push_back("ls");
    args.push_back("--include");
    args.push_back("/z01*");
    args.push_back(path);
    rc = sys_->invoke("rclone", args, &out);

    if (rc != OK) return ERR;

    auto i = out.begin();
    bool eof, err;

    for (;;) {
	// Example line:
	// 12288 z01_001506595429.268937346_0_7eb62d8e0097d5eaa99f332536236e6ba9dbfeccf0df715ec96363f8ddd495b6_0.gz
        eatWhitespace(out, i, &eof);
        if (eof) break;
        string size = eatTo(out, i, ' ', 64, &eof, &err);
        if (eof || err) break;
        eatTo(out, i, '_', 64, &eof, &err);
        if (eof || err) break;
        string secs = eatTo(out, i, '.', 64, &eof, &err);
        if (eof || err) break;
        string nanos = eatTo(out, i, '_', 64, &eof, &err);
        if (eof || err) break;
        eatTo(out, i, '\n', 4096, &eof, &err);
        if (err) break;
	struct timespec ts;
	ts.tv_sec = atol(secs.c_str());
	ts.tv_nsec = atoi(nanos.c_str());
	v->push_back(ts);
    }

    if (err) return ERR;

    sort(v->begin(), v->end(),
	      [](struct timespec &a, struct timespec &b)->bool {
		  return (b.tv_sec < a.tv_sec) ||
		      (b.tv_sec == a.tv_sec &&
		       b.tv_nsec < a.tv_nsec);
	      });

    return OK;
}

int BeakImplementation::status(Options *settings)
{
    RC rc = OK;

    for (auto rule : configuration_->sortedRules()) {
	UI::output("%-20s %s\n", rule->name.c_str(), rule->path.c_str());

	{
	    vector<struct timespec> points;
	    rc = findPointsInTime(rule->local.target, &points);
	    if (points.size() > 0) {
		string ago = timeAgo(&points.front());
		UI::output("%-20s %s\n", ago.c_str(), "local");
	    } else {
		UI::output("%-20s %s\n", "No backup!", "local");
	    }
	}

	for (auto remote : rule->sortedRemotes()) {
	    rc = fetchPointsInTime(remote->target, rule->cache_path);
	    if (rc != OK) continue;

	    vector<struct timespec> points;
	    rc = findPointsInTime(remote->target, &points);
	    if (points.size() > 0) {
		string ago = timeAgo(&points.front());
		UI::output("%-20s %s\n", ago.c_str(), remote->target.c_str());
	    } else {
		UI::output("%-20s %s\n", "No backup!", remote->target.c_str());
	    }
	}
	UI::output("\n");
    }

    return rc;
}

int BeakImplementation::storeForward(Options *settings)
{
    int rc = 0;

    auto ffs  = newForwardTarredFS(file_system_);
//    auto view = ffs->asFileSystem();

    rc = ffs->scanFileSystem(settings);

    for (auto& e : ffs->tar_storage_directories) {
        Path *dir = e.second->path()->prepend(settings->dst);
        dir->mkdir();
        for (auto& f : e.second->tars()) {
            Path *tar = f->path()->prepend(settings->dst);
            f->writeToFile(tar, file_system_);
        }
    }
    return rc;
}

int BeakImplementation::storeReverse(Options *settings)
{
    RC rc = OK;

    auto rfs  = newReverseTarredFS(file_system_);
    auto view = rfs->asFileSystem();

    rfs->lookForPointsInTime(PointInTimeFormat::absolute_point, settings->src);

    rfs->setPointInTime("@0");
    assert(rfs->singlePointInTime());

    rc = rfs->scanFileSystem(settings);

    view->recurse([] (Path *path, FileStat *stat) {
            string permissions = permissionString(stat);
            printf("%s %s %ju\n", path->c_str(), permissions.c_str(), stat->st_size);
        });
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
                "  beak [command] [options] [src] [dst]\n"
                "\n");
        printCommands();
        fprintf(stdout,"\n");
        printOptions();
        fprintf(stdout,"\n");
        fprintf(stdout,"Beak is licensed to you under the GPLv3. For details do: "
                "beak help --license\n");
        break;
    case mount_cmd:
        fprintf(stdout,
        "Examines the contents in src to determine what kind of virtual filesystem\n"
        "to mount as dst. If src contains your files that you want to backup,\n"
        "then dst will contain beak backup tar files, suitable for rcloning somewhere.\n"
        "If src contains beak backup tar files, then dst will contain your original\n"
        "backed up files.\n"
        "\n"
        "Usage:\n"
            "  beak mount [options] [src] [dst]\n"
            "\n"
        "If you really want to backup the beak backup tar files, then you can override\n"
            "the automatic detection with the --forceforward option.\n");
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
            " by Fredrik Öhrström Copyright (C) 2016-2017\n\n"
            "Includes third party libraries:\n"
            " openssl-1.0.2l\n"
            " zlib-1.2.11\n"
            #ifdef PLATFORM_WINAPI
            " winsfp\n"
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
