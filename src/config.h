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

#ifndef CONFIG_H
#define CONFIG_H

#include<map>
#include<memory>
#include<string>
#include<vector>


enum LocationType {
    BEAK_LOCATION,
    RCLONE_LOCATION
};

struct Location {
    LocationType type;
    std::string name;
    std::string source_path;
    std::string snapshot_path;
    std::string args;
    std::vector<std::string> remotes;
};

struct Config
{
    Config();
    bool load();

    Location *location(std::string name) {
        if (locations_.count(name) == 0) return NULL;
        return &locations_[name];
    }
    
private:

    std::map<std::string,Location> locations_;
    bool load(std::vector<char> *buf);
};

std::unique_ptr<Config> newConfig();

#endif
