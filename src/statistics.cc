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

struct ProgressStatisticsImplementation : ProgressStatistics
{
    ProgressStatisticsImplementation(ProgressDisplayType t) : display_type_(t) {}
    ~ProgressStatisticsImplementation() = default;

private:

    Stats copy;

    uint64_t start_time {};

    vector<SecsBytes> secsbytes;

    unique_ptr<ThreadCallback> regular_;

    ProgressDisplayType display_type_;

    bool redrawLine();
    void startDisplayOfProgress();
    void updateProgress();
    void finishProgress();
};

void ProgressStatisticsImplementation::startDisplayOfProgress()
{
    start_time = clockGetTimeMicroSeconds();

    regular_ = newRegularThreadCallback(1000, [this](){ return redrawLine();});
}

//Tar emot objekt: 100% (814178/814178), 669.29 MiB | 6.71 MiB/s, klart.
//Analyserar delta: 100% (690618/690618), klart.

void ProgressStatisticsImplementation::updateProgress()
{
    // Take a snapshot of the stats structure.
    // The snapshot is taken while the regular callback is blocked.
    regular_->doWhileCallbackBlocked([this]() {
            copy = stats;
            copy.latest_update = clockGetTimeMicroSeconds();
        });
}

// Draw the progress line based on the snapshotted contents in the copy struct.
bool ProgressStatisticsImplementation::redrawLine()
{
    if (copy.num_files == 0 || copy.num_files_to_store == 0) return true;
    uint64_t now = clockGetTimeMicroSeconds();
    double secs = ((double)((now-start_time)/1000))/1000.0;
    double secs_latest_update = ((double)((copy.latest_update-start_time)/1000))/1000.0;
    double bytes = (double)copy.size_files_stored;
    secsbytes.push_back({secs_latest_update,bytes});

    double bps = bytes/secs_latest_update;

    int percentage = (int)(100.0*(double)copy.size_files_stored / (double)copy.size_files_to_store);
    string mibs = humanReadableTwoDecimals(copy.size_files_to_store);
    string average_speed = humanReadableTwoDecimals(bps);

    string msg = "Full";
    if (copy.num_files > copy.num_files_to_store) {
        msg = "Incr";
    }
    double max_bytes = (double)copy.size_files_to_store;
    double eta_1s_speed, eta_immediate, eta_average; // estimated total time
    predict_all(secsbytes, secsbytes.size()-1, max_bytes, &eta_1s_speed, &eta_immediate, &eta_average);

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

    string elapsed = humanReadableTime(secs, true);
    // Only show the seconds if we are closer than 2 minutes to ending the transfer.
    // The estimate is too uncertain early on and bit silly to show that exact.
    bool show_seconds = ((eta_immediate - secs) < 60*2);
    string estimated_total = "/"+humanReadableTime(eta_immediate, show_seconds);

    if (secs < 60 || percentage == 100) {
        // Do not try to give an estimate until 60 seconds have passed.
        // Do not show the estimate when all bytes are transferred.
        estimated_total = "";
    }
    UI::redrawLineOutput("%s store: %d%% (%ju/%ju) %s %s/s | %s%s",
                         msg.c_str(),
                         percentage, copy.num_files_stored, copy.num_files_to_store,
                         mibs.c_str(), average_speed.c_str(),
                         elapsed.c_str(), estimated_total.c_str());
    return true;
}

void ProgressStatisticsImplementation::finishProgress()
{
    if (stats.num_files == 0 || stats.num_files_to_store == 0) return;
    regular_->stop();
    updateProgress();
    redrawLine();
    UI::output(" done.\n");
}

unique_ptr<ProgressStatistics> newProgressStatistics(ProgressDisplayType t)
{
    return unique_ptr<ProgressStatisticsImplementation>(new ProgressStatisticsImplementation(t));
}
