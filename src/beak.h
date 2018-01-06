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
#include "filesystem.h"
#include "util.h"

#include<memory>
#include<string>
#include<vector>

enum Command : short;
struct Options;

enum PointInTimeFormat : short;
struct PointInTime;

enum TarHeaderStyle : short;

struct Beak {
    virtual void captureStartTime() = 0;
    virtual std::string argsToVector(int argc, char **argv, std::vector<std::string> *args) = 0;
    virtual int parseCommandLine(int argc, char **argv, Command *cmd, Options *settings) = 0;
    virtual int printInfo(Options *settings) = 0;
    virtual bool lookForPointsInTime(Options *settings) = 0;
    virtual std::vector<PointInTime> &history() = 0;
    virtual bool setPointInTime(std::string g) = 0;

    virtual int configure(Options *settings) = 0;
    virtual int push(Options *settings) = 0;

    virtual int umountDaemon(Options *settings) = 0;

    virtual int mountForwardDaemon(Options *settings) = 0;
    virtual int mountForward(Options *settings) = 0;
    virtual int umountForward(Options *settings) = 0;
    virtual int mountReverseDaemon(Options *settings) = 0;
    virtual int mountReverse(Options *settings) = 0;
    virtual int umountReverse(Options *settings) = 0;

    virtual int shell(Options *settings) = 0;
    virtual int status(Options *settings) = 0;
    virtual int store(Options *settings) = 0;

    virtual void printHelp(Command cmd) = 0;
    virtual void printVersion() = 0;
    virtual void printLicense() = 0;
    virtual void printCommands() = 0;
    virtual void printOptions() = 0;

    virtual void genAutoComplete(std::string filename) = 0;
};

std::unique_ptr<Beak> newBeak(FileSystem *fs);


#define LIST_OF_COMMANDS                                                \
    X(check,"Check the integrity of an archive.")                       \
    X(config,"Configure backup rules.")                                 \
    X(checkout,"Overwrite your local directory with a backup.")         \
    X(diff,"Show changes since last backup.")                           \
    X(help,"Show help. Also: beak push help")                           \
    X(genautocomplete,"Output bash completion script for beak.")        \
    X(genmounttrigger,"Output systemd rule to trigger backup when USB drive is mounted.") \
    X(info,"List points in time and other info about archive.")         \
    X(mount,"Mount your file system as a backup, or vice versa.")       \
    X(history,"Mount all known storages for your backup.")              \
    X(prune,"Discard old backups according to the keep rule.")          \
    X(pull,"Merge a backup from a remote.")                             \
    X(push,"Backup a directory to a remote.")                           \
    X(shell,"Start a minimal shell to explore a backup.")               \
    X(status,"Show the status of your backups both locally and remotly.") \
    X(store,"Store your file system into a backup, or vice versa.")    \
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
    X(ff,forceforward,bool,false,"Force forward mount of backup directory,\n" \
      "                           if you want to backup your backup files!")  \
    X(i,include,std::vector<std::string>,true,"Only matching paths are inluded. E.g. -i '*.c'") \
    X(,license,bool,false,"Show copyright holders,licenses and notices for the program.") \
    X(l,log,std::string,true,"Log debug messages for these parts. E.g. --log=reverse,hashing") \
    X(p,pointintime,std::string,true,"When mounting an archive pick this point in time only.\n" \
      "                           -p @0 is always the most recent. -p @1 the second most recent.\n" \
      "                           You can also suffix @1 to the src directory." )        \
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

struct Options {
    Path *src;
    Path *dst;
    std::string remote;

#define X(shortname,name,type,requirevalue,info) type name; bool name##_supplied;
LIST_OF_OPTIONS
#undef X

    std::vector<std::string> fuse_args;
    int fuse_argc;
    char **fuse_argv;

    Command help_me_on_this_cmd;
    int point_in_time; // 0 is the most recent, 1 second recent etc.
};


#endif
