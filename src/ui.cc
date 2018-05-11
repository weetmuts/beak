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

#include "filesystem.h"
#include "log.h"
#include "ui.h"

#include <assert.h>
#include <stdarg.h>

using namespace std;

void UI::output(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    vfprintf(stdout, fmt, args);
    va_end(args);
    fflush(stdout);
}

void UI::output(string msg)
{
    output(msg.c_str());
}

void UI::outputln(string msg)
{
    output(msg.c_str());
    output("\n");
    fflush(stdout);
}

void UI::clearLine()
{
    if (logLevel() <= INFO) {
        output("\x1B[2K\r");
    }
}

void UI::redrawLineOutput(const char *fmt, ...)
{
    if (logLevel() <= INFO) {
        fprintf(stdout, "\x1B[2K\r");
    }

    va_list args;
    va_start(args, fmt);
    vfprintf(stdout, fmt, args);
    va_end(args);
    fflush(stdout);

    if (logLevel() > INFO) {
        fprintf(stdout, "\n");
    }

}

void UI::outputPrompt(const char *msg)
{
    printf("%s", msg);
    fflush(stdout);
}

void UI::outputPrompt(string msg)
{
    printf("%s", msg.c_str());
    fflush(stdout);
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

int menu(string msg, string prompt, vector<ChoiceEntry> choices)
{
    while (true)
    {
        UI::output("%s\n", msg.c_str());
        for (size_t i=0; i<choices.size(); ++i) {
            if (choices[i].available) {
                const char *info = "%s > %s\n";
                if (choices.size() > 9) {
                    info = "%-2s > %s\n";
                }
                UI::output(info, choices[i].key.c_str(), choices[i].msg.c_str());
            } else {
                const char *info = "    %s\n";
                if (choices.size() > 9) {
                    info = "     %s\n";
                }
                UI::output(info, choices[i].msg.c_str());
            }
        }
        UI::outputPrompt(prompt);
        string s = UI::inputString();

        for (size_t i=0; i<choices.size(); ++i) {
            if (s == choices[i].key || s == choices[i].keyword)
            {
                return i;
            }
        }
        UI::output("Not a proper choice, please try again.\n");
    }
    assert(0);
}

ChoiceEntry *UI::inputChoice(string msg, string prompt, vector<ChoiceEntry> &choices)
{
    int c = 1;
    for (auto& ce : choices) {
        if (ce.key == "" && ce.available) {
            ce.key = to_string(c);
            c++;
        }
    }
    c = menu(msg, prompt, choices);

    ChoiceEntry *ce = &choices[c];
    if (ce->cb) ce->cb();
    ce->index = c;
    assert(ce->key.c_str());
    return ce;
}

YesOrNo UI::yesOrNo(string msg)
{
    for (;;) {
        UI::outputPrompt(msg+"\ny/n>");
        string c = UI::inputString();
        if (c == "y") {
            return UIYes;
        }
        if (c == "n") {
            return UINo;
        }
    }
}

KeepOrChange UI::keepOrChange()
{
    for (;;) {
        UI::outputPrompt("Keep or change?\nk/c>");
        string c = UI::inputString();
        if (c == "k") {
            return UIKeep;
        }
        if (c == "c") {
            return UIChange;
        }
    }
}

KeepOrChange UI::keepOrChangeOrDiscard()
{
    for (;;) {
        UI::outputPrompt("Keep,change or discard?\nk/c/d>");
        string c = UI::inputString();
        if (c == "k") {
            return UIKeep;
        }
        if (c == "c") {
            return UIChange;
        }
        if (c == "d") {
            return UIDiscard;
        }
    }
}
