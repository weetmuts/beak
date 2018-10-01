/*
 Copyright (C) 2017 Fredrik Öhrström

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

#ifndef SYSTEM_H
#define SYSTEM_H

#include "always.h"

#include "filesystem.h"

#include <memory>
#include <string>
#include <vector>

enum Capture {
    CaptureStdout,
    CaptureStderr,
    CaptureBoth
};

struct ThreadCallback
{
    virtual void stop() = 0;
    virtual void doWhileCallbackBlocked(std::function<void()> do_cb) = 0;
    virtual ~ThreadCallback() = default;
};

std::unique_ptr<ThreadCallback> newRegularThreadCallback(int millis, std::function<bool()> thread_cb);

struct System
{
    // Invoke another program within the OS
    virtual RC invoke(std::string program,
                       std::vector<std::string> args,
                       std::vector<char> *output = NULL,
                       Capture capture = CaptureStdout,
                       std::function<void(char *buf, size_t len)> output_cb = NULL) = 0;

    virtual RC invokeShell(Path *init_file) = 0;

    // A daemon mount will exit the current program and continue to run in the background as a daemon,
    virtual RC mountDaemon(Path *dir, FuseAPI *fuseapi, bool foreground=false, bool debug=false) = 0;
    // Unmount the daemon
    virtual RC umountDaemon(Path *dir) = 0;
    // A normal mount forks and the current program continues to run.
    virtual std::unique_ptr<FuseMount> mount(Path *dir, FuseAPI *fuseapi, bool debug=false) = 0;
    // Unmount the previous mount.
    virtual RC umount(ptr<FuseMount> fuse_mount) = 0;

    virtual ~System() = default;
};

std::unique_ptr<System> newSystem();

#endif
