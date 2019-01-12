/*
 Copyright (C) 2019 Fredrik Öhrström

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

#include"log.h"
#include"filetype.h"

#include<map>

using namespace std;

FileType singleCharFileType(char c);
FileType doubleCharFileType(char c1, char c2);
FileType tripleCharFileType(char c1, char c2, char c3);

FileType fileType(Path *p)
{
    const char *s = p->name()->str().c_str();
    size_t l = p->name()->str().length();

    if (s[l-2] == '.' && s[l-1] != '.') {
        return singleCharFileType(s[l-1]);
    }
    if (s[l-3] == '.' && s[l-1] != '.' && s[l-2] != '.') {
        return doubleCharFileType(s[l-1], s[l-2]);
    }
    if (s[l-4] == '.' && s[l-1] != '.' && s[l-2] != '.' && s[l-3] != '.') {
        return tripleCharFileType(s[l-1], s[l-2], s[l-3]);
    }
    return FileType::Unknown;
}

FileType singleCharFileType(char c)
{
    if (c == 'c' ||
        c == 'h' ||
        c == 'C' ||
        c == 'H') {
        return FileType::Source;
    };

    if (c == 'd') {
        return FileType::Build;
    }
    return FileType::Unknown;
}

FileType doubleCharFileType(char c1, char c2)
{
    if (c1 == 'c' && c2 == 'c') return FileType::Source;
    if (c1 == 's' && c2 == 'h') return FileType::Source;

    return FileType::Unknown;
}

FileType tripleCharFileType(char c1, char c2, char c3)
{
    return FileType::Unknown;
}
