/*  
    Copyright (C) 2016 Fredrik Öhrström

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

#include<string>

using namespace std;

string humanReadable(size_t s);
uint64_t clockGetTime();
size_t basepos(string& s);
string basename(string& s);
string dirname(string& s);

struct depthFirstSort
{
    // Special path comparison operator that sorts file names and directories in this order:
    // This is the order necessary to find chunk points depth first.
    // TEXTS/filter/alfa
    // TEXTS/filter
    // TEXTS/filter.zip
    static bool compare(const char *f, const char *t);    
    inline bool operator() (const std::string& a, const std::string& b) { return compare(a.c_str(), b.c_str()); }
};

struct TarSort
{
    // Special path comparison operator that sorts file names and directories in this order:
    // This is the default order for tar files, the directory comes first, then subdirs, then content.
    // TEXTS/filter
    // TEXTS/filter/alfa
    // TEXTS/filter.zip
    static bool compare(const char *f, const char *t);    
};

string commonPrefix(string a, string b);

uint32_t hashString(string a);

string permissionString(mode_t m);

string ownergroupString(uid_t uid, gid_t gid);

