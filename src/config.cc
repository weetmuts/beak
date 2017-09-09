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

#include "log.h"
#include "config.h"
#include "util.h"

#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>

static ComponentId CONFIG = registerLogComponent("config");

std::unique_ptr<Config> newConfig() {
    return std::unique_ptr<Config>(new Config());
}

Config::Config() {
}

bool Config::load() {
    vector<char> buf;

    bool ok = load(&buf);
    if (ok) {
        vector<char>::iterator i = buf.begin();
        bool eof=false, err=false;
        Location *current = NULL;
        while (true) {
            eatWhitespace(buf,i,&eof);
            if (eof) break;
            string block = eatTo(buf,i,'\n', 1024*1024, &eof, &err);
            if (eof || err) break;
            trimWhitespace(&block);
            // Ignore empty lines
            if (block.length() == 0) continue;
            // Ignore comment lines
            if (block[0] == '#') continue;
            if (block[0] == '[' && block.back() == ']') {
                // Found the start of a new target
                string name = block.substr(1,block.length()-2);
                trimWhitespace(&name);
                name.push_back(':');
                locations_[name] = Location();
                current = &locations_[name];
                current->name = name;
                debug(CONFIG,"Location: \"%s\"\n", name.c_str());
            } else {
                std::vector<char> line(block.begin(), block.end());
                auto i = line.begin();
                string key = eatTo(line, i, '=', 1024*1024, &eof, &err);
                trimWhitespace(&key);
                if (eof || err) break;
                string value = eatTo(line, i, -1, 1024*1024, &eof, &err);
                trimWhitespace(&value);
                if (err) break;
                debug(CONFIG,"%s = %s\n", key.c_str(), value.c_str());
                if (key == "source_path") { current->source_path = value; }
                else if (key == "snapshot_path") { current->snapshot_path = value; }
                else if (key == "args") { current->args = value; }
                else if (key == "remote") { current->remotes.push_back(value); }
                else { error(CONFIG, "Unknown key \"%s\" in configuration file!\n", key.c_str()); }
            }
        }
    }
    return true;
}

bool Config::load(vector<char> *buf) {
    char block[512];
    int fd = open("/home/fredrik/.beak.conf", O_RDONLY);
    if (fd == -1) {
        return false;
    }
    while (true) {
        ssize_t n = read(fd, block, sizeof(block));
        if (n == -1) {
            if (errno == EINTR) {
                continue;
            }
            failure(CONFIG,"Could not read from config file %s errno=%d\n", "/home/fredrik/.beak.conf", errno);
            close(fd);
            return false;
        }
        buf->insert(buf->end(), block, block+n);
        if (n < (ssize_t)sizeof(block)) {
            break;
        }
    }
    close(fd);
    return true;
}

