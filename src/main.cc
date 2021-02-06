/*
 Copyright (C) 2016-2020 Fredrik Öhrström

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

#include<sys/resource.h>
#include <stdio.h>
#include <unistd.h>

using namespace std;

int run(int argc, char *argv[]);

int main(int argc, char *argv[])
{
    try {
        return run(argc, argv);
    }
    catch (std::exception &e) {
        fprintf(stderr, "beak: Internal error due to C++ exception >%s<.\n", e.what());
        // The catch/print is necessary for winapi hosts, since
        // a thrown exception is not necessarily printed! The program
        // just silently terminates. If only we could print the stack trace here...
    }
}

int setStackSize()
{
    const rlim_t kStackSize = 32 * 1024 * 1024;   // min stack size = 32 MB
    struct rlimit rl;
    int result;

    result = getrlimit(RLIMIT_STACK, &rl);
    if (result == 0)
    {
        if (rl.rlim_cur < kStackSize)
        {
            rl.rlim_cur = kStackSize;
            result = setrlimit(RLIMIT_STACK, &rl);
            if (result != 0)
            {
                fprintf(stderr, "setrlimit returned result = %d\n", result);
            }
        }
    }

    // ...

    return 0;
}

int run(int argc, char *argv[])
{
    RC rc = RC::OK;

    setStackSize();

    // First create the OS interface to invoke external commands like rclone and rsync.
    auto sys = newSystem();
    // Next create the interface to the local file system where we find:
    // the orgin files and directories, the beak configuration file, the rclone configuration file,
    // and the temporary/cache files.
    auto local_fs = newDefaultFileSystem(sys.get());
    // Then create the interface to hide the differences between different storages types:
    // ie rclone, rsync and local file system.
    auto storage_tool = newStorageTool(sys, local_fs);
    // Then create the interface to restore and read files from the origin.
    // Here we backup the local fs, but we could backup any virtual filesystem
    // for example one exported by a camera app.
    auto origin_tool = newOriginTool(sys, local_fs);
    // Lookup the config file early, if not specified use the default ~/.config/beak/beak.conf
    Path *beak_conf = findBeakConf(argc, argv, configurationFile());
    // Setup logging early, we can now debug command line parsing and configuration loading.
    findAndSetLogging(argc, argv);

    // Fetch the beak configuration from
    auto configuration = newConfiguration(sys, local_fs, beak_conf);
    configuration->load();

    // Now create the beak backup software.
    auto beak = newBeak(configuration, sys, local_fs, storage_tool, origin_tool);

    beak->captureStartTime();

    // Configure the settings by parsing the command line and extract the command.
    Settings settings;
    Command cmd = beak->parseCommandLine(argc, argv, &settings);

    // The monitor is used to display continuous updates on the terminal
    // when beak is performing stores/restores/cache downloads.
    // It also stores the information in the directory /tmp/beak_user_monitor
    auto monitor = newMonitor(sys.get(), local_fs.get(), settings.progress);

    // We now know the command the user intends to invoke.
    switch (cmd)
    {

    case bmount_cmd:
        rc = beak->mountBackupDaemon(&settings);
        break;

    case config_cmd:
        rc = beak->configure(&settings);
        break;

    case diff_cmd:
        rc = beak->diff(&settings, monitor.get());
        break;

    case fsck_cmd:
        rc = beak->fsck(&settings, monitor.get());
        break;

    case genautocomplete_cmd:
        if (settings.from.file == NULL) {
            beak->genAutoComplete("/etc/bash_completion.d/beak");
            printf("Wrote /etc/bash_completion.d/beak\n");
        } else {
            beak->genAutoComplete(settings.from.file->str());
            printf("Wrote %s\n", settings.from.file->c_str());
        }
        break;

   case genmounttrigger_cmd:
        break;

    case prune_cmd:
        rc = beak->prune(&settings, monitor.get());
        break;

    case push_cmd:
        rc = beak->push(&settings, monitor.get());
        break;

    case pushd_cmd:
        settings.delta = true;
        rc = beak->push(&settings, monitor.get());
        break;

    case pull_cmd:
        rc = beak->pull(&settings, monitor.get());
        break;

    case monitor_cmd:
        rc = beak->monitor(&settings, monitor.get());
        break;

    case mount_cmd:
        rc = beak->mountRestoreDaemon(&settings, monitor.get());
        break;

    case restore_cmd:
        rc = beak->restore(&settings, monitor.get());
        break;

    case shell_cmd:
        rc = beak->shell(&settings, monitor.get());
        break;

    case importmedia_cmd:
        rc = beak->importMedia(&settings, monitor.get());
        break;

    case indexmedia_cmd:
        rc = beak->indexMedia(&settings, monitor.get());
        break;

    case servemedia_cmd:
        rc = beak->serveMedia(&settings, monitor.get());
        break;

    case status_cmd:
        rc = beak->status(&settings, monitor.get());
        break;

    case store_cmd:
        rc = beak->store(&settings, monitor.get());
        break;

    case stored_cmd:
        settings.delta = true;
        rc = beak->store(&settings, monitor.get());
        break;

    case umount_cmd:
        rc = beak->umountDaemon(&settings);
        break;

    case version_cmd:
        beak->printVersion(settings.verbose);
        break;

    case help_cmd:
        beak->printHelp(settings.verbose, settings.help_me_on_this_cmd);
        break;

    case nosuch_cmd:
        break;
    }

    return rc.toInteger();
}
