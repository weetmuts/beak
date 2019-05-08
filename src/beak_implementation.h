/*
 Copyright (C) 2016-2019 Fredrik Öhrström

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

#include "restore.h"

#include <map>
#include <string>
#include <utility>
#include <vector>

using namespace std;

struct CommandEntry {
    const char *name;
    CommandType cmdtype;
    Command cmd;
    const char *info;
    ArgumentType expected_from, expected_to;
};

struct OptionEntry {
    OptionType type;
    const char *shortname;
    const char *name;
    Option option;
    bool requires_value;
    const char *info;
};

extern vector<CommandEntry> command_entries_;
extern vector<OptionEntry> option_entries_;

bool hasCommandOption(Command cmd, Option option);

struct BeakImplementation : Beak {

    BeakImplementation(ptr<Configuration> configuration,
                       ptr<System> sys,
                       ptr<FileSystem> local_fs,
                       ptr<StorageTool> storage_tool,
                       ptr<OriginTool> origin_tool);

    void printCommands(bool verbose);
    void printSettings(bool verbose, Command cmd);

    void captureStartTime() {  ::captureStartTime(); }
    Command parseCommandLine(int argc, char **argv, Settings *settings);

    void printHelp(bool verbose, Command cmd);
    void printVersion(bool verbose);

    RC configure(Settings *settings);
    RC diff(Settings *settings);
    RC fsck(Settings *settings);
    RC push(Settings *settings);
    RC pull(Settings *settings);
    RC prune(Settings *settings);

    RC umountDaemon(Settings *settings);

    RC mountBackupDaemon(Settings *settings);
    RC mountBackup(Settings *settings, ProgressStatistics *progress = NULL);
    RC umountBackup(Settings *settings);

    RC mountRestoreDaemon(Settings *settings);
    RC mountRestore(Settings *settings, ProgressStatistics *progress = NULL);
    RC umountRestore(Settings *settings);

    RC shell(Settings *settings);

    RC status(Settings *settings);
    RC store(Settings *settings);
    RC restore(Settings *settings);

    void genAutoComplete(string filename);

    private:

    string argsToVector_(int argc, char **argv, vector<string> *args);
    unique_ptr<Restore> accessBackup_(Argument *storage,
                                      string pointintime,
                                      ProgressStatistics *progress,
                                      FileSystem **out_backup_fs = NULL,
                                      Path **out_root = NULL);
    RC mountRestoreInternal_(Settings *settings, bool daemon, ProgressStatistics *progress);
    bool hasPointsInTime_(Path *path, FileSystem *fs);

    map<string,CommandEntry*> commands_;
    map<Command,CommandEntry*> commands_from_cmd_;
    map<string,OptionEntry*> short_options_;
    map<string,OptionEntry*> long_options_;

    OptionEntry *nosuch_option_;

    vector<PointInTime> history_;

    CommandEntry *parseCommand(string s);
    OptionEntry *parseOption(string s, bool *has_value, string *value);
    Argument parseArgument(std::string arg, ArgumentType expected_type, Settings *settings, Command cmd);

    unique_ptr<FuseMount> backup_fuse_mount_;
    unique_ptr<FuseMount> restore_fuse_mount_;

    ptr<Configuration> configuration_;
    ptr<System> sys_;
    ptr<FileSystem> local_fs_;
    ptr<StorageTool> storage_tool_;
    ptr<OriginTool> origin_tool_;
};
