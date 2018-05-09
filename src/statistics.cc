/*
 Copyright (C) 2018 Fredrik Öhrström

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

#include "statistics.h"

#include "log.h"
#include "system.h"
#include "util.h"

#include <memory.h>

//static ComponentId STORAGETOOL = registerLogComponent("storagetool");

using namespace std;

StoreStatistics::StoreStatistics() {
    memset(this, 0, sizeof(StoreStatistics));
    start = prev = clockGetTime();
    info_displayed = false;
}

//Tar emot objekt: 100% (814178/814178), 669.29 MiB | 6.71 MiB/s, klart.
//Analyserar delta: 100% (690618/690618), klart.

void StoreStatistics::displayProgress()
{
    if (num_files == 0 || num_files_to_store == 0) return;
    uint64_t now = clockGetTime();
    if ((now-prev) < 500000 && num_files_to_store < num_files) return;
    prev = now;
    info_displayed = true;
    UI::clearLine();
    int percentage = (int)(100.0*(float)size_files_stored / (float)size_files_to_store);
    string mibs = humanReadableTwoDecimals(size_files_stored);
    float secs = ((float)((now-start)/1000))/1000.0;
    string speed = humanReadableTwoDecimals(((double)size_files_stored)/secs);
    if (num_files > num_files_to_store) {
        UI::output("Incremental store: %d%% (%ju/%ju), %s | %.2f s %s/s ",
                   percentage, num_files_stored, num_files_to_store, mibs.c_str(), secs, speed.c_str());
    } else {
        UI::output("Full store: %d%% (%ju/%ju), %s | %.2f s %s/s ",
                   percentage, num_files_stored, num_files_to_store, mibs.c_str(), secs, speed.c_str());
    }
}

void StoreStatistics::finishProgress()
{
    if (info_displayed == false || num_files == 0 || num_files_to_store == 0) return;
    displayProgress();
    UI::output(", done.\n");
}
