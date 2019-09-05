/*
 Copyright (C) 2019 Fredrik Öhrström

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
#include "filesystem.h"
#include "fit.h"
#include "log.h"
#include "system.h"
#include "monitor.h"
#include "ui.h"

#include <string>
#include <signal.h>
#include <map>
#include <vector>

#include <unistd.h>

static ComponentId MONITOR = registerLogComponent("monitor");
static ComponentId STATISTICS = registerLogComponent("statistics");

struct MonitorImplementation : Monitor
{
    unique_ptr<ProgressStatistics> newProgressStatistics(string job);
    void updateJob(pid_t pid, string info);
    string lastUpdate(pid_t pid);
    int startDisplay(function<bool()> regular_cb);
    void stopDisplay(int id);
    ProgressStatistics *getStats();

    bool regularDisplay();
    void doWhileCallbackBlocked(std::function<void()> do_cb);

    MonitorImplementation(System *sys, FileSystem *fs, ProgressDisplayType pdt);
    ~MonitorImplementation() = default;

private:

    void checkSharedDir();

    System *sys_ {};
    FileSystem *fs_ {};
    Path *shared_dir_ {};
    map<int,string> jobs_;
    unique_ptr<ThreadCallback> regular_;
    // A list of functions to call before redrawing the monitor.
    vector<function<bool()>> redraws_;
    map<pid_t,string> updates_;
    ProgressDisplayType pdt_;
};

unique_ptr<Monitor> newMonitor(System *sys, FileSystem *fs, ProgressDisplayType pdt) {
    return unique_ptr<Monitor>(new MonitorImplementation(sys, fs, pdt));
}

MonitorImplementation::MonitorImplementation(System *s, FileSystem *fs, ProgressDisplayType pdt) : sys_(s), fs_(fs), pdt_(pdt)
{
}

unique_ptr<ProgressStatistics> newwProgressStatistics(ProgressDisplayType t, Monitor *monitor, std::string job);

unique_ptr<ProgressStatistics>
MonitorImplementation::newProgressStatistics(string job)
{
    return newwProgressStatistics(pdt_, this, job);
}

void MonitorImplementation::checkSharedDir()
{
    Path *tmp = Path::lookup("/dev/shm");
    string shd;
    strprintf(shd, "beak-%s", sys_->userName().c_str());
    shared_dir_ = tmp->append(shd);
    FileStat stat;
    RC rc = fs_->stat(shared_dir_, &stat);
    if (rc.isErr())
    {
        // Directory does not exist, lets create it.
        fs_->mkDir(shared_dir_, "", 0700);
    }
    else
    {
        // Something is there...
        if (!stat.isDirectory())
        {
            error(MONITOR, "Expected \"%s\" to be a directory or not exist!\n", shared_dir_->c_str());
        }
        if ((stat.st_mode & 0777) != 0700)
        {
            error(MONITOR, "Expected \"%s\" to be accessible only by you!\n", shared_dir_->c_str());
        }
        // We ignore group sharing for the moment.
        if (stat.st_uid != geteuid())
        {
            error(MONITOR, "Expected \"%s\" to owned by you!\n", shared_dir_->c_str());
        }
    }
}

void MonitorImplementation::updateJob(pid_t pid, string info)
{
    checkSharedDir();

    updates_[pid] = info;
    string nr = "";
    strprintf(nr, "%d", pid);
    Path *file = Path::lookup(nr);
    file = file->prepend(shared_dir_);

    std::vector<char> data(info.begin(), info.end());

    fs_->createFile(file, &data);
}

string MonitorImplementation::lastUpdate(pid_t pid)
{
    if (updates_.count(pid) != 0) {
        return updates_[pid];
    }
    return "";
}

int MonitorImplementation::startDisplay(function<bool()> progress_cb)
{
    setbuf(stdout, NULL);
    checkSharedDir();
    redraws_.push_back(progress_cb);
    if (!regular_)
    {
        regular_ = newRegularThreadCallback(1000, [this](){ return regularDisplay();});
    }
    return 0;
}

void MonitorImplementation::stopDisplay(int id)
{
    redraws_.pop_back();
}

bool MonitorImplementation::regularDisplay()
{
    for (auto &cb : redraws_)
    {
        cb();
    }
    /*
    RC rc = RC::OK;
    vector<Path*> ps;
    bool ok = fs_->readdir(shared_dir_, &ps);
    if (!ok) return true;
    string s;
    for (auto p : ps)
    {
        FileStat st;
        int pid = atoi(p->c_str());
        if (pid <= 0) continue;
        if (!sys_->processExists(pid)) continue;
        Path *pp = p->prepend(shared_dir_);
        rc = fs_->stat(pp, &st);
        if (rc.isErr()) continue;
        if (!st.isRegularFile()) continue;
        vector<char> content;
        RC rc = fs_->loadVector(pp, 1024, &content);
        if (rc.isOk())
        {
            string tmp = string(content.begin(), content.end());
            while (tmp.back() == '\n' || tmp.back() == ' ') tmp.pop_back();
            while (tmp.length() < 32) tmp = tmp.append(" ");
            s.append(tmp);
            s.append("\n");
        }
    }

    switch (pdt_) {
    case ProgressDisplayType::None: break;
        printf(".");
        break;
    case ProgressDisplayType::Plain: break;
        UI::clearLine();
        printf("\033[0;37;1m\033[44m%s", s.c_str());
        break;
    case ProgressDisplayType::Ansi:
        UI::storeCursor();
        UI::moveTopLeft();
        printf("\033[0;37;1m\033[44m%s", s.c_str());
        UI::restoreCursor();
        break;
    }
    */
    return true;
}

void MonitorImplementation::doWhileCallbackBlocked(function<void()> do_cb)
{
    if (!regular_)
    {
        do_cb();
    }
    else
    {
        regular_->doWhileCallbackBlocked(do_cb);
    }
}

using namespace std;

struct ProgressStatisticsImplementation : ProgressStatistics
{
    ProgressStatisticsImplementation(ProgressDisplayType t, Monitor *m, string job) : pdt_(t), monitor_(m), job_(job) {}
    ~ProgressStatisticsImplementation() = default;
    void setProgress(string msg);

private:

    Stats copy;

    uint64_t start_time {};

    vector<SecsBytes> secsbytes;

    int rotate_ {};
    ProgressDisplayType pdt_ {};
    Monitor *monitor_ {};
    string job_;

    bool redrawLine();
    void startDisplayOfProgress();
    void updateStatHint(size_t s);
    void updateProgress();
    void finishProgress();

};

const char *spinner_[] = { "⠋", "⠙", "⠹", "⠸", "⠼", "⠴", "⠦", "⠧", "⠇", "⠏" };

void ProgressStatisticsImplementation::startDisplayOfProgress()
{
    start_time = clockGetTimeMicroSeconds();
    monitor_->startDisplay([this](){ return redrawLine();});
}

//Tar emot objekt: 100% (814178/814178), 669.29 MiB | 6.71 MiB/s, klart.
//Analyserar delta: 100% (690618/690618), klart.
void ProgressStatisticsImplementation::updateStatHint(size_t s)
{
    stats.stat_size_files_transferred = s;
    stats.latest_stat = clockGetTimeMicroSeconds();
}

void ProgressStatisticsImplementation::updateProgress()
{
    // Take a snapshot of the stats structure.
    // The snapshot is taken while the regular callback is blocked.
    monitor_->doWhileCallbackBlocked([this]() {
            copy = stats;
            copy.latest_update = clockGetTimeMicroSeconds();
        });
}

void ProgressStatisticsImplementation::setProgress(string msg)
{
    string info;
    strprintf(info, "%s | %s", job_.c_str(), msg.c_str());
    monitor_->updateJob(getpid(), info);
}

// Draw the progress line based on the snapshotted contents in the copy struct.
bool ProgressStatisticsImplementation::redrawLine()
{
    if (copy.num_files == 0 || copy.num_files_to_store == 0) return true;
    uint64_t now = clockGetTimeMicroSeconds();
    double secs = ((double)((now-start_time)/1000))/1000.0;

    double secs_latest_update = ((double)((copy.latest_update-start_time)/1000))/1000.0;
    double bytes = (double)copy.size_files_stored;

    /*
    // The stats from rclone are not useful in the beginning, where they are needed....
    if (stats.latest_stat > copy.latest_update) {
        secs_latest_update = ((double)((stats.latest_stat-start_time)/1000))/1000.0;
        bytes = (double)stats.stat_size_files_transferred;
    }*/
    secsbytes.push_back({secs_latest_update,bytes});

    double bps = bytes/secs_latest_update;

    int percentage = (int)(100.0*(double)copy.size_files_stored / (double)copy.size_files_to_store);
    string mibs = humanReadableTwoDecimals(copy.size_files_to_store);
    string average_speed = humanReadableTwoDecimals(bps);

    if (bytes == 0) {
        average_speed = spinner_[rotate_];
        rotate_++;
        if (rotate_>9) rotate_ = 0;
    }
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
    string info;
    strprintf(info, "%s store: %s %2d" "%%" " (%ju/%ju) %s/s | %s%s",
              msg.c_str(), mibs.c_str(),
              percentage, copy.num_files_stored, copy.num_files_to_store,
              average_speed.c_str(),
              elapsed.c_str(), estimated_total.c_str());

    string jobinfo;
    strprintf(jobinfo, "%s | %s",
              job_.c_str(),
              info.c_str());

    monitor_->updateJob(getpid(), jobinfo);

    switch (pdt_) {
    case ProgressDisplayType::None:
        break;
    case ProgressDisplayType::Normal:
        UI::clearLine();
        printf("%s", info.c_str());
        break;
    case ProgressDisplayType::Plain:
        printf("%s\n", info.c_str());
        break;
    case ProgressDisplayType::Top:
        UI::storeCursor();
        UI::moveTopLeft();
        printf("\033[0;37;1m\033[44m %s", info.c_str());
        UI::restoreCursor();
        break;
    default:
        assert(0);
    }

    return true;
}

void ProgressStatisticsImplementation::finishProgress()
{
    if (stats.num_files == 0 || stats.num_files_to_store == 0) return;
    updateProgress();
    redrawLine();

    switch (pdt_) {
    case ProgressDisplayType::None:
    case ProgressDisplayType::Top:
        break;
    case ProgressDisplayType::Plain:
    case ProgressDisplayType::Normal:
        UI::output(" done.\n");
    }
}

unique_ptr<ProgressStatistics> newwProgressStatistics(ProgressDisplayType t, Monitor *monitor, std::string job)
{
    return unique_ptr<ProgressStatisticsImplementation>(new ProgressStatisticsImplementation(t, monitor, job));
}
