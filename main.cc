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
#include "help.h"
#include "log.h"

int main(int argc, char *argv[])
{
    auto beak = newBeak();
    int rc = 0;
    
    beak->captureStartTime();

    vector<string> args;
    beak->argsToVector(argc, argv, &args);

    Command cmd;
    Options settings{};
    beak->parseCommandLine(&args, &cmd, &settings);
    bool has_history = beak->lookForPointsInTime(&settings);
    
    switch (cmd) {
    case check_cmd:
        break;
    case info_cmd:
        rc = beak->printInfo(&settings);
        break;
    case mount_cmd:
        if (!has_history || settings.forceforward) {
            // src contains your files, to be backed up
            // dst will contain a virtual file system with the backup files.
            rc = beak->mountForward(&settings);
        } else {
            // src has a history of backup files, thus
            // dst will contain a virtual file system with your files.
            rc = beak->mountReverse(&settings);
        }
        break;
    case pack_cmd:
        break;
    case prune_cmd:
        break;
    case push_cmd:
        rc = beak->push(&settings);
        break;
    case pull_cmd:
        break;
    case status_cmd:
        rc = beak->status(&settings);
        break;
    case version_cmd:
        printVersion(beak.get());
        break;
    case help_cmd:
        printHelp(beak.get(), settings.help_me_on_this_cmd);
        break;
    case nosuch_cmd:
        break;
    }
    
    return rc;
}
