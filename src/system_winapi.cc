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

#include "log.h"
#include "system.h"

#include <sys/types.h>

using namespace std;

//static ComponentId SYSTEM = registerLogComponent("system");

struct SystemImplementationWinapi : System
{
    RC run(string program,
           vector<string> args,
           int *out_rc);
    RC invoke(string program,
              vector<string> args,
              vector<char> *output,
              Capture capture,
              function<void(char *buffer, size_t len)> cb,
              int *out_rc);

    RC invokeShell(Path *init_file);
    bool processExists(pid_t pid);

    RC mountDaemon(Path *dir, FuseAPI *fuseapi, bool foreground, bool debug);
    RC umountDaemon(Path *dir);
    std::unique_ptr<FuseMount> mount(Path *dir, FuseAPI *fuseapi, bool debug);
    RC umount(ptr<FuseMount> fuse_mount);
    string userName();
    void setStackSize();
    Path *cwd();
    uid_t getUID() {  return 0; }

private:
    int *rooot {};
};

unique_ptr<System> newSystem()
{
    return unique_ptr<System>(new SystemImplementationWinapi());
}

string protect_(string arg)
{
    return arg;
}

RC SystemImplementation::run(string program,
                             vector<string> args,
                             int *out_rc)
{
    return RC::ERR;
}

RC SystemImplementationWinapi::invoke(string program,
                                       vector<string> args,
                                       vector<char> *out,
                                       Capture capture,
                                       function<void(char *buffer, size_t len)> cb)
{
    fprintf(stderr, "INVOKING!\n");
    return RC::ERR;
}

RC SystemImplementationWinapi::invokeShell(Path *init_file)
{
    return RC::ERR;
}

bool SystemImplementationWinapi::processExists(pid_t pid)
{
    return false;
}


unique_ptr<ThreadCallback> newRegularThreadCallback(int millis, std::function<bool()> thread_cb)
{
    return NULL;
}

RC SystemImplementationWinapi::mountDaemon(Path *dir, FuseAPI *fuseapi, bool foreground, bool debug)
{
    return RC::ERR;
}

RC SystemImplementationWinapi::umountDaemon(Path *dir)
{
    return RC::ERR;
}

std::unique_ptr<FuseMount> SystemImplementationWinapi::mount(Path *dir, FuseAPI *fuseapi, bool debug)
{
    return NULL;
}

RC SystemImplementationWinapi::umount(ptr<FuseMount> fuse_mount)
{
    return RC::ERR;
}

string SystemImplementationWinapi::userName()
{
    return "";
}

void SystemImplementationWinapi::setStackSize()
{
}
