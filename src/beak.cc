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

struct CommandEntry;
struct OptionEntry;

struct BeakImplementation : Beak {

    BeakImplementation(FileSystem *src_fs, FileSystem *dst_fs);
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
    RC fetchPointsInTime(string remote, Path *cache);

    int configure(Options *settings);
    int push(Options *settings);
    int prune(Options *settings);

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
    FileSystem *src_file_system_;
    FileSystem *dst_file_system_;
};

unique_ptr<Beak> newBeak(FileSystem *src_fs, FileSystem *dst_fs) {
    return unique_ptr<Beak>(new BeakImplementation(src_fs, dst_fs));
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

BeakImplementation::BeakImplementation(FileSystem *src_fs, FileSystem *dst_fs) :
    forward_fs(src_fs),
    reverse_fs(src_fs),
    src_file_system_(src_fs),
    dst_file_system_(dst_fs)
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

    sys_ = newSystem();

    configuration_ = newConfiguration(sys_.get(), src_fs);
    configuration_->load();
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

		if (src.back() == ':') {
		    src.pop_back();
		    settings->rule = configuration_->rule(src);
		}
                if (settings->rule) {
                    debug(COMMANDLINE, "Translating %s to %s\n", src.c_str(), settings->rule->path->c_str());
                    src = settings->rule->path->str();
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

    if (*cmd == mount_cmd || *cmd == store_cmd) {
        if (!settings->src) {
            failure(COMMANDLINE,"You have to specify a src directory.\n");
            return ERR;
        }
    }
    if (*cmd == mount_cmd) {
        if (!settings->dst) {
            failure(COMMANDLINE,"You have to specify a dst directory.\n");
            return ERR;
        }
        settings->fuse_args.push_back(settings->dst->c_str());
    }
    if (*cmd == push_cmd) {
        if (!settings->rule) {
            failure(COMMANDLINE,"When pushing you have to specify a rule.\n");
            return ERR;
        }
        if (!settings->dst && settings->remote == "") {
            failure(COMMANDLINE,"When pushing you have to specify a remote or a directory.\n");
            return ERR;
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
    assert(settings->rule);
    Path *mount = src_file_system_->mkTempDir("beak_push_");
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
    vector<char> output;
    fprintf(stderr, "LENGTH OF GURKA1 %ju\n", output.size());
    rc = sys_->invoke("rclone", args, &output, CaptureBoth,
                      [=](vector<char>::iterator i) { fprintf(stderr, "RCLONE COPY:\n");
                                                      while (i!=output.end()) { fprintf(stderr, "%c", *i); i++; } fprintf(stderr, "\n\n"); });
    fprintf(stderr, "LENGTH OF GURKA2 %ju\n", output.size());
    // Parse verbose output and look for:
    // 2018/01/29 20:05:36 INFO  : code/src/s01_001517180913.689221661_11659264_b6f526ca4e988180fe6289213a338ab5a4926f7189dfb9dddff5a30ab50fc7f3_0.tar: Copied (new)

    // Unmount virtual filesystem.
    rc = umountForward(&forward_settings);
    rmdir(mount->c_str());

    return rc;
}

int BeakImplementation::prune(Options *settings)
{
    RC rc = OK;


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
    return sys_->invoke("fusermount", args);
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

    RC rc = reverse_fs.loadBeakFileSystem(settings);
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
RC BeakImplementation::fetchPointsInTime(string remote, Path *cache)
{
    RC rc = OK;
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
    rc = sys_->invoke("rclone", args, &out);

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
    dst_file_system_->createFile(p, &out);
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
	UI::output("%-20s %s\n", rule->name.c_str(), rule->path->c_str());
	{
	    vector<struct timespec> points;
	    rc = findPointsInTime(rule->local->target->str(), &points);
	    if (points.size() > 0) {
		string ago = timeAgo(&points.front());
		UI::output("%-20s %s\n", ago.c_str(), "local");
	    } else {
		UI::output("%-20s %s\n", "No backup!", "local");
	    }
	}

	for (auto storage : rule->sortedStorages()) {
	    rc = fetchPointsInTime(storage->target->str(), rule->cache_path);
	    if (rc != OK) continue;

	    vector<struct timespec> points;
	    rc = findPointsInTime(storage->target->str(), &points);
	    if (points.size() > 0) {
		string ago = timeAgo(&points.front());
		UI::output("%-20s %s\n", ago.c_str(), storage->target->c_str());
	    } else {
		UI::output("%-20s %s\n", "No backup!", storage->target->c_str());
	    }
	}
	UI::output("\n");
    }

    return rc;
}

struct StoreStatistics
{
    size_t num_files, size_files, num_dirs;
    size_t num_hard_links, num_symbolic_links, num_nodes;

    size_t num_files_to_store, size_files_to_store;
    size_t num_files_stored, size_files_stored;
    size_t num_dirs_updated;

    size_t num_files_handled, size_files_handled;
    size_t num_dirs_handled;

    size_t num_hard_links_stored;
    size_t num_symbolic_links_stored;
    size_t num_device_nodes_stored;

    size_t num_total, num_total_handled;

    uint64_t prev, start;

    StoreStatistics() {
        memset(this, 0, sizeof(StoreStatistics));
        start = prev = clockGetTime();
    }
    //Tar emot objekt: 100% (814178/814178), 669.29 MiB | 6.71 MiB/s, klart.
    //Analyserar delta: 100% (690618/690618), klart.

    void displayProgress() {
        if (num_files == 0 || num_files_to_store == 0) return;
        uint64_t now = clockGetTime();
        if ((now-prev) < 500000) return;
        prev = now;
        UI::clearLine();
        int percentage = (int)(100.0*(float)size_files_stored / (float)size_files_to_store);
        string mibs = humanReadableTwoDecimals(size_files_stored);
        float secs = ((float)((now-start)/1000))/1000.0;
        string speed = humanReadableTwoDecimals(((double)size_files_stored)/secs);
        if (num_files > num_files_to_store) {
            UI::output("Incremental store: %d%% (%ju/%ju), %s | %.2f s %s/s ",
                       percentage, num_files_stored, num_files_to_store, mibs.c_str(), secs, speed.c_str());
        } else {
            UI::output("Full store: %d%% (%ju/%ju), %s | %.2f s %s/s ",
                       percentage, num_files_stored, num_files_to_store, mibs.c_str(), secs, speed.c_str());
        }
    }

    void finishProgress() {
        if (num_files == 0 || num_files_to_store == 0) return;
        displayProgress();
        UI::output(", done.\n");
    }

};

void calculateForwardWork(Path *path, FileStat *stat,
                          ForwardTarredFS *rfs, Beak *beak,
                          Options *settings, StoreStatistics *st,
                          FileSystem *src_fs, FileSystem *dst_fs)
{
    auto file_to_extract = path->prepend(settings->dst);

    if (stat->isRegularFile()) {
        stat->checkStat(dst_fs, file_to_extract);
        if (stat->disk_update == Store) {
            st->num_files_to_store++; st->size_files_to_store+=stat->st_size;
        }
        st->num_files++; st->size_files+=stat->st_size;
    }
    else if (stat->isDirectory()) st->num_dirs++;
}

void handleFile(Path *path, FileStat *stat,
                ForwardTarredFS *rfs, Beak *beak,
                Options *settings, StoreStatistics *st,
                FileSystem *src_fs, FileSystem *dst_fs)
{
    if (!stat->isRegularFile()) return;

    debug(STORE, "PATH %s\n", path->c_str());
    TarFile *tar = rfs->findTarFromPath(path);
    assert(tar);
    Path *file_name = tar->path()->prepend(settings->dst);
    dst_fs->mkDirp(file_name->parent());
    FileStat old_stat;
    if (OK == dst_fs->stat(file_name, &old_stat) &&
        stat->samePermissions(&old_stat) &&
        stat->sameSize(&old_stat) &&
        stat->sameMTime(&old_stat)) {

        debug(STORE, "Skipping %s\n", file_name->c_str());
    } else {
        tar->createFile(file_name, stat, src_fs, dst_fs);
        dst_fs->utime(file_name, stat);
        st->num_files_stored++;
        st->size_files_stored += stat->st_size;

        verbose(STORE, "Stored %s\n", file_name->c_str());
    }
    st->num_files_handled++;
    st->size_files_handled += stat->st_size;
    st->displayProgress();
}

int BeakImplementation::storeForward(Options *settings)
{
    int rc = 0;

    auto ffs  = newForwardTarredFS(src_file_system_);
    auto view = ffs->asFileSystem();

    uint64_t start = clockGetTime();
    rc = ffs->scanFileSystem(settings);

    StoreStatistics st;

    view->recurse([&ffs,this,settings,&st]
                  (Path *path, FileStat *stat) {calculateForwardWork(path,stat,ffs.get(),this,settings,&st,
                                                                     src_file_system_, dst_file_system_); });

    debug(STORE, "Work to be done: num_files=%ju num_dirs=%ju\n", st.num_files, st.num_dirs);

    view->recurse([&ffs,this,settings,&st]
                  (Path *path, FileStat *stat) {handleFile(path,stat,ffs.get(),this,settings,&st,
                                                           src_file_system_, dst_file_system_); });
    uint64_t stop = clockGetTime();
    uint64_t store_time = stop - start;

    st.finishProgress();

    if (st.num_files_stored == 0 && st.num_dirs_updated == 0) {
        info(STORE, "No stores needed, everything was up to date.\n");
    } else {
        if (st.num_files_stored > 0) {
            string file_sizes = humanReadable(st.size_files_stored);

            info(STORE, "Stored %ju beak files for a total size of %s.\n"
                 "Time to store %jdms.\n",
                 st.num_files_stored, file_sizes.c_str(),
                 store_time / 1000);
        }
    }

    return rc;
}

bool extractHardLink(FileSystem *src, Path *target,
                     FileSystem *dst, Path *dst_root, Path *file_to_extract, FileStat *stat,
                     StoreStatistics *statistics)
{
    target = target->prepend(dst_root);
    FileStat target_stat;
    if (OK != dst->stat(target, &target_stat)) {
        error(STORE, "Cannot extract hard link %s because target %s does not exist!\n",
              file_to_extract->c_str(), target->c_str());
    }
    if (!stat->samePermissions(&target_stat)) {
        error(STORE, "Hard link target must have same permissions as hard link definition!\n"
              "Expected %s to have permissions %s\n", target->c_str(), permissionString(&target_stat).c_str());
    }
    if (!stat->sameMTime(&target_stat)) {
        error(STORE, "Hard link target must have same MTime as hard link definition!\n"
              "Expected %s to have mtime xxx\n", target->c_str());
    }
    FileStat old_stat;
    if (OK == dst->stat(file_to_extract, &old_stat)) {
        if (stat->samePermissions(&old_stat) &&
            target_stat.sameSize(&old_stat) && // The hard link definition does not have size.
            stat->sameMTime(&old_stat)) {
            debug(STORE, "Skipping hard link \"%s\"\n", file_to_extract->c_str());
            return false;
        }
    }

    debug(STORE, "Storing hard link %s to %s\n", file_to_extract->c_str(), target->c_str());

    dst->mkDirp(file_to_extract->parent());
    dst->createHardLink(file_to_extract, stat, target);
    dst->utime(file_to_extract, stat);
    statistics->num_hard_links_stored++;
    verbose(STORE, "Stored hard link %s\n", file_to_extract->c_str());
    statistics->displayProgress();
    return true;
}

bool extractFile(Entry *entry,
                 FileSystem *src, Path *tar_file, off_t tar_file_offset,
                 FileSystem *dst, Path *file_to_extract, FileStat *stat,
                 StoreStatistics *statistics)
{
    if (stat->disk_update == NoUpdate) {
        debug(STORE, "Skipping file \"%s\"\n", file_to_extract->c_str());
        return false;
    }
    if (stat->disk_update == UpdatePermissions) {
        dst->chmod(file_to_extract, stat);
        verbose(STORE, "Updating permissions for file \"%s\" to %o\n", file_to_extract->c_str(), stat->st_mode);
        return false;
    }

    debug(STORE, "Storing file \"%s\" size %ju permissions %s\n   using tar \"%s\" offset %ju\n",
          file_to_extract->c_str(), stat->st_size, permissionString(stat).c_str(),
          tar_file->c_str(), tar_file_offset);

    dst->mkDirp(file_to_extract->parent());
    dst->createFile(file_to_extract, stat,
        [src,tar_file_offset,file_to_extract,tar_file] (off_t offset, char *buffer, size_t len)
        {
            debug(STORE,"Extracting %ju bytes to file %s\n", len, file_to_extract->c_str());
            ssize_t n = src->pread(tar_file, buffer, len, tar_file_offset + offset);
            debug(STORE, "Extracted %ju bytes from %ju to %ju.\n", n,
                  tar_file_offset+offset, offset);
            return n;
        });

    dst->utime(file_to_extract, stat);
    statistics->num_files_stored++;
    statistics->size_files_stored+=stat->st_size;
    verbose(STORE, "Stored %s (%ju %s %06o)\n",
            file_to_extract->c_str(), stat->st_size, permissionString(stat).c_str(), stat->st_mode);
    statistics->displayProgress();
    return true;
}

bool extractSymbolicLink(FileSystem *src, string target,
                        FileSystem *dst, Path *file_to_extract, FileStat *stat,
                        StoreStatistics *statistics)
{
    string old_target;
    FileStat old_stat;
    bool found =  OK == dst->stat(file_to_extract, &old_stat);
    if (found) {
        if (stat->samePermissions(&old_stat) &&
            stat->sameSize(&old_stat) &&
            stat->sameMTime(&old_stat)) {
            if (dst->readLink(file_to_extract, &old_target)) {
                if (target == old_target) {
                    debug(STORE, "Skipping existing link %s\n", file_to_extract->c_str());
                    return false;
                }
            }
        }
    }

    debug(STORE, "Storing symlink %s to %s\n", file_to_extract->c_str(), target.c_str());

    dst->mkDirp(file_to_extract->parent());
    if (found) {
        dst->deleteFile(file_to_extract);
    }
    dst->createSymbolicLink(file_to_extract, stat, target);
    dst->utime(file_to_extract, stat);
    statistics->num_symbolic_links_stored++;
    verbose(STORE, "Stored symlink %s\n", file_to_extract->c_str());
    statistics->displayProgress();
    return true;
}

bool extractNode(FileSystem *src, FileSystem *dst, Path *file_to_extract, FileStat *stat,
                 StoreStatistics *statistics)
{
    FileStat old_stat;
    if (OK == dst->stat(file_to_extract, &old_stat)) {
        if (stat->samePermissions(&old_stat) &&
            stat->sameMTime(&old_stat)) {
            // Compare of size is ignored since the nodes have no size...
            debug(STORE, "Skipping mknod of \"%s\"\n", file_to_extract->c_str());
            return false;
        }
    }

    if (stat->isFIFO()) {
        debug(STORE, "Storing FIFO %s\n", file_to_extract->c_str());
        dst->mkDirp(file_to_extract->parent());
        dst->createFIFO(file_to_extract, stat);
        dst->utime(file_to_extract, stat);
        verbose(STORE, "Stored fifo %s\n", file_to_extract->c_str());
        statistics->displayProgress();
    }
    return true;
}

bool chmodDirectory(FileSystem *dst, Path *file_to_extract, FileStat *stat,
                    StoreStatistics *statistics)
{
    FileStat old_stat;
    if (OK == dst->stat(file_to_extract, &old_stat)) {
        if (stat->samePermissions(&old_stat) &&
            stat->sameMTime(&old_stat)) {
            // Compare of directory size is ignored since the size differ between
            // different file systems.
            debug(STORE, "Skipping chmod of dir \"%s\"\n", file_to_extract->c_str());
            return false;
        }
    }

    debug(STORE, "Chmodding directory %s %s\n", file_to_extract->c_str(),
          permissionString(stat).c_str());

    dst->mkDirp(file_to_extract);
    dst->chmod(file_to_extract, stat);
    dst->utime(file_to_extract, stat);
    statistics->num_dirs_updated++;
    verbose(STORE, "Updated dir %s\n", file_to_extract->c_str());
    statistics->displayProgress();
    return true;
}

void calculateReverseWork(Path *path, FileStat *stat,
                          ReverseTarredFS *rfs, Beak *beak, PointInTime *point,
                          Options *settings, StoreStatistics *st,
                          FileSystem *src_fs, FileSystem *dst_fs)
{
    auto entry = rfs->findEntry(point, path);
    auto file_to_extract = path->prepend(settings->dst);

    if (entry->is_hard_link) st->num_hard_links++;
    else if (stat->isRegularFile()) {
        stat->checkStat(dst_fs, file_to_extract);
        if (stat->disk_update == Store) {
            st->num_files_to_store++; st->size_files_to_store+=stat->st_size;
        }
        st->num_files++; st->size_files+=stat->st_size;
    }
    else if (stat->isSymbolicLink()) st->num_symbolic_links++;
    else if (stat->isDirectory()) st->num_dirs++;
    else if (stat->isFIFO()) st->num_nodes++;
}

void handleHardLinks(Path *path, FileStat *stat,
                     ReverseTarredFS *rfs, Beak *beak,PointInTime *point,
                     Options *settings, StoreStatistics *st,
                     FileSystem *src_fs, FileSystem *dst_fs)
{
    auto entry = rfs->findEntry(point, path);
    auto file_to_extract = path->prepend(settings->dst);

    if (entry->is_hard_link) {
        // Special case since hard links are not encoded in stat structure.
        extractHardLink(src_fs, entry->hard_link,
                        dst_fs, settings->dst,
                        file_to_extract, stat, st);
    }
}

void handleRegularFiles(Path *path, FileStat *stat,
                        ReverseTarredFS *rfs, Beak *beak,PointInTime *point,
                        Options *settings, StoreStatistics *st,
                        FileSystem *src_fs, FileSystem *dst_fs)
{
    auto entry = rfs->findEntry(point, path);
    auto tar_file = entry->tar->prepend(settings->src);
    auto tar_file_offset = entry->offset;
    auto file_to_extract = path->prepend(settings->dst);

    if (!entry->is_hard_link && stat->isRegularFile()) {
        extractFile(entry, src_fs, tar_file, tar_file_offset,
                    dst_fs, file_to_extract, stat, st);
        st->num_files_handled++;
        st->size_files_handled += stat->st_size;
    }
}

void handleNodes(Path *path, FileStat *stat,
                 ReverseTarredFS *rfs, Beak *beak,PointInTime *point,
                 Options *settings, StoreStatistics *st,
                 FileSystem *src_fs, FileSystem *dst_fs)
{
    auto entry = rfs->findEntry(point, path);
    auto file_to_extract = path->prepend(settings->dst);

    if (!entry->is_hard_link && stat->isFIFO()) {
        extractNode(src_fs, dst_fs, file_to_extract, stat, st);
    }
}

void handleSymbolicLinks(Path *path, FileStat *stat,
                         ReverseTarredFS *rfs, Beak *beak,PointInTime *point,
                         Options *settings, StoreStatistics *st,
                         FileSystem *src_fs, FileSystem *dst_fs)
{
    auto entry = rfs->findEntry(point, path);
    auto file_to_extract = path->prepend(settings->dst);

    if (!entry->is_hard_link && stat->isSymbolicLink()) {
        extractSymbolicLink(src_fs, entry->symlink,
                            dst_fs, file_to_extract, stat, st);
    }
}

void handleDirs(Path *path, FileStat *stat,
                ReverseTarredFS *rfs, Beak *beak,PointInTime *point,
                Options *settings, StoreStatistics *st,
                FileSystem *src_fs, FileSystem *dst_fs)
{
    auto file_to_extract = path->prepend(settings->dst);

    if (stat->isDirectory()) {
        chmodDirectory(dst_fs, file_to_extract, stat, st);
    }
}

int BeakImplementation::storeReverse(Options *settings)
{
    RC rc = OK;

    umask(0);
    auto rfs  = newReverseTarredFS(src_file_system_);
    auto view = rfs->asFileSystem();

    rfs->lookForPointsInTime(PointInTimeFormat::absolute_point, settings->src);

    auto point = rfs->setPointInTime("@0");
    if (settings->pointintime != "") {
        point = rfs->setPointInTime(settings->pointintime);
    }
    if (!point) {
        error(STORE, "No such point in time!\n");
    }
    assert(rfs->singlePointInTime());

    uint64_t start = clockGetTime();
    rc = rfs->loadBeakFileSystem(settings);

    StoreStatistics st;

    view->recurse([&rfs,this,point,settings,&st]
                  (Path *path, FileStat *stat) {calculateReverseWork(path,stat,rfs.get(),this,point,settings,&st,
                                                                     src_file_system_, dst_file_system_); });
    debug(STORE, "Work to be done: num_files=%ju num_hardlinks=%ju num_symlinks=%ju num_nodes=%ju num_dirs=%ju\n",
          st.num_files, st.num_hard_links, st.num_symbolic_links, st.num_nodes, st.num_dirs);

    view->recurse([&rfs,this,point,settings,&st]
                  (Path *path, FileStat *stat) {handleRegularFiles(path,stat,rfs.get(),this,point,settings,&st,
                                                                src_file_system_, dst_file_system_); });
    view->recurse([&rfs,this,point,settings,&st]
                  (Path *path, FileStat *stat) {handleNodes(path,stat,rfs.get(),this,point,settings,&st,
                                                                src_file_system_, dst_file_system_); });
    view->recurse([&rfs,this,point,settings,&st]
                  (Path *path, FileStat *stat) {handleSymbolicLinks(path,stat,rfs.get(),this,point,settings,&st,
                                                                src_file_system_, dst_file_system_); });
    view->recurse([&rfs,this,point,settings,&st]
                  (Path *path, FileStat *stat) {handleHardLinks(path,stat,rfs.get(),this,point,settings,&st,
                                                                src_file_system_, dst_file_system_); });
    view->recurse([&rfs,this,point,settings,&st]
                  (Path *path, FileStat *stat) {handleDirs(path,stat,rfs.get(),this,point,settings,&st,
                                                                src_file_system_, dst_file_system_); });

    uint64_t stop = clockGetTime();
    uint64_t store_time = stop - start;

    st.finishProgress();

    if (st.num_files_stored == 0 && st.num_symbolic_links_stored == 0 && st.num_dirs_updated == 0) {
        info(STORE, "No stores needed, everything was up to date.\n");
    } else {
        if (st.num_files_stored > 0) {
            string file_sizes = humanReadable(st.size_files_stored);
            info(STORE, "Stored %ju files for a total size of %s.\n", st.num_files_stored, file_sizes.c_str());
        }
        if (st.num_symbolic_links_stored > 0) {
            info(STORE, "Stored %ju symlinks.\n", st.num_symbolic_links_stored);
        }
        if (st.num_hard_links_stored > 0) {
            info(STORE, "Stored %ju hard links.\n", st.num_hard_links_stored);
        }
        if (st.num_dirs_updated > 0) {
            info(STORE, "Updated %ju dirs.\n", st.num_dirs_updated);
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

bool Options::hasBothSrcAndDst() {
    return src != NULL && dst != NULL;
}

bool Options::hasSrc() {
    return src != NULL;
}
