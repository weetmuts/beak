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

#include "filesystem.h"
#include "log.h"
#include "configuration.h"
#include "ui.h"
#include "util.h"

#include <algorithm>
#include <fcntl.h>
#include <unistd.h>

using namespace std;

static ComponentId CONFIGURATION = registerLogComponent("configuration");

const char *rule_type_names_[] = {
#define X(name,info) #name,
LIST_OF_TYPES
#undef X
};

struct ConfigurationImplementation : public Configuration
{
    ConfigurationImplementation(FileSystem *fs);

    bool load();
    int configure();

    Rule *rule(std::string name);
    std::vector<Rule*> sortedRules();

    // Map rule name to rule.
    std::map<std::string,Rule> rules_;

    // Map path to rule.
    std::map<std::string,Rule*> paths_;

    FileSystem *fs_;

    bool load(std::vector<char> *buf);
    void editRule();
    void createNewRule();

    bool okRuleName(std::string name);
};

std::unique_ptr<Configuration> newConfiguration(FileSystem *fs) {
    return std::unique_ptr<Configuration>(new ConfigurationImplementation(fs));
}

ConfigurationImplementation::ConfigurationImplementation(FileSystem *fs) {
    fs_ = fs;
}

std::string handlePath(std::string path, std::string more, std::string name)
{
    if (more.front() == '/') {
	return more;
    }
    if (path == "") {
	error(CONFIGURATION, "Error in configuration file, "
	      "\"path\" must be specified before a relative \"%s\" path.", name.c_str());
    }
    return path+"/"+more;
}

bool ConfigurationImplementation::load() {
    vector<char> buf;

    bool ok = load(&buf);
    if (ok) {
        vector<char>::iterator i = buf.begin();
        bool eof=false, err=false;
        Rule *current_rule = NULL;
	Storage *current_remote = NULL;

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
                rules_[name] = Rule();
                current_rule = &rules_[name];
                current_rule->name = name;
            } else {
                std::vector<char> line(block.begin(), block.end());
                while (line.back() == '\\') {
                    line.pop_back();
                    block = eatTo(buf,i,'\n', 1024*1024, &eof, &err);
                    if (eof || err) break;
                    line.insert(line.end(), block.begin(), block.end());
                }
                if (err) break;

                auto i = line.begin();
                string key = eatTo(line, i, '=', 1024*1024, &eof, &err);
                trimWhitespace(&key);
                if (eof || err) break;
                string value = eatTo(line, i, -1, 1024*1024, &eof, &err);
                trimWhitespace(&value);
                if (err) break;

                debug(CONFIGURATION,"%s = %s\n", key.c_str(), value.c_str());

                if (key == "type") {
		}
                else if (key == "path") {
		    if (value.back() == '/' && value.length()>1) {
			value.pop_back();
		    }
		    current_rule->path = value;
		    paths_[value] = current_rule;
		}
                else if (key == "history") { current_rule->history_path = value; }
                else if (key == "type") {  }
                else if (key == "cache") {
		    current_rule->cache_path = handlePath(current_rule->path, value, "cache");
		}
                else if (key == "cache_size") {}
                else if (key == "local") {
		    current_rule->local.target = handlePath(current_rule->path, value, "local");
 		}
                else if (key == "local_keep") { current_rule->local.keep = value; }
                else if (key == "args") { current_rule->args = value; }
                else if (key == "remote") { trimWhitespace(&value);
                                            current_rule->remotes[value] = Storage();
					    current_remote = &current_rule->remotes[value];
		                            current_remote->target = value; }
                else if (key == "remote_keep") { current_remote->keep = Keep(value);  }
		else if (key == "remote_round_robin") { current_remote->round_robin =
                                                            (value == "true") ? true: false; }
                else { error(CONFIGURATION, "Unknown key \"%s\" in configuration file!\n", key.c_str()); }
            }
        }
    }
    return true;
}

bool ConfigurationImplementation::load(vector<char> *buf) {
    int loadVector(Path *file, size_t blocksize, std::vector<char> *buf);

    char block[512];
    int fd = open("/home/fredrik/.config/beak/beak.conf", O_RDONLY);
    if (fd == -1) {
        return false;
    }
    while (true) {
        ssize_t n = read(fd, block, sizeof(block));
        if (n == -1) {
            if (errno == EINTR) {
                continue;
            }
            failure(CONFIGURATION,"Could not read from configuration file %s errno=%d\n",
                    "/home/fredrik/.beak.conf", errno);
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

std::vector<Storage*> Rule::sortedRemotes()
{
    std::vector<Storage*> v;
    for (auto & r : remotes) {
	v.push_back(&r.second);
    }
    std::sort(v.begin(), v.end(),
              [](Storage *a, Storage *b)->bool {
                  return (a->target > b->target);
	      });

    return v;
}

std::vector<Rule*> ConfigurationImplementation::sortedRules()
{
    std::vector<Rule*> v;
    for (auto & r : rules_) {
	v.push_back(&r.second);
    }
    std::sort(v.begin(), v.end(),
              [](Rule *a, Rule *b)->bool {
                  return (a->name > b->name);
	      });

    return v;
}

Rule *ConfigurationImplementation::rule(std::string name)
{
    if (rules_.count(name) == 0) return NULL;
    return &rules_[name];
}

void Rule::status() {

}

void Rule::output() {
    UI::output("Name:                %s\n", name.c_str());
    UI::output("Type:                %s\n", rule_type_names_[type]);
    UI::output("Path:                %s\n", path.c_str());
    UI::output("History path:        %s\n", history_path.c_str());
    UI::output("Cache path:          %s\n", cache_path.c_str());
    UI::output("Cache size:          %s\n", humanReadable(cache_size).c_str());
    UI::output("Local storage path:  %s\n", local.target.c_str());
//    UI::output("Keep rule: %s\n\n", keep.c_str());

    for (auto &r : remotes) {
	UI::output("Remote:          %s\n", r.second.target);
	UI::output("Keep:            %s\n", r.second.keep);
	UI::output("Round robin:     %s\n", r.second.round_robin ? "true" : "false");
    }
}

enum KeepOrChange {
    Keep,
    Change,
    Discard
};

KeepOrChange keepOrChange()
{
    for (;;) {
        UI::outputPrompt("Keep or change?\nk/c>");
        std::string c = UI::inputString();
        if (c == "k") {
            return Keep;
        }
        if (c == "c") {
            return Change;
        }
    }
}

KeepOrChange keepOrChangeOrDiscard()
{
    for (;;) {
        UI::outputPrompt("Keep,change or discard?\nk/c/d>");
        std::string c = UI::inputString();
        if (c == "k") {
            return Keep;
        }
        if (c == "c") {
            return Change;
        }
        if (c == "d") {
            return Discard;
        }
    }
}

Path *inputDirectory(const char *prompt) {
    for (;;) {
        UI::outputPrompt(prompt);
        Path *path = UI::inputPath();
        if (path == NULL) {
            UI::output("Path not found!\n");
            continue;
        }
        FileStat fs;
        if (!defaultFileSystem()->stat(path, &fs)) {
            UI::output("Not a proper path!\n");
            continue;
        }
        if (!fs.isDirectory()) {
            UI::output("Path is not a directory!\n");
            continue;
        }
        return path;
    }
}

bool ConfigurationImplementation::okRuleName(std::string name)
{
    if (rules_.count(name) > 0) {
        UI::output("Rule name already exists.\n");
        return false;
    }
    if (name.size() == 0) {
        UI::output("Rule name must not empty.\n");
        return false;
    }
    if (name.size() > 20) {
        UI::output("Rule name must not be longer than 20 characters.\n");
        return false;
    }
    if (name.find(":") != string::npos) {
        UI::output("Rule name must not contain a colon (:)\n");
        return false;
    }
    if (name.find("/") != string::npos) {
        UI::output("Rule name must not contain a slash (/)\n");
        return false;
    }
    if (name.find(" ") != string::npos) {
        UI::output("Rule name must not contain a space (' ')\n");
        return false;
    }
    return true;
}
void ConfigurationImplementation::editRule() {

}



void ConfigurationImplementation::createNewRule() {
    string name = "";
    Path *path = NULL;

    do {
        UI::outputPrompt("name>");
        name = UI::inputString();
    }
    while (!okRuleName(name));

    for (;;) {
        path = inputDirectory("path>");
        break;
    }

    std::vector<std::string> v = {
        "Local and remote backups",
        "Remote backups only",
        "Remote mount"
    };
    RuleType type = (RuleType)UI::inputChoice("Type of rule:", "type>", v);

    size_t cache_size = 1024U*1024U*1024U*10;
    Path *history_path = Path::lookup(".beak/history")->prepend(path);
    Path *cache_path = Path::lookup(".beak/.cache")->prepend(path);
    Path *local_path = Path::lookup(".beak/.local")->prepend(path);

    for (;;) {
        UI::output("\nproposed settings for cache and local storage\n");
	UI::output("---------------------------------------------\n");
        UI::output("History path: %s\n", history_path->c_str());
        UI::output("Cache path:          %s\n", cache_path->c_str());
        UI::output("Cache size:          %s\n", humanReadable(cache_size).c_str());
        UI::output("Local storage path:  %s\n", local_path->c_str());

        auto kc = keepOrChange();
        if (kc == Keep) {
             break;
        } else {

        }
    }

    std::string keep;

    for (;;) {
        UI::output("\nProposed setting for keeping/pruning backups\n");
	UI::output("---------------------------------------------\n");
        UI::output("keep = all:7d daily:2w weekly:2m monthly:2y\n\n");
        auto kc = keepOrChange();
        if (kc == Keep) {
            break;
        } else {

        }
    }

    Rule rule;
    rule.name = name;
    rule.type = type;
    rule.path = path->str();
    rule.history_path = history_path->str();
    rule.cache_path = cache_path->str();
    rule.cache_size = cache_size;
    rule.local.target = local_path->str();

    UI::output("Proposed new rule:\n\n");
    rule.output();
    UI::output("\n");

    auto kcd = keepOrChangeOrDiscard();
    if (kcd == Keep) {
        // Save to configuration file.
    } else if (kcd == Discard) {
        return;
    } else {
    }

}

int ConfigurationImplementation::configure() {

    for (;;) {
        UI::output("Current rules:\n\n");
        UI::output("%-20s %-20s\n", "Name", "Path");
        UI::output("%-20s %-20s\n", "====", "====");

        for (auto &l : rules_) {
            UI::output("%-20s %s\n", l.second.name.c_str(), l.second.path.c_str());
        }
        UI::output("\n");
        UI::output("e) Edit existing rule\n"
               "n) New rule\n"
               "d) Delete rule\n"
               "r) Rename rule\n"
               "c) Copy rule\n"
               "s) Set configuration password\n"
               "q) Quit config\n"
           "e/n/d/r/c/s/q>");

        string c = UI::inputString();

        if (c == "e") {
            editRule();
        }

        if (c == "n") {
            createNewRule();
        }
        if (c == "q") {
            break;
        }

    }

    return 0;
}

std::string keep_keys[] = { "all", "minutely", "hourly", "daily", "weekly", "monthly", "yearly" };

size_t calcTime(std::string s)
{
    char c = s.back();
    size_t scale = 0;
    if (c == 'i') scale = 60;
    if (c == 'h') scale = 3600;
    if (c == 'd') scale = 3600*24;
    if (c == 'd') scale = 3600*24;
    return scale;
}

Keep::Keep(std::string s)
{
/*    // Example:    "all:7d daily:2w weekly:2m monthly:2y yearly:forever"
    // Example:    "all:2d daily:1w monthly:12m"

    std::vector<char> data(s.begin(), s.end());
    auto i = data.begin();

    bool eof, err;

    while (true) {
        string key = eatTo(data, i, ':', 8, &eof, &err);
        if (eof || err) goto err;
        string value = eatTo(data, i, ' ', 16, &eof, &err);
        if (err) goto err;


        if (eof) break;
    }

ok:
    return;

err:
    error(CONFIGURATION, "Could not parse keep rule \"%s\"\n", s);
*/
}
