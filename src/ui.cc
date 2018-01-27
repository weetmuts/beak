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

using namespace std;

void UI::output(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vfprintf(stdout, fmt, args);
    va_end(args);
}

void UI::output(string msg)
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

void UI::outputPrompt(string msg)
{
    printf("%s", msg.c_str());
}

string UI::inputString()
{
    char buf[1024*1024];
    if (fgets (buf, sizeof(buf)-1, stdin) == NULL)
        return "";
    string s = buf;
    if (s.back() == '\n') s.pop_back();
    UI::output("\n");
    return s;
}

Path *UI::inputPath()
{
    string s = inputString();
    Path *p = Path::lookup(s);
    return p;
}

size_t UI::inputSize()
{
    string s = inputString();
    long long l = atol(s.c_str());
    return l;
}

int UI::inputChoice(string msg, string prompt, vector<string> choices)
{
    int c = -1;
    while (true)
    {
        output("%s\n", msg.c_str());
        for (size_t i=0; i<choices.size(); ++i) {
            output("%d > %s\n", i+1, choices[i].c_str());
        }
        outputPrompt(prompt);
        string s = inputString();

        size_t i = atoi(s.c_str());
        if (i >= 1 && i <= choices.size()) {
            c = i-1;
            break;
        }
        output("Not a proper choice, please try again.\n");
    }

    return c;
}

void UI::inputChoice(string msg, string prompt, vector<ChoiceEntry> choices)
{
    vector<string> choice_strings;
    for (auto& ce : choices) choice_strings.push_back(ce.msg);

    int c = inputChoice(msg, prompt, choice_strings);

    ChoiceEntry ce = choices[c];
    ce.cb();
}
