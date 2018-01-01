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

#include"match.h"
#include"log.h"

#include<assert.h>
#include<string.h>

using namespace std;

// This is the goal of match functionality. Identicla to rclone.
// This implementation currently does not do this:
//
// First character is slash => match against root
// First character is not slash => match against end of path
// file.jpg  - matches "file.jpg"
//           - matches "directory/file.jpg"
//           - doesn't match "afile.jpg"
//           - doesn't match "directory/afile.jpg"
// /file.jpg - matches "file.jpg" in the root directory
//           - doesn't match "afile.jpg"
//           - doesn't match "directory/file.jpg"

// A * matches anything but not a /.

//   *.jpg  - matches "file.jpg"
//          - matches "directory/file.jpg"
//          - doesn't match "file.jpg/something"

// Use ** to match anything, including slashes (/).

// dir/** - matches "dir/file.jpg"
//        - matches "dir/dir1/dir2/file.jpg"
//        - doesn't match "directory/file.jpg"
//        - doesn't match "adir/file.jpg"

// A ? matches any character except a slash /.

// l?ss  - matches "less"
//       - matches "lass"
//       - doesn't match "floss"

// A [ and ] together make a a character class,
// such as [a-z] or [aeiou] or [[:alpha:]].

// h[ae]llo - matches "hello"
//          - matches "hallo"
//          - doesn't match "hullo"

// A { and } define a choice between elements. It should contain a
// comma seperated list of patterns, any of which might match.
// These patterns can contain wildcards.

// {one,two}_potato - matches "one_potato"
//                  - matches "two_potato"
//                  - doesn't match "three_potato"
//                  - doesn't match "_potato"

// Special characters can be escaped with a \ before them.

// \*.jpg       - matches "*.jpg"
// \\.jpg       - matches "\.jpg"
//  \[one\].jpg  - matches "[one].jpg"

// If you put any rules which end in / then it will only match directories.

ComponentId MATCH = registerLogComponent("match");

bool Match::use(std::string pattern)
{
    pattern_ = pattern;
    assert(pattern_.length() > 0);
    rooted_ = (pattern_[0] == '/');
    suffix_doublestar_ = ( pattern_.length() >= 3 &&
                           '*' == pattern_[pattern_.length()-1] &&
                           '*' == pattern_[pattern_.length()-2] &&
                           '/' == pattern_[pattern_.length()-3]);
    if (suffix_doublestar_) {
        pattern_.pop_back(); pattern_.pop_back(); pattern_.pop_back();
        if (pattern_.find('*') != string::npos) {
            error(MATCH,"Invalid pattern \"%s\"", pattern.c_str());
        }
    }
    suffix_singlestar_ = (pattern_.back() == '*');
    if (suffix_singlestar_) {
        pattern_.pop_back();
        if (pattern_.find('*') != string::npos ||
            pattern_.find('/') != string::npos) {
            error(MATCH,"Invalid pattern \"%s\"", pattern.c_str());
        }
    }
    prefix_singlestar_ = (pattern_[0] == '*');
    if (prefix_singlestar_) {
        pattern_.erase(0,1);
        if (pattern_.find('*') != string::npos ||
            pattern_.find('/') != string::npos) {
            error(MATCH,"Invalid pattern \"%s\"", pattern.c_str());
        }
    }
    debug(MATCH,"Pattern \"%s\" rooted=%d suffix_doublestar=%d "
          "suffix_singlestar=%d prefix_singlestar=%d\n",
          pattern_.c_str(), rooted_, suffix_doublestar_, suffix_singlestar_, prefix_singlestar_);
    return true;
}

bool Match::match(const char *path) {
    return match(path, strlen(path));
}

bool Match::match(const char *path, size_t len)
{
    debug(MATCH,"Does path  \"%s\" match filter \"%s\" ?\n", path, pattern_.c_str());
    if (rooted_) {
        // Match from the beginning.
        if (suffix_doublestar_) {
            // The /** has already been cut away from the pattern.
            // Does the path begin with the pattern?
            bool m = (0 == strncmp(pattern_.c_str(), path, pattern_.length()));
            if (m) {
                // A match, now check that the path segment ends when the pattern ends.
                m = (path[pattern_.length()] == 0 ||
                     path[pattern_.length()] == '/');
            }
            debug(MATCH,"Rooted double star match %d\n", m);
            return m;
        }
        bool m = 0 == strcmp(pattern_.c_str(), path);
        debug(MATCH,"Rooted exact match %d\n", m);
        return m;
    }

    if (suffix_doublestar_) {
        const char *p = strstr(path, pattern_.c_str());
        bool m = false;
        while (p != NULL) {
            // A match, now check that the path segment ends when the pattern ends.
            m = (p[pattern_.length()] == 0 ||
                 p[pattern_.length()] == '/');
            if (m) break;
            p++;
            p = strstr(p, pattern_.c_str());
        }
        debug(MATCH,"Double star match %d\n", m);
        return m;
    }

    const char *p = strrchr(path, '/');
    if (p == NULL) p = path; else p++;
    size_t slen = strlen(p);
    debug(MATCH,"Last element in path \"%s\"\n", p);

    if (prefix_singlestar_) {
        bool m = false;
        if (pattern_.length() <= slen) {
            size_t diff = slen-pattern_.length();
            m = (0 == strcmp(p+diff, pattern_.c_str()));
            if (m) {
                p+=diff;
                m = (p[pattern_.length()] == 0 ||
                     p[pattern_.length()] == '/');
            }
        }
        debug(MATCH,"Prefix single star %d\n", m);
        return m;
    }

    if (suffix_singlestar_) {
        bool m = (0 == strncmp(p, pattern_.c_str(), pattern_.length()));
        debug(MATCH,"Single star last %d\n", m);
        return m;
    }
    return 0 == strcmp(p, pattern_.c_str());
}
