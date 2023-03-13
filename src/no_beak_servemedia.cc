/*
 Copyright (C) 2020 Fredrik Öhrström

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
#include "filesystem_helpers.h"
#include "log.h"
#include "storagetool.h"
#include "system.h"

static ComponentId SERVEMEDIA = registerLogComponent("servemedia");

RC BeakImplementation::serveMedia(Settings *settings, Monitor *monitor)
{
    RC rc = RC::ERR;

    assert(settings->from.type == ArgOrigin);

    usageError(SERVEMEDIA, "This version of beak does not have the media import/index/serve features compiled in!\n");

    return rc;
}
