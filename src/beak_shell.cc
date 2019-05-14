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
#include "system.h"

static ComponentId SHELL = registerLogComponent("shell");

RC BeakImplementation::shell(Settings *settings, Monitor *monitor)
{
    RC rc = RC::OK;

    assert(settings->from.type == ArgStorage);

    string storage = "";
    if (settings->from.type == ArgStorage) {
        storage = settings->from.storage->storage_location->str();
    } else if (settings->from.type == ArgDir) {
        storage = settings->from.dir->str();
    }

    Path *mount = local_fs_->mkTempDir("beak_shell_");
    Path *stop = local_fs_->mkTempFile("beak_shell_stop_", "echo Unmounting backup "+storage);
    Path *start = local_fs_->mkTempFile("beak_shell_start_",
                                        "trap "+stop->str()+" EXIT"
                                        +"; cd "+mount->str()
                                        +"; echo Mounted "+storage
                                        +"; echo Exit shell to unmount backup.\n");
    FileStat fs;
    fs.setAsExecutable();
    local_fs_->chmod(start, &fs);
    local_fs_->chmod(stop, &fs);

    settings->to.type = ArgDir;
    settings->to.dir = mount;
    settings->fuse_args.push_back(mount->str());
    settings->updateFuseArgsArray();

    rc = mountRestore(settings);
    if (rc.isErr()) goto cleanup;

    rc = sys_->invokeShell(start);

    rc = umountRestore(settings);

cleanup:

    local_fs_->deleteFile(start);
    local_fs_->deleteFile(stop);
    local_fs_->rmDir(mount);

    return rc;
}
