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

struct MonitorImplementation : Monitor
{
    void updateJob(pid_t pid, string info);
    string lastUpdate(pid_t pid);
    int startDisplay(string job, function<bool()> regular_cb);
    void stopDisplay(int id);

    bool regularDisplay();
    void doWhileCallbackBlocked(std::function<void()> do_cb);

    MonitorImplementation(System *sys, FileSystem *fs);
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
};

unique_ptr<Monitor> newMonitor(System *sys, FileSystem *fs) {
    return unique_ptr<Monitor>(new MonitorImplementation(sys, fs));
}

MonitorImplementation::MonitorImplementation(System *s, FileSystem *fs) : sys_(s), fs_ (fs)
{
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

int MonitorImplementation::startDisplay(string job, function<bool()> progress_cb)
{
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

    UI::storeCursor();
    UI::moveTopLeft();
    printf("\033[0;37;1m\033[44m%s", s.c_str());
    UI::restoreCursor();
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
