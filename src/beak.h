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
#include "util.h"

#include<memory>
#include<string>
#include<vector>

#define DEFAULT_DEPTH_TO_FORCE_TARS 2
#define DEFAULT_KEEP_RULE "all:2d daily:2w weekly:2m monthly:2y"
#define KEEP_RULE_SHORTHAND_TWOS "all:2d daily:2w weekly:2m monthly:2y"
#define KEEP_RULE_SHORTHAND_ONES "all:1d daily:1w weekly:1m monthly:1y"

enum Command : short;
enum PointInTimeFormat : short;
enum TarHeaderStyle : short;

struct Settings;
struct PointInTime;
struct Rule;
struct Storage;
struct StorageTool;
struct OriginTool;

struct Beak
{
    virtual void captureStartTime() = 0;
    virtual Command parseCommandLine(int argc, char **argv, Settings *settings) = 0;

    virtual RC store(Settings *settings) = 0;
    virtual RC restore(Settings *settings) = 0;
    virtual RC shell(Settings *settings) = 0;
    virtual RC prune(Settings *settings) = 0;

    virtual RC fsck(Settings *settings) = 0;
    virtual RC configure(Settings *settings) = 0;

    virtual RC status(Settings *settings) = 0;
    virtual RC push(Settings *settings) = 0;
    virtual RC pull(Settings *settings) = 0;

    virtual RC umountDaemon(Settings *settings) = 0;

    virtual RC mountBackupDaemon(Settings *settings) = 0;
    virtual RC mountBackup(Settings *settings) = 0;
    virtual RC umountBackup(Settings *settings) = 0;
    virtual RC mountRestoreDaemon(Settings *settings) = 0;
    virtual RC mountRestore(Settings *settings) = 0;
    virtual RC umountRestore(Settings *settings) = 0;

    virtual void printHelp(Command cmd) = 0;
    virtual void printVersion() = 0;
    virtual void printLicense() = 0;
    virtual void printCommands() = 0;
    virtual void printSettings() = 0;

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
    ArgORS,  // Origin, Rule or Storage
    ArgNORS, // None, Origin, Rule or Storage
};

// ArgORS will match ArgOrigin, ArgRule or ArgStorage.

#define LIST_OF_COMMANDS \
    X(bmount,"Mount your file system as a backup.",ArgOrigin,ArgDir) \
    X(config,"Configure backup rules.",ArgNone,ArgNone) \
    X(diff,"Show changes since last backup.",ArgORS,ArgORS) \
    X(fsck,"Check the integrity of your backup.",ArgStorage,ArgNone) \
    X(genautocomplete,"Output bash completion script for beak.",ArgFile,ArgNone) \
    X(genmounttrigger,"Output systemd rule to trigger backup when USB drive is mounted.",ArgFile,ArgNone) \
    X(help,"Show help. Also: beak push help",ArgORS,ArgNone) \
    X(prune,"Discard old backups according to the keep rule.",ArgStorage,ArgNone) \
    X(mount,"Mount your backup as a file system.",ArgStorage,ArgDir) \
    X(pull,"Merge the most recent backup for the given rule.",ArgRule,ArgNone) \
    X(push,"Backup a rule to a storage location.",ArgRule,ArgNone) \
    X(restore,"Restore from a backup into your file system.",ArgStorage,ArgOrigin) \
    X(shell,"Mount your backup(s) and spawn a shell. Exit the shell to unmount.",ArgStorage,ArgNone) \
    X(status,"Show the status of your backups of a rule.",ArgRule,ArgNone) \
    X(store,"Store your file system into a backup.",ArgOrigin,ArgStorage) \
    X(umount,"Unmount a virtual file system.",ArgDir,ArgNone) \
    X(version,"Show version.",ArgNone,ArgNone) \
    X(nosuch,"No such command.",ArgNone,ArgNone) \

enum Command : short {
#define X(name,info,argfrom,argto) name##_cmd,
LIST_OF_COMMANDS
#undef X
};

#define LIST_OF_OPTIONS \
    X(c,cache,std::string,true,"Directory to store cached files when mounting a remote storage.") \
    X(d,depth,int,true,"Force all dirs at this depth to contain tars.\n" \
      "                           1 is the root, 2 is the first subdir. The default is 2.")    \
    X(f,foreground,bool,false,"When mounting do not spawn a daemon.")   \
    X(fd,fusedebug,bool,false,"Enable fuse debug mode, this also triggers foreground.") \
    X(i,include,std::vector<std::string>,true,"Only matching paths are inluded. E.g. -i '*.c'") \
    X(,license,bool,false,"Show copyright holders,licenses and notices for the program.") \
    X(l,log,std::string,true,"Log debug messages for these parts. E.g. --log=reverse,hashing") \
    X(ll,listlog,bool,false,"List all log parts available.") \
    X(p,pointintime,std::string,true,"When mounting an archive pick this point in time only.\n" \
      "                           -p @0 is always the most recent. -p @1 the second most recent.\n" \
      "                           You can also suffix @1 to the storage directory." )        \
    X(pf,pointintimeformat,PointInTimeFormat,true,"How to present the point in time.\n" \
      "                           E.g. absolute,relative or both. Default is both.")    \
    X(,tarheader,TarHeaderStyle,true,"Style of tar headers used. E.g. --tarheader=simple\n"   \
      "                           Alternatives are: none,simple,full Default is simple.")    \
    X(ta,targetsize,size_t,true,"Tar target size. E.g. --targetsize=20M\n" \
      "                           Default is 10M.")    \
    X(tr,triggersize,size_t,true,"Trigger tar generation in dir at size. E.g. -tr 40M\n" \
      "                           Default is 20M.")    \
    X(tx,triggerglob,std::vector<std::string>,true,"Trigger tar generation in matching dirs.\n" \
      "                           E.g. -tx '/work/project_*'\n") \
    X(q,quite,bool,false,"Silence information output.")             \
    X(v,verbose,bool,false,"More detailed information.")            \
    X(x,exclude,std::vector<std::string>,true,"Paths matching glob are excluded. E.g. -exclude='beta/**'") \
    X(,yesorigin,bool,false,"The origin directory contains beak files and this is intended.")            \
    X(nso,nosuch,bool,false,"No such option")

enum Option {
#define X(shortname,name,type,requirevalue,info) name##_option,
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

#define X(shortname,name,type,requirevalue,info) type name {}; bool name##_supplied {};
LIST_OF_OPTIONS
#undef X

    std::vector<std::string> fuse_args;
    int fuse_argc {};
    char **fuse_argv {};

    Command help_me_on_this_cmd {};
    int point_in_time {};

    Settings copy() { return *this; }
    void updateFuseArgsArray();
};


#endif
