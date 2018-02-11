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
struct Options;
struct PointInTime;
struct Rule;
struct Storage;

struct Beak
{
    virtual void captureStartTime() = 0;
    virtual std::string argsToVector(int argc, char **argv, std::vector<std::string> *args) = 0;
    virtual Command parseCommandLine(int argc, char **argv, Options *settings) = 0;
    virtual RCC printInfo(Options *settings) = 0;
    virtual std::vector<PointInTime> history() = 0;

    virtual RCC configure(Options *settings) = 0;
    virtual RCC push(Options *settings) = 0;
    virtual RCC prune(Options *settings) = 0;

    virtual RCC umountDaemon(Options *settings) = 0;

    virtual RCC mountForwardDaemon(Options *settings) = 0;
    virtual RCC mountForward(Options *settings) = 0;
    virtual int umountForward(Options *settings) = 0;
    virtual RCC remountReverseDaemon(Options *settings) = 0;
    virtual RCC remountReverse(Options *settings) = 0;
    virtual int umountReverse(Options *settings) = 0;

    virtual int shell(Options *settings) = 0;
    virtual RCC status(Options *settings) = 0;
    virtual RCC storeForward(Options *settings) = 0;
    virtual RCC restoreReverse(Options *settings) = 0;

    virtual void printHelp(Command cmd) = 0;
    virtual void printVersion() = 0;
    virtual void printLicense() = 0;
    virtual void printCommands() = 0;
    virtual void printOptions() = 0;

    virtual void genAutoComplete(std::string filename) = 0;

    virtual ~Beak() = default;
};

std::unique_ptr<Beak> newBeak(ptr<FileSystem> src_fs, ptr<FileSystem> dst_fs);

#define LIST_OF_COMMANDS                                                \
    X(check,"Check the integrity of an archive.")                       \
    X(config,"Configure backup rules.")                                 \
    X(checkout,"Overwrite your source directory with a backup.")        \
    X(diff,"Show changes since last backup.")                           \
    X(help,"Show help. Also: beak push help")                           \
    X(genautocomplete,"Output bash completion script for beak.")        \
    X(genmounttrigger,"Output systemd rule to trigger backup when USB drive is mounted.") \
    X(info,"List points in time and other info about archive.")         \
    X(mount,"Mount your file system as a backup.")                      \
    X(history,"Mount all known storages for your backup.")              \
    X(prune,"Discard old backups according to the keep rule.")          \
    X(pull,"Merge a backup from a remote.")                             \
    X(push,"Backup a directory to a remote.")                           \
    X(remount,"Mount your backup as a file system.")                    \
    X(restore,"Restore from your backup into your file system.")        \
    X(shell,"Start a minimal shell to explore a backup.")               \
    X(status,"Show the status of your backups both locally and remotely.") \
    X(store,"Store your file system into a backup.")                    \
    X(umount,"Unmount a virtual file system.")                          \
    X(version,"Show version.")                                          \
    X(nosuch,"No such command.")                                        \

enum Command : short {
#define X(name,info) name##_cmd,
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
    X(nso,nosuch,bool,false,"No such option")

enum Option {
#define X(shortname,name,type,requirevalue,info) name##_option,
LIST_OF_OPTIONS
#undef X
};

enum ArgumentType
{
    ArgUnspecified,
    ArgPath,
    ArgRule,
    ArgStorage
};

struct Argument
{
    ArgumentType type {};
    Path *path {};
    Rule *rule {};
    Storage backup;
    std::string point_in_time;
};

struct Options
{
    Argument from, to;

#define X(shortname,name,type,requirevalue,info) type name {}; bool name##_supplied {};
LIST_OF_OPTIONS
#undef X

    std::vector<std::string> fuse_args;
    int fuse_argc {};
    char **fuse_argv {};

    Command help_me_on_this_cmd {};
    int point_in_time {};

    Options copy() { return *this; }
};


#endif
