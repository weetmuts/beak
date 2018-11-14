/*
 Copyright (C) 2018 Fredrik Öhrström

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

#ifndef CONTENTSPLIT_H
#define CONTENTSPLIT_H

#include"always.h"
#include"filesystem.h"

#include<vector>

struct ContentChunk
{
    std::vector<char> hash;
    size_t size;
};

void splitContent(Path *file, std::vector<ContentChunk> *chunks, size_t preferred_chunk_size);

#endif
