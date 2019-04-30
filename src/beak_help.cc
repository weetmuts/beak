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
#include "version.h"

const char *argName(ArgumentType at) {
    switch (at)
    {
    case ArgUnspecified:
    case ArgNone: return "";
    case ArgOrigin: return "<origin>";
    case ArgRule: return "<rule>";
    case ArgRuleOrNone: return "[<rule>]";
    case ArgStorage: return "<storage>";
    case ArgStorageOrRule: return "<storage>|<rule>";
    case ArgDir: return "<dir>";
    case ArgFile: return "<file>";
    case ArgFileOrNone: return "[<file>]";
    case ArgORS: return "<origin>|<rule>|<storage>";
    case ArgNORS: return "[<origin>|<rule>|<storage>]";
    case ArgCommand: return "<command>";
    case ArgNC: return "[<command>]";
    }
    return "?";
}

void BeakImplementation::printCommands(bool verbose)
{
    fprintf(stdout, "Available Commands:\n");

    size_t max = 0;
    for (auto &e : command_entries_)
    {
        if (verbose == false && e.cmdtype != CommandType::PRIMARY) continue;
        size_t l = strlen(e.name);
        if (l > max) max = l;
    }

    for (auto &e : command_entries_)
    {
        if (e.cmd == nosuch_cmd) continue;
        if (verbose == false && e.cmdtype != CommandType::PRIMARY) continue;
        size_t l = strlen(e.name);
        char verb = ' ';
        if (e.cmdtype == CommandType::SECONDARY) verb = '*';
        fprintf(stdout, "%c %s%-.*s%s\n",
                verb,
                e.name,
                (int)(max-l+4),
                "                                        ",
                e.info);
    }
}

bool isExperimental(OptionEntry &e)
{
    const char *exp = "Experimental!";
    size_t len = strlen(e.info);
    size_t explen = strlen(exp);

    if (len > strlen(exp)) {
        return 0 == strncmp(exp, e.info+len-explen, explen);
    }
    return false;
}

void BeakImplementation::printSettings(Command cmd)
{
    OptionType ot = OptionType::LOCAL;

    string option_header = "Options:\n";
    if (cmd == nosuch_cmd) {
        ot = OptionType::GLOBAL;
        option_header = "Common options for all commands:\n";
    }
    size_t max = 0;
    int num_options = 0;
    for (auto &e : option_entries_)
    {
        if (e.option == nosuch_option) continue;
        if (e.type != ot) continue;
        if (cmd != nosuch_cmd && !hasCommandOption(cmd, e.option)) continue;
        if (isExperimental(e)) continue;
        size_t l = strlen(e.name);
        if (l > max) max = l;
        num_options++;
    }

    if (num_options > 0)
    {
        fprintf(stdout, "%s", option_header.c_str());
    }

    for (auto &e : option_entries_)
    {
        if (e.option == nosuch_option) continue;
        if (e.type != ot) continue;
        if (cmd != nosuch_cmd && !hasCommandOption(cmd, e.option)) continue;
        if (isExperimental(e)) continue;

        string sn = e.shortname;
        size_t sl = strlen(e.shortname);
        if (sl > 0) {
            sn = string("-")+e.shortname;
            sl++;
        }

        string n = e.name;
        size_t l = strlen(e.name);
        if (n[n.length()-1] == '_')
        {
            n = "";
            l = 0;
        }
        else
        {
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

void BeakImplementation::printHelp(bool verbose, Command cmd)
{
    CommandEntry *ce {};
    if (cmd == nosuch_cmd)
    {
        fprintf(stdout,
                "usage: beak <command> [options] [<args>]\n"
                "\n");
        printCommands(verbose);
        fprintf(stdout,"\n");
    }
    else
    {
        ce = commands_from_cmd_[cmd];
        int num_args = 0;
        string help = ce->name;
        if (ce->expected_from != ArgNone) {
            num_args++;
            help += " ";
            help += argName(ce->expected_from);
        }
        if (ce->expected_to != ArgNone) {
            num_args++;
            help += " ";
            help += argName(ce->expected_to);
        }

        fprintf(stdout, "%s\n\nusage: beak %s\n\n", ce->info, help.c_str());
    }
    switch (cmd) {
    case bmount_cmd:
        fprintf(stdout, "Create a backup through a mount. The mounted virtual file system\n"
                "contains the backup.\n\n");
        break;
    case config_cmd:
        fprintf(stdout, "A rule designates an origin directory, the storage locations\n"
                "and their prune rules. Such a rule can then be used with the commands:\n"
                "push, pull, prune, mount and shell.\n\n");
        break;
    case diff_cmd:
        fprintf(stdout, "Display a summary of the differences between the two arguments.\n"
                "The difference is by default grouped on the first subdirectory level.\n"
                "Add -v to show all files.\n"
                "Add -d 1 to do the summary on the root level.\n\n");
        break;
    case fsck_cmd:
        fprintf(stdout, "Add -v to show all missing, superfluous and wrongly sized files.\n");
        break;
    default:
        break;
    }
    printSettings(cmd);
    fprintf(stdout,"\n");
}

void BeakImplementation::printVersion(bool verbose)
{
    fprintf(stdout, "beak version " BEAK_VERSION "\n");

    if (!verbose) return;

    fprintf(stdout, "\n"
            "Copyright (C) 2016-2019 Fredrik Öhrström\n"
            "Licensed to you under the GPLv3 or later (https://www.gnu.org/licenses/gpl-3.0.txt)\n\n"
            "This binary (" BEAK_VERSION ") is built from the source:\n"
            "https://github.com/weetmuts/beak " BEAK_COMMIT "\n"

            #ifdef PLATFORM_WINAPI
            "This build of beak also includes third party code:\n"
            "openssl-1.0.2l - Many authors, see https://www.openssl.org/community/thanks.html\n"
            "https://github.com/openssl/openssl\n\n"
            "zlib-1.2.11 - Jean-loup Gailly and Mark Adler\n"
            "https://www.zlib.net/\n\n"
            "WinFsp - Windows File System Proxy, Copyright (C) Bill Zissimopoulos\n"
            "https://github.com/billziss-gh/winfsp\n"
            #endif
            "\n");
}
