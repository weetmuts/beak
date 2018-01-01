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

#include "ui.h"
#include "filesystem.h"

#include <stdarg.h>

void UI::output(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vfprintf(stdout, fmt, args);
    va_end(args);
}

void UI::output(std::string msg)
{
    output(msg.c_str());
}

void UI::clearLine()
{
    output("\x1B[2K\r");
}

void UI::outputPrompt(const char *msg)
{
    printf("%s", msg);
}

void UI::outputPrompt(std::string msg)
{
    printf("%s", msg.c_str());
}

std::string UI::inputString()
{
    char buf[1024*1024];
    if (fgets (buf, sizeof(buf)-1, stdin) == NULL)
        return "";
    std::string s = buf;
    if (s.back() == '\n') s.pop_back();
    return s;
}

Path *UI::inputPath()
{
    std::string s = inputString();
    Path *p = Path::lookup(s);
    return p;
}

size_t UI::inputSize()
{
    std::string s = inputString();
    long long l = atol(s.c_str());
    return l;
}

int UI::inputChoice(std::string msg, std::string prompt, std::vector<std::string> choices)
{
    int c = -1;
    while (true)
    {
        output("%s\n", msg.c_str());
        for (size_t i=0; i<choices.size(); ++i) {
            output("%d) %s\n", i+1, choices[i].c_str());
        }
        outputPrompt(prompt);
        std::string s = inputString();

        size_t i = atoi(s.c_str());
        if (i >= 1 && i <= choices.size()) {
            c = i;
            break;
        }
        output("Not a proper choice, please try again.\n");
    }

    return c;
}
