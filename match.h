/*
 Copyright (C) 2016-2017 Fredrik Öhrström

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

#ifndef MATCH_H
#define MATCH_H

#include <string>

// Currntly supported match patterns:
// Suffix: /**
//   Means Alfa/Beta/** << slashes allowed
//   matches Alfa/Beta and everything below Beta.
//   matches /x/y/Alfa/Beta etc
//   does not match Alfa/BetaBeta/Gamma
//
// Prefix: *
//   Means *.jpg << no slashes allowed! To be fixed
//   matches img1.jpg /alfa/beta/img2.jpg etc
//   does not match on directory: img1.jpg/foo

// Suffix: *
//   Means log* << no slashes allowed! To be fixed
//   matches log_123.txt Alfa/log_123.txt
//   does not match Alfa/alog_123.txt
//   does not match directory log_123/

struct Match
{
    bool use(std::string pattern);
    bool match(const char *path);
    bool match(const char *path, size_t len);
    
    private:
    std::string pattern_;

    bool rooted_;
    bool suffix_doublestar_;
    bool suffix_singlestar_;
    bool prefix_singlestar_;
};

#endif
