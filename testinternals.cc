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

#include"log.h"
#include"match.h"

static ComponentId MAIN = registerLogComponent("main");

void testMatch(std::string pattern, const char *path, bool should_match)
    throw (std::string);

bool verbose_ = false;
bool err_found_ = false;

int main(int argc, char *argv[])
{
    if (argc > 1 && std::string("--verbose") == argv[1]) {
        setLogLevel(VERBOSE);
        verbose_ = true;
    }
    if (argc > 1 && std::string("--debug") == argv[1]) {
        setLogLevel(DEBUG);
        setLogComponents("all");
    }
    try {
        testMatch("/Alfa/**", "Alfa/beta/gamma", false);
        testMatch("/Alfa/**", "/Alfa/beta/gamma", true);
        testMatch("/Alfa/beta/**", "/Alfa/beta/gamma", true);
        testMatch("/Alfa/beta/**", "/Alfa/betagamma", false);

        testMatch("Alfa/**", "Alfa/beta/gamma", true);
        testMatch("Alfa/**", "AlfaBeta/gamma", false);
        testMatch("Alfa/**", "/xx/yy/Alfa/gamma", true);

        testMatch("*.jpg", "alfa.jpg", true);
        testMatch("*.jpg", "/Alfa/betA/x.jpg", true);
        testMatch("*.jpg", ".jpgalfa", false);        

        testMatch("log*", "log.txt", true);
        testMatch("loggo*", "/Alfa/Beta/loggo*", true);
        testMatch("log*", "/log", true);
        testMatch("log*", "alfalog", false);        
        
        if (!err_found_) {
            printf("OK\n");
        } else {
            printf("Errors detected!\n");
        }
    }
    catch (std::string e) {
        fprintf(stderr, "%s\n", e.c_str());
    }
}


void testMatch(std::string pattern, const char *path, bool should_match)
    throw (std::string)
{
    if (should_match) {
        verbose(MAIN,"\"%s\" matches pattern \"%s\" ", path, pattern.c_str());
    }
    else {
        verbose(MAIN,"\"%s\" should not match pattern \"%s\" ", path, pattern.c_str());
    }
    Match m;
    m.use(pattern);
    bool r = m.match(path);

    if (r == should_match) {
        verbose(MAIN, "OK\n");
    }
    else {
        verbose(MAIN, "ERR!\n");
        err_found_ = true;
    }

    if (!verbose_) {
        if (r != should_match) {
            std::string s = "";
            if (!should_match) s = " NOT ";
            throw std::string("Failure: ")+pattern+" should "+s+" match "+path;
        }
    }
}
