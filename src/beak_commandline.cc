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
#include "beak_implementation.h"
#include "log.h"
#include "origintool.h"

using namespace std;

static ComponentId COMMANDLINE = registerLogComponent("commandline");

CommandEntry *BeakImplementation::parseCommand(string s)
{
    if (commands_.count(s) == 0) return NULL;
    return commands_[s];
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

Argument BeakImplementation::parseArgument(string arg, ArgumentType expected_type, Settings *settings, Command cmd)
{
    Argument argument;

    assert(expected_type != ArgUnspecified && expected_type != ArgNone);

    auto at = arg.find_last_of('@');
    auto colon = arg.find(':');
    if (at != string::npos && (colon == string::npos || at > colon))
    {
        // An @ sign after the colon is a reference to a point in time.
        // Example @0 in: s3_work_crypt:@0
        //            in: user@backupserver:/backups@0
        //            in: /media/you/USBDevice@0
        auto point = arg.substr(at);
        arg = arg.substr(0,at);
        if (expected_type != ArgStorage
            && expected_type != ArgStorageOrRule
            && expected_type != ArgORS
            && expected_type != ArgNORS) {
            error(COMMANDLINE, "A point in time must only be suffixed to a storage or rule.\n");
        }
        argument.point_in_time = point;
        debug(COMMANDLINE, "found point in time (%s) after storage %s\n", point.c_str(), arg.c_str());
    }

    // Check if the argument is a directory.
    if (expected_type == ArgDir)
    {
        Path *dir = Path::lookup(arg);
        Path *rp = dir->realpath();
        if (!rp)
        {
            usageError(COMMANDLINE, "Expected directory. Got \"%s\" instead.\n", arg.c_str());
        }
        argument.dir = rp;
        argument.type = ArgDir;
        debug(COMMANDLINE, "found directory arg \"%s\", as expected.\n", dir->c_str());
        return argument;
    }

    // Check if the argument is a file.
    if (expected_type == ArgFile
        || expected_type == ArgFileOrNone)
    {
        Path *file = Path::lookup(arg);
        Path *rp = file->realpath();
        if (!rp)
        {
            usageError(COMMANDLINE, "Expected file. Got \"%s\" instead.\n", arg.c_str());
        }
        argument.file = rp;
        argument.type = ArgFile;
        debug(COMMANDLINE, "found file arg \"%s\", as expected.\n", file->c_str());
        return argument;
    }

    // Check if the argument is a storage.
    if (expected_type == ArgORS
        || expected_type == ArgStorage
        || expected_type == ArgStorageOrRule)
    {
        Path *storage_location = Path::lookup(arg);
        Storage *storage = configuration_->findStorageFrom(storage_location);
        if (!storage && cmd == store_cmd) {
            // If we are storing, then try to create a missing directory.
            storage = configuration_->createStorageFrom(storage_location);
        }
        if (storage) {
            argument.type = ArgStorage;
            argument.storage = storage;

            switch (storage->type) {
            case FileSystemStorage: debug(COMMANDLINE, "storage \"%s\" parsed as directory.\n", arg.c_str()); break;
            case RCloneStorage: debug(COMMANDLINE, "storage \"%s\" parsed as rclone.\n", arg.c_str()); break;
            case RSyncStorage: debug(COMMANDLINE, "storage \"%s\" parsed as rsync.\n", arg.c_str()); break;
            case NoSuchStorage: break;
            }
            debug(COMMANDLINE, "found storage arg \"%s\", as expected.\n", storage_location->c_str());
            return argument;
        }

        if (expected_type == ArgStorage)
        {
            usageError(COMMANDLINE, "Expected storage, but \"%s\" is not a storage location.\n", arg.c_str());
        }

        // Not a storage, thus ArgORS will pass through here, to try origin and rule.
    }

    // Check if the argument is a rule.
    if (expected_type == ArgORS
        || expected_type == ArgStorageOrRule
        || expected_type == ArgRule
        || expected_type == ArgRuleOrNone)
    {
        Rule *rule = configuration_->rule(arg);

        if (rule) {
            argument.rule = rule;
            argument.origin = argument.rule->origin_path;
            argument.type = ArgRule;
            debug(COMMANDLINE, "found rule arg %s pointing to origin %s\n",
                  arg.c_str(), argument.rule->origin_path->c_str());
            return argument;
        }

        if (expected_type == ArgRule ||
            expected_type == ArgRuleOrNone ||
            expected_type == ArgStorageOrRule)
        {
            // We expected a rule, but there was none....
            usageError(COMMANDLINE, "Expected a rule. Got \"%s\" instead.\n", arg.c_str());
        }
    }


    // Check if argument is an origin.
    if (expected_type == ArgOrigin ||
        expected_type == ArgORS)
    {
        // If there is no rule, then we expect an origin directory.
        Path *origin = Path::lookup(arg);
        Path *rp = origin->realpath();
        if (rp) {
            if (hasPointsInTime_(rp, origin_tool_->fs()) && !settings->yesorigin) {
                error(COMMANDLINE, "You passed a storage location as an origin. If this is what you want add --yes-origin\n");
            }
            argument.origin = rp;
            argument.type = ArgOrigin;
            debug(COMMANDLINE, "found origin arg \"%s\".\n", origin->c_str());
            return argument;
        }

        if (expected_type == ArgOrigin)
        {
            usageError(COMMANDLINE, "Expected an origin. Got \"%s\" instead.\n", arg.c_str());
        }
    }

    // Check if argument is a command name.
    if (expected_type == ArgNC)
    {
        CommandEntry *cmde = parseCommand(arg.c_str());
        Command cmd = nosuch_cmd;
        if (cmde != NULL) cmd = cmde->cmd;
        if (cmd == nosuch_cmd) {
            usageError(COMMANDLINE, "Expected command. Got \"%s\" instead.\n", arg.c_str());
        }
        argument.type = ArgCommand;
        argument.command = cmd;
        return argument;
    }

    usageError(COMMANDLINE, "Not what I expected, got \"%s\".\n", arg.c_str());

    return argument;
}


const char *arg_name_(ArgumentType at) {

    switch (at) {

    case ArgUnspecified:
        return "?";
    case ArgNone:
        return "no";
    case ArgOrigin:
        return "origin";
    case ArgRule:
        return "rule";
    case ArgRuleOrNone:
        return "rule or none";
    case ArgStorage:
        return "storage";
    case ArgStorageOrRule:
        return "storage or rule";
    case ArgDir:
        return "dir";
    case ArgFile:
        return "file";
    case ArgFileOrNone:
        return "file or none";
    case ArgORS:
        return "origin, rule or storage";
    case ArgNORS:
        return "?";
    case ArgCommand:
        return "command";
    case ArgNC:
        return "?";
    }
    return "?";
}

Command BeakImplementation::parseCommandLine(int argc, char **argv, Settings *settings)
{
    vector<string> args;
    argsToVector_(argc, argv, &args);

    settings->help_me_on_this_cmd = nosuch_cmd;
    settings->fuse_args.push_back("beak"); // Application name
    settings->pointintimeformat = both_point;

    if (args.size() < 1) return nosuch_cmd;

    CommandEntry *cmde = parseCommand(args[0]);
    Command cmd = nosuch_cmd;
    if (cmde != NULL) cmd = cmde->cmd;

    if (cmd == nosuch_cmd)
    {
        if (args[0] == "")
        {
            cmd = help_cmd;
            return cmd;
        }
        usageError(COMMANDLINE, "No such command \"%s\"\n", args[0].c_str());
    }
    settings->depth = 2; // Default value

    auto i = args.begin();
    i = args.erase(i);

    bool options_completed = false;
    for (;i != args.end(); ++i)
    {
        int l = i->length();
        if (!l) continue;

        if (*i == "--") {
            options_completed = true;
            continue;
        }

        if (!options_completed)
        {
            bool contains_value;
            string value;
            OptionEntry *ope = parseOption(*i, &contains_value, &value);
            Option op = ope->option;
            if (op != nosuch_option) {
                if ((ope->type == OptionType::LOCAL_PRIMARY ||
                     ope->type == OptionType::LOCAL_SECONDARY)
                    && !hasCommandOption(cmd, op)) {
                    usageError(COMMANDLINE, "You cannot use option: --%s with the command: %s.\n",
                               ope->name, cmde->name);
                }
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
            case background_option:
                settings->background = true;
                break;
            case cache_option:
                settings->cache = value;
                break;
            case contentsplit_option:
                settings->contentsplit.push_back(value);
                break;
            case deepcheck_option:
                settings->deepcheck = true;
                break;
            case depth_option:
                settings->depth = atoi(value.c_str());
                settings->depth_supplied = true;
                if ((cmd == store_cmd || cmd == bmount_cmd) && settings->depth < 1) {
                    error(COMMANDLINE, "For store/bmount depth (-d) cannot be set to "
                          "less than 1, ie the root.\n");
                }
                if ((cmd == diff_cmd) && settings->depth < 0) {
                    error(COMMANDLINE, "For diff depth (-d) cannot be set to "
                          "less than 0, ie the root.\n");
                }
                break;
            case dryrun_option:
                settings->dryrun = true;
                settings->dryrun_supplied = true;
                break;
            case foreground_option:
                settings->foreground = true;
                break;
            case fusedebug_option:
                settings->fusedebug = true;
                break;
            case include_option:
                settings->include.push_back(value);
                break;
            case keep_option:
                settings->keep = value;
                settings->keep_supplied = true;
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
            case monitor_option:
                settings->monitor = true;
                break;
            case now_option:
                settings->now = value;
                settings->now_supplied = true;
                break;
            case pointintimeformat_option:
                if (value == "absolute") settings->pointintimeformat = absolute_point;
                else if (value == "relative") settings->pointintimeformat = relative_point;
                else if (value == "both") settings->pointintimeformat = both_point;
                else {
                    error(COMMANDLINE, "No such point in time format \"%s\".\n", value.c_str());
                }
                break;
            case progress_option:
                if (value == "none") settings->progress = ProgressDisplayType::None;
                else if (value == "normal") settings->progress = ProgressDisplayType::Normal;
                else if (value == "plain") settings->progress = ProgressDisplayType::Plain;
                else if (value == "top") settings->progress = ProgressDisplayType::Top;
                else {
                    error(COMMANDLINE, "No such progress display type \"%s\".\n", value.c_str());
                }
                break;
            case relaxtimechecks_option:
                settings->relaxtimechecks = true;
                break;
            case tarheader_option:
            {
                if (value == "none") settings->tarheader = TarHeaderStyle::None;
                else if (value == "simple") settings->tarheader = TarHeaderStyle::Simple;
                else if (value == "full") settings->tarheader = TarHeaderStyle::Full;
                else {
                    error(COMMANDLINE, "No such tar header style \"%s\".\n", value.c_str());
                }
                settings->tarheader_supplied = true;
            }
            break;
            case targetsize_option:
            {
                size_t parsed_size;
                RC rc = parseHumanReadable(value.c_str(), &parsed_size);
                if (rc.isErr())
                {
                    error(COMMANDLINE,
                          "Cannot set target size because \"%s\" is not a proper number (e.g. 1,2K,3M,4G,5T).\n",
                          value.c_str());
                }
                settings->targetsize = parsed_size;
                settings->targetsize_supplied = true;
            }
            break;
            case trace_option:
                settings->trace = true;
                setLogLevel(TRACE);
                break;
            case triggersize_option:
            {
                size_t parsed_size;
                RC rc = parseHumanReadable(value.c_str(), &parsed_size);
                if (rc.isErr())
                {
                    error(COMMANDLINE,
                          "Cannot set trigger size because \"%s\" is not a proper number (e.g. 1,2K,3M,4G,5T)\n",
                          value.c_str());
                }
                settings->triggersize = parsed_size;
                settings->triggersize_supplied = true;
            }
            break;
            case splitsize_option:
            {
                size_t parsed_size;
                RC rc = parseHumanReadable(value.c_str(), &parsed_size);
                if (rc.isErr())
                {
                    error(COMMANDLINE,
                          "Cannot set split size because \"%s\" is not a proper number (e.g. 1,2K,3M,4G,5T)\n",
                          value.c_str());
                }
                // Mask the lowest 9 bits to make the split size a multiple of 512.
                size_t masked_size = parsed_size & ~((size_t)0x1ff);
                if (masked_size != parsed_size) {
                    fprintf(stderr, " FROM %zu to %zu\n", parsed_size, masked_size);
                }
                settings->splitsize = parsed_size;
                settings->splitsize_supplied = true;
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
            case yesorigin_option:
                settings->yesorigin = true;
                break;
            case yesprune_option:
                settings->yesprune = true;
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

        if (options_completed)
        {
            if (settings->from.type == ArgUnspecified)
            {
                settings->from = parseArgument(*i, cmde->expected_from, settings, cmd);
            }
            else if (settings->to.type == ArgUnspecified)
            {
                settings->to = parseArgument(*i, cmde->expected_to, settings, cmd);
                if (settings->to.type == ArgOrigin)
                {
                    settings->fuse_args.push_back(settings->to.origin->c_str());
                }
                if (settings->to.type == ArgDir)
                {
                    settings->fuse_args.push_back(settings->to.dir->c_str());
                }
            }
            else {
                error(COMMANDLINE, "Superfluous argument %s\n", (*i).c_str());
            }
        }
    }

    if (cmde->expected_from != ArgNone
        && cmde->expected_from != ArgFileOrNone
        && cmde->expected_from != ArgRuleOrNone
        && cmde->expected_from != ArgNC )
    {
        if (settings->from.type == ArgUnspecified)
        {
            error(COMMANDLINE, "Command expects %s as first argument.\n", arg_name_(cmde->expected_from));
        }
        if (cmde->expected_to != ArgNone)
        {
            if (settings->to.type == ArgUnspecified)
            {
                error(COMMANDLINE, "Command expects %s as second argument.\n", arg_name_(cmde->expected_to));
            }
        }
    }
    if (cmde->expected_from == ArgNC && settings->from.type == ArgCommand)
    {
        settings->help_me_on_this_cmd = settings->from.command;
    }
    settings->updateFuseArgsArray();

    return cmd;
}
