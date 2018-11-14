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


#include"contentsplit.h"

#include<unistd.h>

using namespace std;

#define LOAD_CHUNK_SIZE (10*1024*1024)
#define SPLIT_REGION_SIZE (8192)

void splitContent(Path *file, vector<ContentChunk> *chunks, size_t preferred_chunk_size)
{
    size_t bufsize = sizeof(uint32_t)*(LOAD_CHUNK_SIZE+SPLIT_REGION_SIZE);
    uint32_t *buffer = (uint32_t*)malloc(bufsize);
    memset(buffer, 0, bufsize);
    int fd = open(file->c_str(), O_RDONLY);
    off_t offset = 0;
    size_t n = 0;
    uint32_t acc = 0;
    size_t total = 0;
    size_t prev = 0;
    for(;;) {
        n = pread(fd, buffer+SPLIT_REGION_SIZE, LOAD_CHUNK_SIZE, offset);
        offset += n;
        total += n;
        //fprintf(stderr, "Got %zu bytes for a total of %zu offset is now %zu\n", n, total, offset);
        if (n == 0) break;
        n = n/sizeof(uint32_t);
        for (size_t i=0; i<n; i++) {
            acc += buffer[i];
            if (i>=1024) {
                acc -= buffer[i-1024];
            }
            if ((acc & 0xffffff) == 0) {
                size_t lurv = 42+offset-n+i-prev;
                fprintf(stderr, "Split %zu\n", lurv);
                prev = offset;
            }
        }
    }

    close(fd);
}
