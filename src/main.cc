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

#include "beak.h"
#include "log.h"
#include "system.h"

static ComponentId MAIN = registerLogComponent("main");

int main(int argc, char *argv[])
{
    auto beak = newBeak();
    Command cmd;
    Options settings;

    int rc = 0;

    auto sys = newSystem();
    
    beak->captureStartTime();
    beak->parseCommandLine(argc, argv, &cmd, &settings);
    bool has_history = beak->lookForPointsInTime(&settings);
    
    switch (cmd) {
    case check_cmd:
        break;
    case config_cmd:
        break;
    case info_cmd:
        rc = beak->printInfo(&settings);
        break;
    case genautocomplete_cmd:
        break;
    case mount_cmd:
        if (!has_history || settings.forceforward) {
            // src contains your files, to be backed up
            // dst will contain a virtual file system with the backup files.
            rc = beak->mountForwardDaemon(&settings);
        } else {
            // src has a history of backup files, thus
            // dst will contain a virtual file system with your files.
            rc = beak->mountReverseDaemon(&settings);
        }
        break;
    case pack_cmd:
        break;
    case prune_cmd:
        break;
    case push_cmd:
    {
        char name[32];
        strcpy(name, "/tmp/beakXXXXXX");
        char *mount = mkdtemp(name);
        if (!mount) {
            error(MAIN, "Could not create temp directory!");
        }
        Options forward_settings;
        forward_settings.src = settings.src;
        forward_settings.dst = Path::lookup(mount);
        forward_settings.fuse_argc = 1;
        char *arg;
        char **argv = &arg;
        *argv = new char[16];
        strcpy(*argv, "beak");
        forward_settings.fuse_argv = argv;
        
        // Spawn virtual filesystem.
        rc = beak->mountForward(&forward_settings);

        std::vector<std::string> args;
        args.push_back("copy");
        args.push_back(mount);
        if (settings.dst) {
            args.push_back(settings.dst->str());
        } else {
            args.push_back(settings.remote);
        }
        rc = sys->invoke("rclone", args);

        // Unmount virtual filesystem.
        rc = beak->unmountForward(&forward_settings);
    }
        break;
    case pull_cmd:
        break;
    case status_cmd:
        rc = beak->status(&settings);
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
    
    return rc;
}
