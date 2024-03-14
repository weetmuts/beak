/*
 Copyright (C) 2016-2024 Fredrik Öhrström

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

#ifndef BEAK_H
#define BEAK_H

#include "always.h"
#include "configuration.h"
#include "filesystem.h"
#include "monitor.h"
#include "util.h"

#include<memory>
#include<string>
#include<vector>

// The default settings is to assume the subdirectories on the
// toplevel are independent projects. Thus force tars inside each subdir.
#define DEFAULT_DEPTH_TO_FORCE_TARS 2
#define DEFAULT_REMOTE_KEEP_RULE "all:2d daily:2w weekly:2m monthly:2y"
#define DEFAULT_LOCAL_KEEP_RULE "all:2d daily:2w"

enum Command : short;
enum PointInTimeFormat : short;
enum TarHeaderStyle : short;

enum class TarFilePaddingStyle : short;
enum class WhichArgument { FirstArg, SecondArg  };

struct Settings;
struct PointInTime;
struct Rule;
struct Storage;
struct StorageTool;
struct OriginTool;

enum class CommandType { PRIMARY, SECONDARY, MEDIA };
enum class OptionType { GLOBAL_PRIMARY, LOCAL_PRIMARY, GLOBAL_SECONDARY, LOCAL_SECONDARY };


Path *findBeakConf(int argc, char **argv, Path *default_conf);
void findAndSetLogging(int argc, char **argv);

struct Beak
{
    virtual Command parseCommandLine(int argc, char **argv, Settings *settings) = 0;

    virtual RC store(Settings *settings, Monitor *monitor) = 0;
    virtual RC restore(Settings *settings, Monitor *monitor) = 0;
    virtual RC shell(Settings *settings, Monitor *monitor) = 0;
    virtual RC importMedia(Settings *settings, Monitor *monitor) = 0;
    virtual RC indexMedia(Settings *settings, Monitor *monitor) = 0;
    virtual RC serveMedia(Settings *settings, Monitor *monitor) = 0;
    virtual RC prune(Settings *settings, Monitor *monitor) = 0;

    virtual RC diff(Settings *settings, Monitor *monitor) = 0;
    virtual RC delta(Settings *settings, Monitor *monitor) = 0;
    virtual RC stat(Settings *settings, Monitor *monitor) = 0;
    virtual RC fsck(Settings *settings, Monitor *monitor) = 0;
    virtual RC configure(Settings *settings) = 0;

    virtual RC status(Settings *settings, Monitor *monitor) = 0;
    virtual RC monitor(Settings *settings, Monitor *monitor) = 0;
    virtual RC push(Settings *settings, Monitor *monitor) = 0;
    virtual RC pull(Settings *settings, Monitor *monitor) = 0;
    virtual RC stash(Settings *settings, Monitor *monitor) = 0;

    virtual RC umountDaemon(Settings *settings) = 0;

    virtual RC mountBackupDaemon(Settings *settings) = 0;
    virtual RC mountBackup(Settings *settings, Monitor *monitor) = 0;
    virtual RC umountBackup(Settings *settings) = 0;
    virtual RC mountRestoreDaemon(Settings *settings, Monitor *monitor) = 0;
    virtual RC mountRestore(Settings *settings, Monitor *monitor) = 0;
    virtual RC umountRestore(Settings *settings) = 0;

    virtual void printHelp(bool verbose, Command cmd, bool has_media) = 0;
    virtual void printVersion(bool verbose) = 0;
    virtual void printCommands(bool verbose, bool has_media) = 0;
    virtual void printSettings(bool verbose, Command cmd, bool has_media) = 0;

    virtual void genAutoComplete(std::string filename) = 0;

    virtual ~Beak() = default;
};

std::unique_ptr<Beak> newBeak(ptr<Configuration> config,
                              ptr<System> sys, ptr<FileSystem> sys_fs,
                              ptr<StorageTool> storage_tool,
                              ptr<OriginTool> origin_tool);

enum ArgumentType
{
    ArgUnspecified,
    ArgNone,
    ArgOrigin,
    ArgRule,
    ArgRuleOrNone,
    ArgStorage,
    ArgStorageOrRule,
    ArgDir,
    ArgFile,
    ArgFileOrDir,
    ArgFileOrNone,
    ArgORS,  // Origin, Rule or Storage
    ArgNORS, // None, Origin, Rule or Storage
    ArgCommand, // Command
    ArgNC    // None or Command
};

#define LIST_OF_COMMANDS \
    X(bmount,CommandType::SECONDARY,"Mount your file system as a backup.",ArgOrigin,ArgDir) \
    X(config,CommandType::PRIMARY,"Configure backup rules.",ArgNone,ArgNone)               \
    X(delta,CommandType::PRIMARY,"Create new delta files in to storage for beak files not in from storage.",ArgStorage,ArgStorage) \
    X(diff,CommandType::PRIMARY,"Show differences between backups and/or origins.",ArgORS,ArgORS) \
    X(fsck,CommandType::PRIMARY,"Check the integrity of your backup.",ArgStorage,ArgNone) \
    X(genautocomplete,CommandType::SECONDARY,"Output bash completion script for beak.",ArgFileOrNone,ArgNone) \
    X(genmounttrigger,CommandType::SECONDARY,"Output systemd rule to trigger backup when USB drive is mounted.",ArgFile,ArgNone) \
    X(help,CommandType::PRIMARY,"Usage: beak help [-v] [<command>]",ArgNC,ArgNone) \
    X(monitor,CommandType::PRIMARY,"Monitor currently running backups.",ArgNone,ArgNone) \
    X(mount,CommandType::PRIMARY,"Mount your backup as a file system.",ArgStorageOrRule,ArgDir) \
    X(prune,CommandType::PRIMARY,"Discard old backups according to the keep rule.",ArgStorage,ArgNone) \
    X(pull,CommandType::PRIMARY,"Merge the most recent backup for the given rule.",ArgRule,ArgNone) \
    X(push,CommandType::PRIMARY,"Backup a rule to a storage location.",ArgRule,ArgNone) \
    X(pushd,CommandType::PRIMARY,"Backup a rule to a storage location using delta compression.",ArgRule,ArgNone) \
    X(restore,CommandType::PRIMARY,"Restore from a backup into your file system.",ArgStorage,ArgOrigin) \
    X(shell,CommandType::PRIMARY,"Mount your backup(s) and spawn a shell. Exit the shell to unmount.",ArgStorageOrRule,ArgNone) \
    X(stat,CommandType::PRIMARY,"Show file type statistics for a directory or a backup.",ArgORS,ArgNone) \
    X(import,CommandType::MEDIA,"Find media files in the source dir then rename and store them normalized into the target dir.",ArgFileOrDir,ArgStorage) \
    X(index,CommandType::MEDIA,"Scan imported media and generate thumbnails and index.html.",ArgOrigin,ArgNone) \
    X(serve,CommandType::MEDIA,"Serve imported media to a web-browser.",ArgOrigin,ArgNone) \
    X(status,CommandType::PRIMARY,"Show the backup status of your configured rules.",ArgRuleOrNone,ArgNone) \
    X(stash,CommandType::PRIMARY,"Stash the current directory into a local temporary backup.",ArgNone,ArgNone) \
    X(store,CommandType::PRIMARY,"Store your file system into a backup.",ArgOrigin,ArgStorage) \
    X(stored,CommandType::PRIMARY,"Store your file system into a backup using delta compression.",ArgOrigin,ArgStorage) \
    X(umount,CommandType::PRIMARY,"Unmount a virtual file system.",ArgDir,ArgNone) \
    X(version,CommandType::PRIMARY,"Show version.",ArgNone,ArgNone) \
    X(nosuch,CommandType::SECONDARY,"No such command.",ArgNone,ArgNone) \

enum Command : short {
#define X(name,cmdtype,info,argfrom,argto) name##_cmd,
LIST_OF_COMMANDS
#undef X
};

#define LIST_OF_OPTIONS \
    X(OptionType::LOCAL_PRIMARY,c,cache,std::string,true,"Directory to store cached files when mounting a remote storage.") \
    X(OptionType::LOCAL_PRIMARY,,contentsplit,std::vector<std::string>,true,"Split matching files based on content. E.g. --contentsplit='*.vdi'") \
    X(OptionType::LOCAL_PRIMARY,,deepcheck,bool,false,"Do deep checking of backup integrity.") \
    X(OptionType::LOCAL_PRIMARY,,delta,bool,true,"Use delta compression.")    \
    X(OptionType::LOCAL_PRIMARY,,depth,int,true,"Force all dirs at this depth to contain tars. 1 is the root, 2 is the first subdir. The default is 2.")    \
    X(OptionType::LOCAL_PRIMARY,,diff,bool,false,"Diff against stash.") \
    X(OptionType::LOCAL_PRIMARY,,dryrun,bool,false,"Print what would be done, do not actually perform the prune/store.") \
    X(OptionType::LOCAL_SECONDARY,f,foreground,bool,false,"When mounting do not spawn a daemon.")   \
    X(OptionType::LOCAL_SECONDARY,fd,fusedebug,bool,false,"Enable fuse debug mode, this also triggers foreground.") \
    X(OptionType::LOCAL_PRIMARY,bg,background,bool,false,"Enter background mode, the progress can be monitored using \"beak monitor\".") \
    X(OptionType::LOCAL_PRIMARY,i,include,std::vector<std::string>,true,"Only matching paths are inluded. E.g. -i '*.c'") \
    X(OptionType::LOCAL_PRIMARY,k,keep,std::string,true,"Keep rule for prune.") \
    X(OptionType::LOCAL_PRIMARY,,list,bool,false,"List stashes.") \
    X(OptionType::GLOBAL_SECONDARY,l,log,std::string,true,"Log debug messages for these parts. E.g. --log=backup,hashing --log=all,-lock") \
    X(OptionType::GLOBAL_SECONDARY,ll,listlog,bool,false,"List all log parts available.") \
    X(OptionType::LOCAL_PRIMARY,,monitor,bool,false,"Display download progress of cache downloads.") \
    X(OptionType::LOCAL_PRIMARY,pf,pointintimeformat,PointInTimeFormat,true,"How to present the point in time. E.g. absolute,relative or both. Default is both.")    \
    X(OptionType::GLOBAL_PRIMARY,pr,progress,ProgressDisplayType,true,"How to present the progress of the backup or restore. E.g. none,plain,ansi. Default is ansi.") \
    X(OptionType::LOCAL_SECONDARY,,relaxtimechecks,bool,false,"Accept future dated files.") \
    X(OptionType::LOCAL_SECONDARY,,tarheader,TarHeaderStyle,true,"Style of tar headers used. E.g. --tarheader=simple Alternatives are: none,simple,full Default is simple.")    \
    X(OptionType::LOCAL_PRIMARY,,now,std::string,true,"When pruning use this date time as now.") \
    X(OptionType::LOCAL_SECONDARY,,padding,TarFilePaddingStyle,true,"Style of padding of tarfiles. E.g. --padding=absolute Alternatives are: none,relative,absolute Default is relative.")    \
    X(OptionType::LOCAL_SECONDARY,ta,targetsize,size_t,true,"Tar target size. E.g. --targetsize=20M and the default is 10M.") \
    X(OptionType::LOCAL_SECONDARY,tr,triggersize,size_t,true,"Trigger tar generation in dir at size. E.g. -tr 40M and the default is 20M.")    \
    X(OptionType::GLOBAL_SECONDARY,,trace,bool,true,"Log the most detailed trace information.") \
    X(OptionType::LOCAL_SECONDARY,ts,splitsize,size_t,true,"Split large files into smaller chunks. E.g. -ts 40M and the default is 50M.")    \
    X(OptionType::LOCAL_SECONDARY,tx,triggerglob,std::vector<std::string>,true,"Trigger tar generation in matching dirs. E.g. -tx '/work/project_*'") \
    X(OptionType::GLOBAL_PRIMARY,q,quite,bool,false,"Silence information output.")             \
    X(OptionType::GLOBAL_SECONDARY,,useconfig,std::string,true,"Use this configuration file instead of the default.") \
    X(OptionType::GLOBAL_PRIMARY,v,verbose,bool,false,"More detailed information. Works for help as well.") \
    X(OptionType::GLOBAL_SECONDARY,,writeonly,bool,false,"Storage is write-only, avoid reads to verify backup.") \
    X(OptionType::LOCAL_PRIMARY,x,exclude,std::vector<std::string>,true,"Paths matching glob are excluded. E.g. -exclude='beta/**'") \
    X(OptionType::LOCAL_SECONDARY,,yesorigin,bool,false,"The origin directory contains beak files and this is intended.")            \
    X(OptionType::LOCAL_PRIMARY,,yesprune,bool,false,"Respond yes to question if prune should be done.")            \
    X(OptionType::LOCAL_PRIMARY,,yesrestore,bool,false,"Respond yes to question if restore should be done.")            \
    X(OptionType::LOCAL_PRIMARY,,forceoverwritefiles,bool,false,"When restoring overwrite newer files.")            \
    X(OptionType::LOCAL_PRIMARY,nso,nosuch,bool,false,"No such option")

enum Option {
#define X(cmdtype,shortname,name,type,requirevalue,info) name##_option,
LIST_OF_OPTIONS
#undef X
};

#define LIST_OF_OPTIONS_PER_COMMAND \
    X(bmount_cmd, (16, contentsplit_option, depth_option, foreground_option, fusedebug_option, splitsize_option, tarheader_option, targetsize_option, triggersize_option, triggerglob_option, exclude_option, include_option, progress_option, padding_option, relaxtimechecks_option, tarheader_option, yesorigin_option) ) \
    X(config_cmd, (0) ) \
    X(delta_cmd, (0) ) \
    X(diff_cmd, (1, depth_option) ) \
    X(stat_cmd, (1, depth_option) ) \
    X(fsck_cmd, (1, deepcheck_option) ) \
    X(import_cmd, (2, include_option, exclude_option) ) \
    X(store_cmd, (14, background_option, contentsplit_option, delta_option, depth_option, splitsize_option, targetsize_option, triggersize_option, triggerglob_option, exclude_option, include_option, padding_option, progress_option, relaxtimechecks_option, tarheader_option, yesorigin_option) ) \
    X(stored_cmd, (14, background_option, contentsplit_option, delta_option, depth_option, splitsize_option, targetsize_option, triggersize_option, triggerglob_option, exclude_option, include_option, padding_option, progress_option, relaxtimechecks_option, tarheader_option, yesorigin_option) ) \
    X(mount_cmd, (3, progress_option,foreground_option, fusedebug_option ) )  \
    X(prune_cmd, (4, keep_option, now_option, dryrun_option, yesprune_option) ) \
    X(pull_cmd, (2, background_option, progress_option) ) \
    X(push_cmd, (2, background_option, delta_option, progress_option) )  \
    X(pushd_cmd, (2, background_option, delta_option, progress_option) ) \
    X(restore_cmd, (4, background_option, progress_option, yesrestore_option, forceoverwritefiles_option) )  \
    X(stash_cmd, (1, diff_option, list_option) )


struct CommandOption
{
    Command cmd;
    std::vector<Option> options;

    void add_(int count, ...);
};

struct Argument
{
    ArgumentType type {};
    Rule *rule {};
    Path *origin {};
    Storage *storage {};
    Path *dir {};
    Path *file {};
    std::string point_in_time;
    Command command;
};

struct Settings
{
    Argument from, to;
    ArgumentType expected_from, expected_to;

#define X(cmdtype,shortname,name,type,requirevalue,info) type name {}; bool name##_supplied {};
LIST_OF_OPTIONS
#undef X

    std::vector<std::string> fuse_args;
    int fuse_argc {};
    char **fuse_argv {};

    Command help_me_on_this_cmd {};

    Settings copy() { return *this; }
    void updateFuseArgsArray();

    ~Settings();
};

std::string buildJobName(const char *cmd, Settings *s);

#endif
