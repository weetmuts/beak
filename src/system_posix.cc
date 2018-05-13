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
#include <pthread.h>
#include <sys/types.h>
#include <wait.h>

#include <unistd.h>

using namespace std;

static ComponentId SYSTEM = registerLogComponent("system");

struct ThreadCallbackImplementation : ThreadCallback
{
    ThreadCallbackImplementation(int millis, function<bool()> thread_cb);
    void stop();
    void doWhileCallbackBlocked(std::function<void()> do_cb);
    ~ThreadCallbackImplementation();

private:

    pthread_mutex_t execute_ {};
    bool running_ {};
    int millis_ {};
    function<bool()> regular_cb_;
    pthread_t thread_ {};

    friend void *regularThread(void *data);
};

void ThreadCallbackImplementation::stop()
{
    running_ = false;
    if (thread_) {
        //pthread_kill(thread_, SIGUSR2);
    }
}

void ThreadCallbackImplementation::doWhileCallbackBlocked(function<void()> do_cb)
{
    pthread_mutex_lock(&execute_);
    do_cb();
    pthread_mutex_unlock(&execute_);
}

void *regularThread(void *data)
{
    auto tcbi = (ThreadCallbackImplementation*)(data);

    time_t prev = time(NULL);
    time_t curr = prev;
    while (tcbi->running_) {
        curr = time(NULL);
        time_t diff = curr-prev;
        if (diff >= 1) {
            tcbi->doWhileCallbackBlocked(tcbi->regular_cb_);
            prev = curr;
        } else {
            int rc = usleep(1000000);
            if (rc == -1) {
                if (errno == EINTR) {
                    debug(SYSTEM, "Regular thread callback awaken by signal.\n");
                } else {
                    error(SYSTEM, "Could not sleep.\n");
                }
            }
        }
    }
    return NULL;
}

ThreadCallbackImplementation::ThreadCallbackImplementation(int millis, function<bool()> regular_cb)
    : millis_(millis), regular_cb_(regular_cb)
{
    running_ = true;
    int rc  = pthread_create(&thread_, NULL, regularThread, this);
    if (rc) {
        error(SYSTEM, "Could not create thread.\n");
    }
    millis_++;
}


ThreadCallbackImplementation::~ThreadCallbackImplementation()
{
    fprintf(stderr, "Destructing regular thread\n");
    if (thread_) {
        //pthread_kill(thread_, SIGUSR2);
        pthread_join(thread_, NULL);
        fprintf(stderr, "JOINED thread properly!\n");
    }
}

unique_ptr<ThreadCallback> newRegularThreadCallback(int millis, std::function<bool()> thread_cb)
{
    return unique_ptr<ThreadCallback>(new ThreadCallbackImplementation(millis, thread_cb));
}

struct SystemImplementation : System
{
    RC invoke(string program,
               vector<string> args,
               vector<char> *out,
               Capture capture,
               function<void(char *buf, size_t len)> cb);

    RC regularThreadCallback(int millis, function<bool()> thread_cb);

    ~SystemImplementation() = default;
};

unique_ptr<System> newSystem()
{
//    onExit([](){ printf("\nInterrupted, cleaning up.\n"); exit(0); });
    return unique_ptr<System>(new SystemImplementation());
}

string protect_(string arg)
{
    return arg;
}

RC SystemImplementation::invoke(string program,
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
                    debug(SYSTEM, "Input: \"%*s\"\n", n, buf);
                    //fprintf(stderr, "BANANAS >%*s<\n", n, buf);
                } else {
                    // No more data to read.
                    debug(SYSTEM, "Input: done\n");
                    //fprintf(stderr, "NOMORE GURKA\n");
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
                return RC::ERR;
            }
        }
    }
    return RC::OK;
}

function<void()> exit_handler;

void exitHandler(int signum)
{
    if (exit_handler) exit_handler();
}

void doNothing(int signum) {
}

void onExit(function<void()> cb)
{
    /*
    exit_handler = cb;
    struct sigaction new_action, old_action;

    new_action.sa_handler = exitHandler;
    sigemptyset (&new_action.sa_mask);
    new_action.sa_flags = 0;*/
/*
    sigaction (SIGINT, NULL, &old_action);
    if (old_action.sa_handler != SIG_IGN) sigaction(SIGINT, &new_action, NULL);
    sigaction (SIGHUP, NULL, &old_action);
    if (old_action.sa_handler != SIG_IGN) sigaction (SIGHUP, &new_action, NULL);
    sigaction (SIGTERM, NULL, &old_action);
    if (old_action.sa_handler != SIG_IGN) sigaction (SIGTERM, &new_action, NULL);
*/
/*    new_action.sa_handler = doNothing;
    sigemptyset (&new_action.sa_mask);
    new_action.sa_flags = 0;*/

    //sigaction (SIGUSR2, NULL, &old_action);
    //if (old_action.sa_handler != SIG_IGN) sigaction(SIGUSR2, &new_action, NULL);
}
