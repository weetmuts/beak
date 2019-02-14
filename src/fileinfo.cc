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
    X(js,Source)         \
    X(py,Source)         \
    X(sh,Source)         \
    X(xz,Archive)        \
                         \
    X(bat,Source)        \
    X(cpp,Source)        \
    X(css,Web)           \
    X(doc,Document)      \
    X(exe,Executable)    \
    X(hpp,Source)        \
    X(pdf,Document)      \
    X(png,Image)         \
    X(tex,Document)      \
    X(txt,Document)      \
                         \
    X(docx,Document)     \
    X(html,Web)          \
    X(java,Source)       \
                         \
    X(class,Object)      \
                         \
    X(makefile,Source)   \


// Any other found extension is stored here.
set<string> extensions_;

const char *intern_extension_(const char *s)
{
    string e = string(s);
    set<string>::iterator i = extensions_.find(e);
    if (i == extensions_.end()) {
        extensions_.insert(e);
        i = extensions_.find(e);
    }
    return (*i).c_str();
}

FileInfo fileInfo(Path *p)
{
    const char *s = p->name()->str().c_str();
    size_t l = p->name()->str().length();

#define X(suffix,type)                \
    {                                 \
        size_t len = STRLEN(#suffix); \
        if (l>(len+2) && s[l-len-1] == '.' && !strncasecmp(&s[l-len], #suffix, len)) { \
            return { FileType::type, #suffix, \
                     fileTypeName(FileType::type, false), \
                     fileTypeName(FileType::type, true) };       \
        } \
    }

LIST_OF_SUFFIXES

#undef X
    const char *dot = strrchr(s, '.');

    if (dot) {
        dot = intern_extension_(dot+1);
    } else {
        dot = "";
    }
    return { FileType::Other, dot, fileTypeName(FileType::Other, false), fileTypeName(FileType::Other, true) };
}

const char *fileTypeName(FileType ft, bool pluralis)
{
    switch (ft)
    {
#define X(type,name,names) case FileType::type: return pluralis?#names:#name;
LIST_OF_FILETYPES
#undef X
    };

    assert(0);
}
