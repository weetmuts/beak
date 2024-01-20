/*
 Copyright (C) 2023 Fredrik Öhrström

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
#include "backup.h"
#include "log.h"
#include "origintool.h"
#include "storagetool.h"

#include<algorithm>

static ComponentId STASH = registerLogComponent("stash");

RC BeakImplementation::stash(Settings *settings, Monitor *monitor)
{
    RC rc = RC::OK;
    Path *cwd = sys_->cwd();
    string cwds = cwd->str();

    std::replace( cwds.begin(), cwds.end(), '/', '_');
    cwds = string("stash_")+cwds;

    Path *stash = cacheDir()->append(cwds);
    local_fs_->mkDirpWriteable(stash);

    if (settings->diff)
    {
        settings->from.type = ArgOrigin;
        settings->from.origin = cwd;

        Storage storage;
        storage.type = FileSystemStorage;
        storage.storage_location = stash;
        settings->to.type = ArgStorage;
        settings->to.storage = &storage;

        rc = diff(settings, monitor);

        return rc;
    }

    settings->from.type = ArgOrigin;
    settings->from.origin = cwd;
    Storage storage;
    storage.type = FileSystemStorage;
    storage.storage_location = stash;
    settings->to.type = ArgStorage;
    settings->to.storage = &storage;

    rc = store(settings, monitor);

    info(STASH, "Stashed into %s\n", stash->c_str());

    return rc;
}
