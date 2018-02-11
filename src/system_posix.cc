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

#include <memory.h>
#include <sys/types.h>
#include <wait.h>

#include <unistd.h>

using namespace std;

static ComponentId SYSTEM = registerLogComponent("system");

struct SystemImplementation : System
{
    RCC invoke(string program,
               vector<string> args,
               vector<char> *out,
               Capture capture,
               function<void(char *buf, size_t len)> cb);

    ~SystemImplementation() = default;
};

unique_ptr<System> newSystem()
{
    return unique_ptr<System>(new SystemImplementation());
}

string protect_(string arg)
{
    return arg;
}

RCC SystemImplementation::invoke(string program,
                                 vector<string> args,
                                 vector<char> *output,
                                 Capture capture,
                                 function<void(char *buf, size_t len)> cb)
{
    int link[2];
    const char **argv = new const char*[args.size()+2];
    argv[0] = program.c_str();
    int i = 1;
    debug(SYSTEM, "Invoking: \"%s\"\n", program.c_str());
    for (auto &a : args) {
        argv[i] = a.c_str();
        i++;
        debug(SYSTEM, "\"%s\"\n ", a.c_str());
    }
    argv[i] = NULL;

    debug(SYSTEM, "\n");

    if (output) {
        if (pipe(link) == -1) {
            error(SYSTEM, "Could not create pipe!\n");
        }
    }
    pid_t pid = fork();
    int status;
    if (pid == 0) {
        // I am the child!
        if (output) {
            if (capture == CaptureBoth || capture == CaptureStdout) {
                dup2 (link[1], STDOUT_FILENO);
            }
            if (capture == CaptureBoth || capture == CaptureStderr) {
                dup2 (link[1], STDERR_FILENO);
            }
            close(link[0]);
            close(link[1]);
        }
        close(0); // Close stdin
        execvp(program.c_str(), (char*const*)argv);
        perror("Execvp failed:");
        error(SYSTEM, "Invoking %s failed!\n", program.c_str());
    } else {
        if (pid == -1) {
            error(SYSTEM, "Could not fork!\n");
        }

        if (output) {
            close(link[1]);

            char buf[4096 + 1];

            int n = 0;

            for (;;) {
                memset(buf, 0, sizeof(buf));
                n = read(link[0], buf, sizeof(buf));
                if (n > 0) {
                    output->insert(output->end(), buf, buf+n);
                    if (cb) { cb(buf, n); }
                    //fprintf(stderr, "BANANAS >%*s<\n", n, buf);
                } else {
                    // No more data to read.
                    fprintf(stderr, "NOMORE GURKA\n");
                    break;
                }
            }
        }
        debug(SYSTEM,"Waiting for child %d.\n", pid);
        // Wait for the child to finish!
        waitpid(pid, &status, 0);
        if (WIFEXITED(status)) {
            // Child exited properly.
            int rc = WEXITSTATUS(status);
            debug(SYSTEM,"Return code %d\n", rc);
            if (rc != 0) {
                warning(SYSTEM,"%s exited with non-zero return code: %d\n", program.c_str(), rc);
                return RCC::ERRR;
            }
        }
    }
    return RCC::OKK;
}
