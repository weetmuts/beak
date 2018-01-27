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

#ifndef UI_H
#define UI_H

#include"filesystem.h"

#include<vector>

struct ChoiceEntry
{
    std::string msg;
    std::function<void()> cb;

    ChoiceEntry(std::string m, std::function<void()> c) : msg(m), cb(c) { }
};

struct UI {
    // Print output data
    static void output(const char *fmt, ...);
    static void output(std::string msg);
    // Clear line, so that output will overwrite it again!
    static void clearLine();
    // Present a prompt message "name>" no newline/cr.
    static void outputPrompt(const char *msg);
    static void outputPrompt(std::string msg);
    // Request a string from the user.
    static std::string inputString();
    // Request a path (file or directory) from the user.
    static Path *inputPath();
    // Request a size, 1024, 1K, 2M, 3G
    static size_t inputSize();
    // Request a choice from a menu
    static int inputChoice(std::string msg, std::string prompt, std::vector<std::string> choices);
    static void inputChoice(std::string msg, std::string prompt, std::vector<ChoiceEntry> choices);
};

#endif
