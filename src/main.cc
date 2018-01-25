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

#include "always.h"
#include "beak.h"
#include "filesystem.h"
#include "log.h"

#include<stdio.h>

using namespace std;

int main(int argc, char *argv[])
{
    int rc = 0;
    Command cmd;
    Options settings;

    auto fs_src = newDefaultFileSystem();
    auto fs_dst = newDefaultFileSystem();
    auto beak = newBeak(fs_src.get(), fs_dst.get());

    beak->captureStartTime();
    beak->parseCommandLine(argc, argv, &cmd, &settings);

    switch (cmd) {

    case check_cmd:
        break;

    case checkout_cmd:
        break;

    case config_cmd:
        rc = beak->configure(&settings);
        break;

    case diff_cmd:
        break;

    case info_cmd:
        rc = beak->printInfo(&settings);
        break;

    case genautocomplete_cmd:
        if (settings.src == NULL) {
            beak->genAutoComplete("/etc/bash_completion.d/beak");
            printf("Wrote /etc/bash_completion.d/beak\n");
        } else {
            beak->genAutoComplete(settings.src->str());
            printf("Wrote %s\n", settings.src->c_str());
        }
        break;

    case genmounttrigger_cmd:
        break;

    case history_cmd:
        break;

    case mount_cmd:
    {
        if (!settings.hasBothSrcAndDst()) {
            fprintf(stdout, "You must supply a src and a dst.\n");
            exit(1);
        }
        bool has_history = beak->lookForPointsInTime(&settings);
        if (!has_history || settings.forceforward) {
            // src contains your files, to be backed up
            // dst will contain a virtual file system with the backup files.
            rc = beak->mountForwardDaemon(&settings);
        } else {
            // src has a history of backup files, thus
            // dst will contain a virtual file system with your files.
            rc = beak->mountReverseDaemon(&settings);
        }
    }
    break;

    case prune_cmd:
        break;

    case push_cmd:
        rc = beak->push(&settings);
        break;

    case pull_cmd:
        break;

    case shell_cmd:
        rc = beak->shell(&settings);
        break;

    case status_cmd:
        rc = beak->status(&settings);
        break;

    case store_cmd:
    {
        if (!settings.hasBothSrcAndDst()) {
            fprintf(stdout, "You must supply a src and a dst.\n");
            exit(1);
        }
        bool has_history = beak->lookForPointsInTime(&settings);
        if (!has_history || settings.forceforward) {
            // src contains your files, to be backed up
            // the backup files will be stored in dst
            rc = beak->storeForward(&settings);
        } else {
            // src has a history of backup files, thus
            // store the extracted files into dst.
            rc = beak->storeReverse(&settings);
        }
        break;
    }
    case umount_cmd:
        if (!settings.hasSrc()) {
            fprintf(stdout, "You must supply a directory unmount.\n");
            exit(1);
        }
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

    return rc;
}
