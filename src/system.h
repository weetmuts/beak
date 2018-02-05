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

#include <memory>
#include <string>
#include <vector>

enum Capture {
    CaptureStdout,
    CaptureStderr,
    CaptureBoth
};

struct System
{
    virtual RC invoke(std::string program,
                      std::vector<std::string> args,
                      std::vector<char> *output = NULL,
                      Capture capture = CaptureStdout,
                      std::function<void(char *buf, size_t len)> output_cb = NULL) = 0;
    virtual ~System() = default;
};

std::unique_ptr<System> newSystem();

#endif
