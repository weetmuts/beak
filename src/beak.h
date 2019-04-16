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

#ifndef BEAK_H
#define BEAK_H

#include "always.h"
#include "configuration.h"
#include "filesystem.h"
#include "statistics.h"
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
enum class WhichArgument { FirstArg, SecondArg  };

struct Settings;
struct PointInTime;
struct Rule;
struct Storage;
struct StorageTool;
struct OriginTool;

enum class CommandType { PRIMARY, SECONDARY };

struct Beak
{
    virtual void captureStartTime() = 0;
    virtual Command parseCommandLine(int argc, char **argv, Settings *settings) = 0;

    virtual RC store(Settings *settings) = 0;
    virtual RC restore(Settings *settings) = 0;
    virtual RC shell(Settings *settings) = 0;
    virtual RC prune(Settings *settings) = 0;

    virtual RC diff(Settings *settings) = 0;
    virtual RC fsck(Settings *settings) = 0;
    virtual RC configure(Settings *settings) = 0;

    virtual RC status(Settings *settings) = 0;
    virtual RC push(Settings *settings) = 0;
    virtual RC pull(Settings *settings) = 0;

    virtual RC umountDaemon(Settings *settings) = 0;

    virtual RC mountBackupDaemon(Settings *settings) = 0;
    virtual RC mountBackup(Settings *settings, ProgressStatistics *progress = NULL) = 0;
    virtual RC umountBackup(Settings *settings) = 0;
    virtual RC mountRestoreDaemon(Settings *settings) = 0;
    virtual RC mountRestore(Settings *settings, ProgressStatistics *progress = NULL) = 0;
    virtual RC umountRestore(Settings *settings) = 0;

    virtual void printHelp(Command cmd) = 0;
    virtual void printVersion() = 0;
    virtual void printLicense() = 0;
    virtual void printCommands(CommandType cmdtype) = 0;
    virtual void printSettings(CommandType cmdtype) = 0;

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
    ArgStorage,
    ArgDir,
    ArgFile,
    ArgFileOrNone,
    ArgORS,  // Origin, Rule or Storage
    ArgNORS, // None, Origin, Rule or Storage
};

// ArgORS will match ArgOrigin, ArgRule or ArgStorage.

#define LIST_OF_COMMANDS \
    X(bmount,CommandType::SECONDARY,"Mount your file system as a backup.",ArgOrigin,ArgDir) \
    X(config,CommandType::PRIMARY,"Configure backup rules.",ArgNone,ArgNone)               \
    X(diff,CommandType::PRIMARY,"Show differences between backups and/or origins.",ArgORS,ArgORS) \
    X(fsck,CommandType::PRIMARY,"Check the integrity of your backup.",ArgStorage,ArgNone) \
    X(genautocomplete,CommandType::SECONDARY,"Output bash completion script for beak.",ArgFileOrNone,ArgNone) \
    X(genmounttrigger,CommandType::SECONDARY,"Output systemd rule to trigger backup when USB drive is mounted.",ArgFile,ArgNone) \
    X(help,CommandType::PRIMARY,"Show help. Also: beak push help",ArgNone,ArgNone) \
    X(license,CommandType::PRIMARY,"Display license and notices.",ArgNone,ArgNone) \
    X(mount,CommandType::PRIMARY,"Mount your backup as a file system.",ArgStorage,ArgDir) \
    X(prune,CommandType::PRIMARY,"Discard old backups according to the keep rule.",ArgStorage,ArgNone) \
    X(pull,CommandType::PRIMARY,"Merge the most recent backup for the given rule.",ArgRule,ArgNone) \
    X(push,CommandType::PRIMARY,"Backup a rule to a storage location.",ArgRule,ArgNone) \
    X(restore,CommandType::PRIMARY,"Restore from a backup into your file system.",ArgStorage,ArgOrigin) \
    X(shell,CommandType::PRIMARY,"Mount your backup(s) and spawn a shell. Exit the shell to unmount.",ArgStorage,ArgNone) \
    X(status,CommandType::PRIMARY,"Show the status of your rule backups.",ArgRule,ArgNone) \
    X(store,CommandType::PRIMARY,"Store your file system into a backup.",ArgOrigin,ArgStorage) \
    X(umount,CommandType::PRIMARY,"Unmount a virtual file system.",ArgDir,ArgNone) \
    X(version,CommandType::PRIMARY,"Show version.",ArgNone,ArgNone) \
    X(nosuch,CommandType::SECONDARY,"No such command.",ArgNone,ArgNone) \

enum Command : short {
#define X(name,cmdtype,info,argfrom,argto) name##_cmd,
LIST_OF_COMMANDS
#undef X
};

#define LIST_OF_OPTIONS \
    X(CommandType::SECONDARY,c,cache,std::string,true,"Directory to store cached files when mounting a remote storage.") \
    X(CommandType::SECONDARY,,contentsplit,std::vector<std::string>,true,"Split matching files based on content. E.g. -cs '*.vdi'") \
    X(CommandType::SECONDARY,d,depth,int,true,"Force all dirs at this depth to contain tars.\n" \
      "                           1 is the root, 2 is the first subdir. The default is 2.")    \
    X(CommandType::SECONDARY,,dryrun,bool,false,"Print what would be done, do not actually perform the prune/store.\n") \
    X(CommandType::SECONDARY,f,foreground,bool,false,"When mounting do not spawn a daemon.")   \
    X(CommandType::SECONDARY,fd,fusedebug,bool,false,"Enable fuse debug mode, this also triggers foreground.") \
    X(CommandType::SECONDARY,i,include,std::vector<std::string>,true,"Only matching paths are inluded. E.g. -i '*.c'") \
    X(CommandType::SECONDARY,k,keep,std::string,true,"Keep rule for prune.") \
    X(CommandType::SECONDARY,l,log,std::string,true,"Log debug messages for these parts. E.g. --log=backup,hashing --log=all,-lock") \
    X(CommandType::SECONDARY,ll,listlog,bool,false,"List all log parts available.") \
    X(CommandType::SECONDARY,pf,pointintimeformat,PointInTimeFormat,true,"How to present the point in time.\n" \
      "                           E.g. absolute,relative or both. Default is both.")    \
    X(CommandType::SECONDARY,pr,progress,ProgressDisplayType,true,"How to present the progress of the backup or restore.\n" \
      "                           E.g. none,plain,ansi,os. Default is ansi.") \
    X(CommandType::SECONDARY,,relaxtimechecks,bool,false,"Accept future dated files.") \
    X(CommandType::SECONDARY,,robot,bool,false,"Switch to a terminal output format that suitable for parsing by another program.\n") \
    X(CommandType::SECONDARY,,tarheader,TarHeaderStyle,true,"Style of tar headers used. E.g. --tarheader=simple\n"   \
      "                           Alternatives are: none,simple,full Default is simple.")    \
    X(CommandType::SECONDARY,,now,std::string,true,"When pruning use this date time as now.\n") \
    X(CommandType::SECONDARY,ta,targetsize,size_t,true,"Tar target size. E.g. --targetsize=20M\n" \
      "                           Default is 10M.")    \
    X(CommandType::SECONDARY,tr,triggersize,size_t,true,"Trigger tar generation in dir at size. E.g. -tr 40M\n" \
      "                           Default is 20M.")    \
    X(CommandType::SECONDARY,ts,splitsize,size_t,true,"Split large files into smaller chunks. E.g. -ts 40M\n" \
      "                           Default is 50M.")    \
    X(CommandType::SECONDARY,tx,triggerglob,std::vector<std::string>,true,"Trigger tar generation in matching dirs.\n" \
      "                           E.g. -tx '/work/project_*'\n") \
    X(CommandType::SECONDARY,q,quite,bool,false,"Silence information output.")             \
    X(CommandType::PRIMARY,v,verbose,bool,false,"More detailed information.")            \
    X(CommandType::SECONDARY,x,exclude,std::vector<std::string>,true,"Paths matching glob are excluded. E.g. -exclude='beta/**'") \
    X(CommandType::SECONDARY,,yesorigin,bool,false,"The origin directory contains beak files and this is intended.")            \
    X(CommandType::SECONDARY,,yesprune,bool,false,"Respond yes to question if prune should be done.")            \
    X(CommandType::SECONDARY,nso,nosuch,bool,false,"No such option")

enum Option {
#define X(cmdtype,shortname,name,type,requirevalue,info) name##_option,
LIST_OF_OPTIONS
#undef X
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


#endif
