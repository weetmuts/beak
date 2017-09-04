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

#include "defs.h"
#include "forward.h"
#include "reverse.h"

#include<memory>
#include<string>
#include<vector>

#define LIST_OF_COMMANDS                                                \
    X(check,"Check the integrity of an archive.")                       \
    X(help,"Show help. Also: beak push help")                           \
    X(info,"List points in time and other info about archive.")         \
    X(mount,"Mount a backup as a virtual file system.")                 \
    X(pack,"Update the backup to use incremental changes.")             \
    X(prune,"Discard old backups according to the backup retention policy.") \
    X(pull,"Restore a backup to a directory.")                          \
    X(push,"Backup a directory.")                                       \
    X(status,"Show the current status of your backups.")                \
    X(version,"Show version.")                                          \
    X(nosuch,"No such command.")                                        \

enum Command {
#define X(name,info) name##_cmd,
LIST_OF_COMMANDS
#undef X
};

#define LIST_OF_OPTIONS \
    X(d,depth,int,true,"Force all dirs at this depth to contain tars.\n" \
          "                      1 is the root, 2 is the first subdir. The default is 2.")    \
    X(f,foreground,bool,false,"When mounting do not spawn a daemon.")   \
    X(fd,fusedebug,bool,false,"Enable fuse debug mode, this also triggers foreground.") \
    X(ff,forceforward,bool,false,"Force forward mount of backup directory," \
                                 "if you want to backup your backup files!")  \
    X(i,include,vector<string>,true,"Only paths matching glob are inluded. E.g. -i '.*\\.c'") \
    X(l,log,string,true,"Log debug messages for these parts. E.g. --log=reverse,hashing") \
    X(p,pointintime,string,true,"When mounting an archive pick this point in time only.\n" \
      "                       -p @0 is always the most recent. -p @1 the second most recent.\n" \
      "                       You can also suffix @1 to the src directory." )        \
    X(pf,pointintimeformat,PointInTimeFormat,true,"How to present the point in time.\n" \
      "                                 E.g. absolute,relative or both. Default is both.")    \
    X(ta,targetsize,size_t,true,"Tar target size. E.g. --targetsize=20M" \
      "                      Default is 10M.")    \
    X(tr,triggersize,size_t,true,"Trigger tar generation in dir at size. E.g. -tr 40M\n" \
      "                      Default is 20M.")    \
    X(tx,triggerglob,vector<string>,true,"Trigger tar generation in dir if path matches glob. " \
                                          "E.g. -tx 'work/project_.*'\n" \
      "                      Default is 20M.")    \
    X(q,quite,bool,false,"Silence information output.")             \
    X(v,verbose,bool,false,"More detailed information.")            \
    X(x,exclude,vector<string>,true,"Paths matching glob are excluded. E.g. -exclude='.*\\.c'") \
    X(nso,nosuch,bool,false,"No such option")    
        
    
enum Option {
#define X(shortname,name,type,requirevalue,info) name##_option,
LIST_OF_OPTIONS
#undef X
};

struct Options {
    Path *src;
    Path *dst;

#define X(shortname,name,type,requirevalue,info) type name; bool name##_supplied;
LIST_OF_OPTIONS
#undef X

    vector<string> fuse_args;
    int fuse_argc;
    char **fuse_argv;

    Command help_me_on_this_cmd;
    int point_in_time; // 0 is the most recent, 1 second recent etc.
};

struct Beak {
    virtual void captureStartTime() = 0;
    virtual string argsToVector(int argc, char **argv, vector<string> *args) = 0;
    virtual int parseCommandLine(vector<string> *args, Command *cmd, Options *settings) = 0;
    virtual int printInfo(Options *settings) = 0;
    virtual bool lookForPointsInTime(Options *settings) = 0;
    virtual vector<PointInTime> &history() = 0;
    virtual bool setPointInTime(string g) = 0;
    
    virtual int push(Options *settings) = 0;
    virtual int mountForward(Options *settings) = 0;
    virtual int mountReverse(Options *settings) = 0;
    virtual int status(Options *settings) = 0;
    
    virtual void printCommands() = 0;
    virtual void printOptions() = 0;
};

std::unique_ptr<Beak> newBeak();



#endif
