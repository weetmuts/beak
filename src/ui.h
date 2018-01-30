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
    // If number is empty, then a number is automatically selected for this choice.
    // Otherwise the key can be for example s for save, q for quit etc.
    std::string key; // Empty, or a number, or "q" for quit or "save" for save etc.
    std::string keyword; // Supplementary key, usually a longer word, like the rule name.
    std::string msg; // The text display for this choice, often identical to keyword.
    std::function<void()> cb;

    int index;
    bool available;

    ChoiceEntry(std::string kw)
    : keyword(kw), msg(kw), index(-1), available(true) { }
    ChoiceEntry(std::string k, std::string kw, std::string m)
    : key(k), keyword(kw), msg(m), index(-1), available(true)  { }
    ChoiceEntry(std::string m, std::function<void()> c)
    : msg(m), cb(c), index(-1), available(true)  { }
    ChoiceEntry(std::string k, std::string kw, std::string m, std::function<void()> c)
    : key(k), keyword(kw), msg(m), cb(c), index(-1), available(true)  { }
};

enum YesOrNo {
    UIYes,
    UINo
};

enum KeepOrChange {
    UIKeep,
    UIChange,
    UIDiscard
};

struct UI
{
    // Print output data
    static void output(const char *fmt, ...);
    static void output(std::string msg);
    static void outputln(std::string msg);
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
    static ChoiceEntry *inputChoice(std::string msg, std::string prompt, std::vector<ChoiceEntry> &choices);

    static YesOrNo yesOrNo(std::string msg);
    static KeepOrChange keepOrChange();
    static KeepOrChange keepOrChangeOrDiscard();
};

#endif
