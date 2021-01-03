/*
 Copyright (C) 2020 Fredrik Öhrström

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

#ifndef RDIFF_H
#define RDIFF_H

#include "always.h"
#include "filesystem.h"

// Write a sig file that identifies the contents of the old file using rolling hashes.
bool generateSignature(Path *old, FileSystem *old_fs,
                       Path *sig, FileSystem *sig_fs);
// Write a delta file that describes how to convert old file to the target file.
// The delta calculation does not need the whole old file, it only needs the sig file.
bool generateDelta(Path *sig, FileSystem *sig_fs,
                   Path *target, FileSystem *target_fs,
                   Path *delta, FileSystem *delta_fs);
// Write the generated target file using the old file and the delta file.
bool applyPatch(Path *old, FileSystem *old_fs,
                Path *delta, FileSystem *delta_fs,
                Path *target, FileSystem *target_fs);

#endif
