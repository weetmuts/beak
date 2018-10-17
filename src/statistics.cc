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

#include "fit.h"
#include "log.h"
#include "system.h"
#include "util.h"

#include <memory.h>
#include <string.h>

static ComponentId STATISTICS = registerLogComponent("statistics");

using namespace std;

struct StoreStatisticsImplementation : StoreStatistics
{
private:

    Stats copy;

    uint64_t start_time {};

    vector<SecsBytes> secsbytes;

    unique_ptr<ThreadCallback> regular_;

    bool redrawLine();
    void startDisplayOfProgress();
    void updateProgress();
    void finishProgress();
};

void StoreStatisticsImplementation::startDisplayOfProgress()
{
    start_time = clockGetTimeMicroSeconds();

    regular_ = newRegularThreadCallback(1000, [this](){ return redrawLine();});
}

//Tar emot objekt: 100% (814178/814178), 669.29 MiB | 6.71 MiB/s, klart.
//Analyserar delta: 100% (690618/690618), klart.

void StoreStatisticsImplementation::updateProgress()
{
    // Take a snapshot of the stats structure.
    // The snapshot is taken while the regular callback is blocked.
    regular_->doWhileCallbackBlocked([this]() { copy = stats; });
}

// Draw the progress line based on the snapshotted contents in the copy struct.
bool StoreStatisticsImplementation::redrawLine()
{
    if (copy.num_files == 0 || copy.num_files_to_store == 0) return true;
    uint64_t now = clockGetTimeMicroSeconds();
    double secs = ((double)((now-start_time)/1000))/1000.0;
    double bytes = (double)copy.size_files_stored;
    secsbytes.push_back({secs,bytes});
    double bps = bytes/secs;

    int percentage = (int)(100.0*(double)copy.size_files_stored / (double)copy.size_files_to_store);
    string mibs = humanReadableTwoDecimals(copy.size_files_stored);
    string average_speed = humanReadableTwoDecimals(bps);

    string msg = "Full";
    if (copy.num_files > copy.num_files_to_store) {
        msg = "Incr";
    }
    double max_bytes = (double)copy.size_files_to_store;
    double eta_1s_speed, eta_immediate, eta_average; // estimated total time
    double etr;                                      // estimated time remaining
    predict_all(secsbytes, secsbytes.size()-1, max_bytes, &eta_1s_speed, &eta_immediate, &eta_average);

    etr = eta_immediate - secs;

    debug(STATISTICS, "stored(secs,bytes)\t"
          "%.1f\t"
          "%ju\t"
          "%.0f\t"
          "%.0f\t"
          "%.0f\n",
          secs,
          copy.size_files_stored,
          eta_1s_speed,
          eta_immediate,
          eta_average);


    UI::redrawLineOutput("%s store: %d%% (%ju/%ju), %s %s/s| %.1f/(%.0fs,%.0fs,%.0fs) ETR %.0fs",
                         msg.c_str(),
                         percentage, copy.num_files_stored, copy.num_files_to_store, mibs.c_str(),
                         average_speed.c_str(), secs, eta_1s_speed, eta_immediate, eta_average,
                         etr);
    return true;
}

void StoreStatisticsImplementation::finishProgress()
{
    if (stats.num_files == 0 || stats.num_files_to_store == 0) return;
    regular_->stop();
    updateProgress();
    redrawLine();
    UI::output(", done.\n");
}

unique_ptr<StoreStatistics> newStoreStatistics()
{
    return unique_ptr<StoreStatisticsImplementation>(new StoreStatisticsImplementation());
}
