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

struct NamedRestore
{
    string name;
    unique_ptr<Restore> restore;
};

struct BeakImplementation : Beak
{
    BeakImplementation(ptr<Configuration> configuration,
                       ptr<System> sys,
                       ptr<FileSystem> local_fs,
                       ptr<StorageTool> storage_tool,
                       ptr<OriginTool> origin_tool);

    void printCommands(bool verbose, bool has_media);
    void printSettings(bool verbose, Command cmd, bool has_media);

    Command parseCommandLine(int argc, char **argv, Settings *settings);

    void printHelp(bool verbose, Command cmd, bool has_media);
    void printVersion(bool verbose);

    RC configure(Settings *settings);
    RC diff(Settings *settings, Monitor *monitor);
    RC stat(Settings *settings, Monitor *monitor);
    RC fsck(Settings *settings, Monitor *monitor);
    RC push(Settings *settings, Monitor *monitor);
    RC storeRuleLocallyThenRemotely(Rule *rule, Settings *settings, Monitor *monitor);
    RC storeRuleRemotely(Rule *rule, Settings *settings, Monitor *monitor);
    RC pull(Settings *settings, Monitor *monitor);
    RC prune(Settings *settings, Monitor *monitor);

    RC umountDaemon(Settings *settings);

    RC mountBackupDaemon(Settings *settings);
    RC mountBackup(Settings *settings, Monitor *monitor);
    RC umountBackup(Settings *settings);

    RC mountRestoreDaemon(Settings *settings, Monitor *monitor);
    RC mountRestore(Settings *settings, Monitor *monitor);
    RC umountRestore(Settings *settings);

    RC shell(Settings *settings, Monitor *monitor);
    RC importMedia(Settings *settings, Monitor *monitor);
    RC indexMedia(Settings *settings, Monitor *monitor);
    RC serveMedia(Settings *settings, Monitor *monitor);

    RC status(Settings *settings, Monitor *monitor);
    RC monitor(Settings *settings, Monitor *monitor);
    RC store(Settings *settings, Monitor *monitor);
    RC restore(Settings *settings, Monitor *monitor);

    void genAutoComplete(string filename);

    private:

    string argsToVector_(int argc, char **argv, vector<string> *args);
    unique_ptr<Restore> accessSingleStorageBackup_(Argument *storage, // Use the storage to select the storage.
                                                   string pointintime,
                                                   Monitor *monitor,
                                                   FileSystem **out_backup_fs = NULL,
                                                   Path **out_root = NULL);
    vector<NamedRestore> accessMultipleStorageBackup_(Argument *storage, // Use the rule to select the storages.
                                                       string pointintime,
                                                       Monitor *monitor,
                                                       FileSystem **out_backup_fs = NULL,
                                                       Path **out_root = NULL);
    RC mountRestoreInternal_(Settings *settings, bool daemon, Monitor *monitor);
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
