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
#include "backup.h"
#include "log.h"
#include "origintool.h"
#include "storagetool.h"

static ComponentId STATUS = registerLogComponent("status");

extern void update_mctim_maxes(const struct stat *sb);

RC BeakImplementation::status(Settings *settings)
{
    RC rc = RC::OK;
    struct timespec mtim_max {};  /* time of last modification */
    struct timespec ctim_max {};  /* time of last meta data modification */

    assert(settings->from.type == ArgRule || settings->from.type == ArgNone);

    Rule *rule = settings->from.rule;

    info(STATUS, "Scanning %s...", rule->origin_path->c_str());

    memset(&mtim_max, 0, sizeof(mtim_max));
    memset(&ctim_max, 0, sizeof(ctim_max));
    uint64_t start = clockGetTimeMicroSeconds();
    auto progress = newProgressStatistics(settings->progress);
    rc = origin_tool_->fs()->recurse(rule->origin_path,
                                     [=](const char *path, const struct stat *sb) {
                                         update_mctim_maxes(sb);
                                         return RecurseContinue;
                                     });

    uint64_t stop = clockGetTimeMicroSeconds();
    uint64_t store_time = stop - start;

    info(STATUS, "in %jdms.\n", store_time / 1000);

    char most_recent_mtime[20];
    memset(most_recent_mtime, 0, sizeof(most_recent_mtime));
    strftime(most_recent_mtime, 20, "%Y-%m-%d_%H:%M:%S", localtime(&mtim_max.tv_sec));

    char most_recent_ctime[20];
    memset(most_recent_ctime, 0, sizeof(most_recent_ctime));
    strftime(most_recent_ctime, 20, "%Y-%m-%d_%H:%M:%S", localtime(&ctim_max.tv_sec));

    info(STATUS, "mtime=%s ctime=%s\n", most_recent_mtime, most_recent_ctime);

    return rc;
}
