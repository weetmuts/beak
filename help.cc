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

#include "log.h"
#include "beak.h"

void printHelp(Beak *beak, Command cmd)
{
    switch (cmd) {
    case nosuch_cmd:
        fprintf(stdout,
                "Beak is a mirroring-backup-rotation/retention tool that is\n"
                "designed to co-exist with the (cloud) storage of your choice\n"
                "and allow push/pull to share the backups between multiple client computers.\n"
                "\n"
                "Usage:\n"
                "  beak [command] [options] [src] [dst]\n"
                "\n");
        beak->printCommands();
        fprintf(stdout,"\n");
        beak->printOptions();
        fprintf(stdout,"\n");
        break;
    case mount_cmd:
        fprintf(stdout,
        "Examines the contents in src to determine what kind of virtual filesystem\n"
        "to mount as dst. If src contains your files that you want to backup,\n"
        "then dst will contain beak backup tar files, suitable for rcloning somewhere.\n"
        "If src contains beak backup tar files, then dst will contain your original\n"
        "backed up files.\n"
        "\n"
        "Usage:\n"
            "  beak mount [options] [src] [dst]\n"
            "\n"
        "If you really want to backup the beak backup tar files, then you can override\n"
            "the automatic detection with the --forceforward option.\n");
        break;
    default:
        fprintf(stdout, "Sorry, no help for that command yet.\n");
        break;
    }
}

void printVersion(Beak *beak)
{
    fprintf(stdout, "tarredfs " TARREDFS_VERSION "\n");
}

