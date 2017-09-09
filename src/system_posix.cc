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
#include <wait.h>

#include <unistd.h>

static ComponentId SYSTEM = registerLogComponent("system");

struct SystemImplementation : System
{
    int invoke(std::string program, std::vector<std::string> args);
};

std::unique_ptr<System> newSystem()
{
    return std::unique_ptr<System>(new SystemImplementation());
}

string protect_(string arg)
{
    return arg;
}

int SystemImplementation::invoke(std::string program, std::vector<std::string> args)
{
    const char **argv = new const char*[args.size()+2];
    argv[0] = program.c_str();
    int i = 1;
    debug(SYSTEM, "Invoking: %s ", program.c_str());
    for (auto &a : args) {
        argv[i] = a.c_str();
        i++;
        debug(SYSTEM, ">>%s<< ", a.c_str());
    }
    argv[i] = NULL;
    
    debug(SYSTEM, "\n");

    int rc = 0;
    pid_t pid = fork();
    int status;
    if (pid == 0) {
        // I am the child!
        close(0); // Close stdin
        execvp(program.c_str(), (char*const*)argv);
        perror("Execvp failed:");
        error(SYSTEM, "Invoking %s failed!\n", program.c_str());
    } else {
        // Wait for the child to finish!
        waitpid(pid, &status, 0);
        if (WIFEXITED(status)) {
            // Child exited properly.
            rc = WEXITSTATUS(status);
            debug(SYSTEM,"Return code %d\n", rc);
        }
    }
    return rc;
}

