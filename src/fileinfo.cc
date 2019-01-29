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
#include"fileinfo.h"

#include<map>
#include<set>

using namespace std;

#define STRLEN(s) ((sizeof(s)/sizeof(s[0]))-1)

// Map extensions to file types.
#define LIST_OF_SUFFIXES \
    X(c,Source)          \
    X(h,Source)          \
    X(o,Object)          \
                         \
    X(cc,Source)         \
                         \
    X(cpp,Source)        \
    X(pdf,Document)      \
    X(tex,Document)      \
                         \
    X(docx,Document)     \
    X(java,Source)       \
                         \
    X(class,Object)      \
                         \
    X(makefile,Source)   \


// Any other found extension is stored here.
set<string> extensions_;

FileInfo fileInfo(Path *p)
{
    const char *s = p->name()->str().c_str();
    size_t l = p->name()->str().length();

#define X(name,type) { size_t len = STRLEN(#name); if (l>(len+2) && s[l-len-1] == '.' && !strncasecmp(&s[l-len], #name, len)) { return { FileType::type, #name }; } }
LIST_OF_SUFFIXES
#undef X

    return { FileType::Other, "" };
}

const char *fileTypeName(FileType ft)
{
    switch (ft)
    {
#define X(name) case FileType::name: return #name;
LIST_OF_FILETYPES
#undef X
    };

    assert(0);
}
