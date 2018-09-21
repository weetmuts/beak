/*
 Copyright (C) 2016-2018 Fredrik Öhrström

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

#include "always.h"
#include "beak.h"
#include "configuration.h"
#include "filesystem.h"
#include "log.h"
#include "origintool.h"
#include "storagetool.h"
#include "system.h"

#include <stdio.h>
#include <unistd.h>

using namespace std;

int run(int argc, char *argv[]);

int main(int argc, char *argv[])
{
    try {
        return run(argc, argv);
    }
    catch (...) {
        fprintf(stderr, "Internal error due to C++ exception.\n");
        // The catch/print is necessary for winapi hosts, since
        // a thrown exception is not necessarily printed! The program
        // just silently terminates. If only we could print the stack trace here...
    }
}

int run(int argc, char *argv[]) {
    RC rc = RC::OK;

    // First create the OS interface to invoke external commands like rclone and rsync.
    auto sys = newSystem();
    // Next create the interface to the file system that stores the beak configuration files
    // and the temporary/cache files.
    auto sys_fs = newDefaultFileSystem();
    // Next create the filesystem that can be used as a storage location,
    // but it might not be used, if storing remotely.
    auto local_storage_fs = newDefaultFileSystem();
    // Next create the filesystem where the origin files are found.
    auto origin_fs = newDefaultFileSystem();
    // Then create the interface to hide the differences between different storages types:
    // ie rclone, rsync and local file system.
    auto storage_tool = newStorageTool(sys, sys_fs, local_storage_fs);
    // Then create the interface to restore and read files from the origin.
    auto origin_tool = newOriginTool(sys, sys_fs, origin_fs);
    // Fetch the beak configuration from ~/.config/beak/beak.conf
    auto configuration = newConfiguration(sys, sys_fs);
    configuration->load();

    // Now create the beak backup software.
    auto beak = newBeak(configuration, sys, sys_fs, storage_tool, origin_tool);

    beak->captureStartTime();

    // Configure the settings by parsing the command line and extract the command.
    Options settings;
    Command cmd = beak->parseCommandLine(argc, argv, &settings);

    // We now know the command the user intends to invoke.
    switch (cmd) {

    case check_cmd:
        rc = beak->check(&settings);
        break;

    case config_cmd:
        rc = beak->configure(&settings);
        break;

    case diff_cmd:
        break;

    case genautocomplete_cmd:
        if (settings.from.dir == NULL) {
            beak->genAutoComplete("/etc/bash_completion.d/beak");
            printf("Wrote /etc/bash_completion.d/beak\n");
        } else {
            beak->genAutoComplete(settings.from.dir->str());
            printf("Wrote %s\n", settings.from.dir->c_str());
        }
        break;

   case genmounttrigger_cmd:
        break;

    case history_cmd:
        break;

    case bmount_cmd:
        rc = beak->mountBackupDaemon(&settings);
        break;

    case prune_cmd:
        rc = beak->prune(&settings);
        break;

    case push_cmd:
        rc = beak->push(&settings);
        break;

    case pull_cmd:
        break;

    case mount_cmd:
        rc = beak->mountRestoreDaemon(&settings);
        break;

    case restore_cmd:
        rc = beak->restore(&settings);
        break;

    case status_cmd:
        rc = beak->status(&settings);
        break;

    case store_cmd:
        rc = beak->store(&settings);
        break;

    case umount_cmd:
        rc = beak->umountDaemon(&settings);
        break;

    case version_cmd:
        beak->printVersion();
        break;

    case help_cmd:
        if (settings.license) {
            beak->printLicense();
        } else {
            beak->printHelp(settings.help_me_on_this_cmd);
        }
        break;

    case nosuch_cmd:
        break;
    }

    return rc.toInteger();
}
