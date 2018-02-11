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
#include "filesystem.h"
#include "log.h"

#include<stdio.h>

using namespace std;

int main(int argc, char *argv[])
{
    RCC rcc = RCC::OKK;
    int rc = 0;
    Options settings;

    auto fs_src = newDefaultFileSystem();
    auto fs_dst = newDefaultFileSystem();
    auto beak = newBeak(fs_src, fs_dst);

    beak->captureStartTime();
    Command cmd = beak->parseCommandLine(argc, argv, &settings);

    switch (cmd) {

    case check_cmd:
        break;

    case checkout_cmd:
        break;

    case config_cmd:
        rcc = beak->configure(&settings);
        break;

    case diff_cmd:
        break;

    case info_cmd:
        rcc = beak->printInfo(&settings);
        break;

    case genautocomplete_cmd:
        if (settings.from.path == NULL) {
            beak->genAutoComplete("/etc/bash_completion.d/beak");
            printf("Wrote /etc/bash_completion.d/beak\n");
        } else {
            beak->genAutoComplete(settings.from.path->str());
            printf("Wrote %s\n", settings.from.path->c_str());
        }
        break;

   case genmounttrigger_cmd:
        break;

    case history_cmd:
        break;

    case mount_cmd:
        rc = beak->mountForwardDaemon(&settings);
        break;

    case prune_cmd:
        rcc = beak->prune(&settings);
        break;

    case push_cmd:
        rcc = beak->push(&settings);
        break;

    case pull_cmd:
        break;

    case remount_cmd:
        rc = beak->remountReverseDaemon(&settings);
        break;

    case restore_cmd:
        rc = beak->restoreReverse(&settings);
        break;

    case shell_cmd:
        rc = beak->shell(&settings);
        break;

    case status_cmd:
        rc = beak->status(&settings);
        break;

    case store_cmd:
        rcc = beak->storeForward(&settings);
        break;

    case umount_cmd:
        rcc = beak->umountDaemon(&settings);
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

    printf("%d\n", rc);
    return rcc.toInteger();
}
