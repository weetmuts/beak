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

#include "beak.h"
#include "filesystem.h"
#include "log.h"
#include "configuration.h"
#include "system.h"
#include "tarfile.h"
#include "ui.h"
#include "util.h"

#include <algorithm>
#include <assert.h>
#include <fcntl.h>
#include <unistd.h>

using namespace std;

#define LIST_OF_RULE_KEYWORDS                                                       \
    X(origin,"Directory to be backed up.")                                            \
    X(type,"How to backup the directory, LocalAndRemote, RemoteOnly or MountOnly.") \
    X(cache,"When mounting remote storages cache files here.")                      \
    X(cache_size,"Maximum size of cache.")                                          \
    X(local,"Local directory for storage of backups.")                              \
    X(local_keep,"Keep rule for local storage.")                                    \

#define LIST_OF_STORAGE_KEYWORDS \
    X(remote,"Remote directory or rclone target for bacup storage.")                \
    X(remote_type,"FileSystemStorage or RCloneStorage.")                            \
    X(remote_keep,"Keep rule for local storage.")                                   \

enum RuleKeyWord : short {
#define X(name,info) name##_key,
LIST_OF_RULE_KEYWORDS
#undef X
};

enum StorageKeyWord : short {
#define X(name,info) name##_key,
LIST_OF_STORAGE_KEYWORDS
#undef X
};

const char *rule_keywords_[] {
#define X(name,info) #name,
LIST_OF_RULE_KEYWORDS
#undef X
};

const char *storage_keywords_[] {
#define X(name,info) #name,
LIST_OF_STORAGE_KEYWORDS
#undef X
};

const char *rule_type_names_[] = {
#define X(name,info) #name,
LIST_OF_TYPES
#undef X
};

const char *storage_type_names_[] = {
#define X(name,info) #name,
LIST_OF_STORAGE_TYPES
#undef X
};

// Logging must be enabled with env var BEAK_LOG_configuration since
// this code runs before command line parsing!
static ComponentId CONFIGURATION = registerLogComponent("configuration");

class ConfigurationImplementation : public Configuration
{
public:
    ConfigurationImplementation(ptr<System> sys, ptr<FileSystem> fs);

    bool load();
    bool save();
    RC configure();

    Rule *rule(string name);
    vector<Rule*> sortedRules();
    Rule *findRuleFromStorageLocation(Path *storage_location);
    Storage *findStorageFrom(Path *storage_location);
    Storage *createStorageFrom(Path *storage_location);

    void editRule();
    void createNewRule();
    void deleteRule();
    void renameRule();
    void copyRule();
    void outputRule(Rule *r, std::vector<ChoiceEntry> *buf = NULL);
    void outputStorage(Storage *s, std::vector<ChoiceEntry> *buf = NULL);
    Rule *deleteStorage(Rule *r);

    bool okRuleName(string name);
    pair<bool,StorageType> okStorage(string storage);

    void editName(Rule *r);
    void editPath(Rule *r);
    void editType(Rule *r);
    void editCachePath(Rule *r);
    void editCacheSize(Rule *r);
    void editLocalPath(Rule *r);
    void editLocalKeep(Rule *r);

    bool editRemoteTarget(Storage *r);
    void editRemoteKeep(Storage *r);

private:
    bool parseRow(string key, string value,
                  Rule *current_rule, Storage **current_storage);

    bool isFileSystemStorage(Path *storage_location);
    bool isRCloneStorage(Path *storage_location, string *type = NULL);
    bool isRSyncStorage(Path *storage_location);

    // Map rule name to rule.
    map<string,Rule> rules_;

    // Map path to rule.
    map<Path*,Rule*> paths_;

    ptr<System> sys_;
    ptr<FileSystem> fs_;
};

unique_ptr<Configuration> newConfiguration(ptr<System> sys, ptr<FileSystem> fs) {
    return unique_ptr<Configuration>(new ConfigurationImplementation(sys, fs));
}

ConfigurationImplementation::ConfigurationImplementation(ptr<System> sys, ptr<FileSystem> fs)
    : sys_(sys), fs_(fs)
{
}

Path *inputDirectory(ptr<FileSystem> fs, const char *prompt)
{
    for (;;) {
        UI::outputPrompt(prompt);
        Path *path = UI::inputPath();
        if (path == NULL) {
            UI::output("Path not found!\n");
            continue;
        }
        FileStat st;
        RC rc = fs->stat(path, &st);
        if (rc.isErr()) {
            UI::output("Not a proper path!\n");
            continue;
        }
        if (!st.isDirectory()) {
            UI::output("Path is not a directory!\n");
            continue;
        }
        return path;
    }
}

Path *realPath(Path *path, string more)
{
    if (more.front() == '/') {
	return Path::lookup(more);
    }
    if (!path) error(CONFIGURATION,
                     "Error in configuration file, the path must be supplied "
                     "before a relative path is used.\n");

    return path->append(more);
}

Path *relativePathIfPossible(Path *path, Path *curr)
{
    Path *common = Path::commonPrefix(path, curr);
    if (!common) return curr;
    if (common != path) return curr;

    return curr->subpath(path->depth());
}

bool ConfigurationImplementation::parseRow(string key, string value,
                                           Rule *current_rule, Storage **current_storage)
{
    RuleKeyWord rkw;
    bool ok;
    debug(CONFIGURATION,"loading %s:%s for rule %s\n", key.c_str(), value.c_str(), current_rule->name.c_str());
    lookupType(key,RuleKeyWord,rule_keywords_,rkw,ok);
    if (ok) {
        switch (rkw) {
        case origin_key:
            current_rule->origin_path = Path::lookup(value);
            paths_[current_rule->origin_path] = current_rule;
            current_rule->generateDefaultSettingsBasedOnPath();
            break;
        case type_key:
        {
            RuleType rt = LocalThenRemoteBackup;
            lookupType(value,RuleType,rule_type_names_,rt,ok);
            if (!ok) error(CONFIGURATION, "No such rule type \"%s\"\n", value.c_str());
            current_rule->type = rt;
        }
            break;
        case cache_key:
            current_rule->cache_path = realPath(current_rule->origin_path, value);
            break;
        case cache_size_key:
            {
                RC rc = parseHumanReadable(value, &current_rule->cache_size);
                if (rc.isErr()) error(CONFIGURATION, "Could not parse cache size \"%s\"\n", value.c_str());
            }
            break;
        case local_key:
            {
                Storage *local = &current_rule->storages[Path::lookupRoot()];
                current_rule->local = local;
                local->storage_location = realPath(current_rule->origin_path, value);
                local->type = FileSystemStorage;
            }
            break;
        case local_keep_key:
            if (!current_rule->local) {
                error(CONFIGURATION, "Local path must be specified before local keep rule.\n");
            }
            if (!current_rule->local->keep.parse(value)) {
                error(CONFIGURATION, "Invalid keep rule \"%s\".\n", value.c_str());
            }
            break;
        }
        return true;
    }

    StorageKeyWord skw;
    lookupType(key,StorageKeyWord,storage_keywords_,skw,ok);

    if (ok) {
        switch (skw) {
        case remote_key:
        {
            if (value == "") error(CONFIGURATION, "Remote storage cannot be empty.\n");
            Path *storage_location = Path::lookup(value);
            Rule *rule = findRuleFromStorageLocation(storage_location);
            if (rule) {
                error(CONFIGURATION, "The remote storage location \"%s\" is used in two rules!\n", value.c_str());
            }
            *current_storage = &current_rule->storages[storage_location];
            (*current_storage)->storage_location = storage_location;
            break;
        }
        case remote_type_key:
            if (!current_storage) {
                error(CONFIGURATION, "Remote must be specified before type.\n");
            }
            {
                StorageType st {};
                lookupType(value,StorageType,storage_type_names_,st,ok);
                if (!ok) error(CONFIGURATION, "No such storage type \"%s\"\n", value.c_str());
                (*current_storage)->type = st;
            }
            break;
        case remote_keep_key:
            if (!(*current_storage)) {
                error(CONFIGURATION, "Remote must be specified before keep rule.\n");
            }
            if (!(*current_storage)->keep.parse(value)) {
                error(CONFIGURATION, "Invalid keep rule \"%s\".\n", value.c_str());
            }
            break;
        }
        return true;
    }

    error(CONFIGURATION, "Invalid key \"%s\".", key.c_str());
    return false;
}

bool ConfigurationImplementation::load()
{
    rules_.clear();
    paths_.clear();
    vector<char> buf;
    Path *config = configurationFile();
    RC rc = fs_->loadVector(config, 32768, &buf);
    if (rc.isOk()) {
        vector<char>::iterator i = buf.begin();
        bool eof=false, err=false;
        Rule *current_rule = NULL;
	Storage *current_storage = NULL;

        while (true) {
            eatWhitespace(buf,i,&eof);
            if (eof) break;
            string block = eatTo(buf,i,'\n', 1024*1024, &eof, &err);
            if (err) break; // eof is ok here, the last line might not be terminated with a \n
            trimWhitespace(&block);
            // Ignore empty lines
            if (block.length() == 0) continue;
            // Ignore comment lines
            if (block[0] == '#') continue;
            if (block[0] == '[' && block.back() == ']') {
                // Found the start of a new target
                string name = block.substr(1,block.length()-2);
                trimWhitespace(&name);
                if (rules_.count(name) > 0) {
                    error(CONFIGURATION, "Duplicate rule [%s] found in configuration file!\n");
                }
                rules_[name] = Rule();
                current_rule = &rules_[name];
                current_rule->name = name;
                debug(CONFIGURATION,"loading configuration for rule [%s]\n", name.c_str());
            } else {
                vector<char> line(block.begin(), block.end());
                // Like in bash, a backslash at the end of the line means
                // include the next line.
                while (line.back() == '\\') {
                    line.pop_back();
                    block = eatTo(buf,i,'\n', 1024*1024, &eof, &err);
                    if (eof || err) break;
                    line.insert(line.end(), block.begin(), block.end());
                }
                if (err) break; // eof is ok here, the last line might not be terminated with a \n

                auto i = line.begin();
                string key = eatTo(line, i, '=', 1024*1024, &eof, &err);
                trimWhitespace(&key);
                if (eof || err) break;
                string value = eatTo(line, i, -1, 1024*1024, &eof, &err);
                trimWhitespace(&value);
                if (err) break;
                parseRow(key, value, current_rule, &current_storage);
            }
        }
    }
    return true;
}

bool ConfigurationImplementation::save()
{
    string conf;
    for (auto rule : sortedRules())
    {
        conf += "[" + rule->name + "]\n";
        conf += "origin = " + rule->origin_path->str() + "\n";
        conf += "type = " + string(rule_type_names_[rule->type]) + "\n";
        conf += "cache = " + relativePathIfPossible(rule->origin_path, rule->cache_path)->str() + "\n";
        conf += "cache_size = " + humanReadable(rule->cache_size) + "\n";
        if (rule->type == LocalThenRemoteBackup) {
            conf += "local = " + relativePathIfPossible(rule->origin_path, rule->local->storage_location)->str() + "\n";
            conf += "local_keep = " + rule->local->keep.str() + "\n";
        }

        for (auto storage : rule->sortedStorages())
        {
            if (storage != rule->local) {
                conf += "remote = " + storage->storage_location->str() + "\n";
                conf += "remote_type = " + string(storage_type_names_[storage->type]) + "\n";
                conf += "remote_keep = " + storage->keep.str() + "\n";
            }
        }
    }
    vector<char> buf(conf.begin(), conf.end());
    fs_->createFile(configurationFile(), &buf);

    UI::output("Configuration saved!\n\n");
    load();
    return true;
}

vector<Storage*> Rule::sortedStorages()
{
    vector<Storage*> v;
    for (auto & r : storages)
    {
        v.push_back(&r.second);
    }
    sort(v.begin(), v.end(),
              [](Storage *a, Storage *b)->bool {
             return (a->storage_location->str() < b->storage_location->str());
	      });

    return v;
}

Storage* Rule::storage(Path *storage_location)
{
    if (storages.count(storage_location) > 0) {
        return &storages[storage_location];
    }
    return NULL;
}

void Rule::generateDefaultSettingsBasedOnPath()
{
    cache_path = realPath(origin_path, ".beak/cache");

    cache_path = Path::lookup(".beak/cache");
    cache_size = 10ul+1024*1024*1024;

    string keep = DEFAULT_LOCAL_KEEP_RULE;

    storages[Path::lookupRoot()] = { FileSystemStorage, Path::lookup(".beak/local"), keep };
    local = &storages[Path::lookupRoot()];
}

void ConfigurationImplementation::editName(Rule *r)
{
    for (;;) {
        UI::outputPrompt("name>");
        string old_name = r->name;
        r->name = UI::inputString();
        if (r->name != old_name) r->needs_saving = true;
        if (okRuleName(r->name)) break;
    }
}

void ConfigurationImplementation::editPath(Rule *r)
{
    r->origin_path = inputDirectory(fs_, "path>");
}

void ConfigurationImplementation::editType(Rule *r)
{
    vector<ChoiceEntry> v = {
        { "Local and remote backups" },
        { "Remote backups only" },
        { "Remote mount" }
    };
    ChoiceEntry *ce = UI::inputChoice("Type of rule:", "type>", v);
    r->type = (RuleType)ce->index;
}

void ConfigurationImplementation::editCachePath(Rule *r)
{
    r->cache_path = inputDirectory(fs_, "cache path>");
}

void ConfigurationImplementation::editCacheSize(Rule *r)
{
    for (;;) {
        UI::outputPrompt("cache size>");
        string s = UI::inputString();
        RC rc = parseHumanReadable(s, &r->cache_size);
        if (rc.isOk()) break;
        UI::output("Invalid cache size.\n");
    }
}

void ConfigurationImplementation::editLocalPath(Rule *r)
{
    assert(r->local);
    r->local->storage_location = inputDirectory(fs_, "local path>");
}

void ConfigurationImplementation::editLocalKeep(Rule *r)
{
    assert(r->local);
    for (;;) {
        UI::outputPrompt("local keep>");
        string k = UI::inputString();
        bool ok = r->local->keep.parse(k);
        if (ok) break;
        UI::output("Invalid keep rule.\n");
    }
}

vector<Rule*> ConfigurationImplementation::sortedRules()
{
    vector<Rule*> v;
    for (auto & r : rules_) {
	v.push_back(&r.second);
    }
    sort(v.begin(), v.end(),
              [](Rule *a, Rule *b)->bool {
                  return (a->name < b->name);
	      });

    return v;
}

Rule *ConfigurationImplementation::rule(string name)
{
    auto colon = name.find(':');
    if (colon != string::npos && name.back() == ':')
    {
        name.pop_back();
    }

    if (rules_.count(name) == 0) return NULL;
    return &rules_[name];
}

Rule *ConfigurationImplementation::findRuleFromStorageLocation(Path *storage_location)
{
    Path *rp = storage_location->realpath();
    if (rp) {
        storage_location = rp;
    }

    for (auto rule : sortedRules())
    {
        for (auto storage : rule->sortedStorages())
        {
            if (storage->storage_location == storage_location)
            {
                return rule;
            }
        }
    }
    return NULL;
}

void Rule::status() {
}

void ConfigurationImplementation::outputRule(Rule *r, std::vector<ChoiceEntry> *buf)
{
    string msg;

    strprintf(msg, "Name:         %s", r->name.c_str());
    if (!buf) UI::outputln(msg);
    else buf->push_back(ChoiceEntry( msg, [=](){ editName(r); }));

    strprintf(msg, "Path:         %s", r->origin_path->c_str());
    if (!buf) UI::outputln(msg);
    else buf->push_back(ChoiceEntry( msg, [=](){ editPath(r); }));

    strprintf(msg, "Type:         %s", rule_type_names_[r->type]);
    if (!buf) UI::outputln(msg);
    else buf->push_back(ChoiceEntry( msg, [=](){ editType(r); }));

    strprintf(msg, "Cache path:   %s", relativePathIfPossible(r->origin_path, r->cache_path)->c_str());
    if (!buf) UI::outputln(msg);
    else buf->push_back(ChoiceEntry( msg, [=](){ editCachePath(r); }));

    strprintf(msg, "Cache size:   %s", humanReadable(r->cache_size).c_str());
    if (!buf) UI::outputln(msg);
    else buf->push_back(ChoiceEntry( msg, [=](){ editCacheSize(r); }));

    if (r->type == LocalThenRemoteBackup) {
        strprintf(msg, "Local:        %s", relativePathIfPossible(r->origin_path, r->local->storage_location)->c_str());
        if (!buf) UI::outputln(msg);
        else buf->push_back(ChoiceEntry( msg, [=](){ editLocalPath(r); }));

        strprintf(msg, "Keep:         %s", r->local->keep.str().c_str());
        if (!buf) UI::outputln(msg);
        else buf->push_back(ChoiceEntry( msg, [=](){ editLocalKeep(r); }));
    }

    for (auto &s : r->sortedStorages()) {
        if (s != r->local) {
            outputStorage(s, buf);
        }
    }
}

bool isDirectory(ptr<FileSystem> fs, Path *path)
{
    FileStat st;
    RC rc = fs->stat(path, &st);
    if (rc.isErr()) {
        return false;
    }
    if (!st.isDirectory()) {
        return false;
    }
    return true;
}


bool ConfigurationImplementation::okRuleName(string name)
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

pair<bool,StorageType> ConfigurationImplementation::okStorage(string storage)
{
    if (storage.size() == 0) {
        return { false, FileSystemStorage };
    }
    size_t cp = storage.find(":");
    if (cp != string::npos) {
        Path *rclone = Path::lookup(storage.substr(0, cp+1));
        string type;
        bool ok = isRCloneStorage(rclone, &type);
        if (ok) {
            // This is an rclone rule.
            UI::output("Storage identified as an rclone storage.\n");
            if (type != "crypt") {
                UI::output("The rclone rule \"%s\" is not encrypted!\n", storage.c_str());
                auto kc = UI::keepOrChange();
                if (kc == UIChange) return { false, FileSystemStorage };
            }
            return { true, RCloneStorage };
        }
    }
    Path *p = Path::lookup(storage);
    if (isDirectory(fs_, p)) {
        // This is a plain directory
        UI::output("Storage identified as directory.\n");
        return { true, FileSystemStorage };
    }

    UI::output("Neither an rclone storage nor a directory.\n");
    return { false, FileSystemStorage };
}

void ConfigurationImplementation::editRule()
{
    vector<ChoiceEntry> v;
    for (auto r : sortedRules()) {
        v.push_back({ r->name });
    }
    auto ce = UI::inputChoice("Which rule to edit:", "rule>", v);
    string s = v[ce->index].keyword;
    Rule *r = &rules_[s];

    for (;;) {
        vector<ChoiceEntry> c;
        outputRule(r, &c);
        c.push_back({"a", "", "Add storage"});
        c.push_back({"e", "", "Erase storage"});
        if (r->needs_saving) {
            c.push_back({"s", "", "Save (unsaved data exists!)"});
            c.push_back({"d", "", "Discard changes"});
        } else {
            c.push_back({"q", "", "Exit to main menu"});
        }
        auto ce = UI::inputChoice("Which rule data to edit:", "\n>", c);
        if (ce->key == "a") {
            Storage storage;
            bool b = editRemoteTarget(&storage);
            if (b) {
                editRemoteKeep(&storage);
                r->storages[storage.storage_location] = storage;
            }
        }
        else if (ce->key == "e") {
            r = deleteStorage(r);
        } else
        if (ce->key == "s") {
            save();
            break;
        } else
        if (ce->key == "d" || ce->key == "q") {
            break;
        }
    }
}

void ConfigurationImplementation::renameRule()
{
    vector<ChoiceEntry> v;
    for (auto r : sortedRules()) {
        v.push_back({ r->name });
    }
    auto ce = UI::inputChoice("Which rule to rename:", "rule>", v);
    string s = v[ce->index].keyword;
    Rule *r = &rules_[s];

    UI::output("Enter new name for \"%s\" rule.\n", r->name.c_str());
    editName(r);
    save();
}

void ConfigurationImplementation::copyRule()
{
    vector<ChoiceEntry> v;
    for (auto r : sortedRules()) {
        v.push_back({ r->name });
    }
    auto ce = UI::inputChoice("Which rule to copy:", "rule>", v);
    string s = v[ce->index].keyword;
    Rule *r = &rules_[s];

    UI::output("Enter name for copy of \"%s\" rule.\n", r->name.c_str());

    Rule copy = *r;
    editName(&copy);
    rules_[copy.name] = copy;
    save();
}

void ConfigurationImplementation::createNewRule()
{
    Rule rule;
    editName(&rule);
    editPath(&rule);
    editType(&rule);
    rule.generateDefaultSettingsBasedOnPath();

    // Ask for storages
    for (;;) {
        UI::output("\nAdd preconfigured storage. Empty line to stop adding.\n\n");
        Storage storage;
        bool b = editRemoteTarget(&storage);
        if (!b) break;
        editRemoteKeep(&storage);
        rule.storages[storage.storage_location] = storage;
    }

    UI::output("Proposed new rule:\n\n");
    outputRule(&rule);
    UI::output("\n");

    auto kcd = UI::keepOrChangeOrDiscard();
    if (kcd == UIKeep) {
        // Save to configuration file.
        rules_[rule.name] = rule;
        //paths_[rule.path] = rule;
        save();
    } else if (kcd == UIDiscard) {
        return;
    } else {
    }

}

void ConfigurationImplementation::deleteRule()
{
    vector<ChoiceEntry> choices;
    for (auto r : sortedRules()) {
        choices.push_back( { r->name } );
    }
    auto ce = UI::inputChoice("Which rule to delete:", "rule>", choices);
    string s = choices[ce->index].keyword;

    auto answer = UI::yesOrNo("Really delete?");

    if (answer == UIYes) {
        rules_.erase(s);
        save();
    }
}

Rule *ConfigurationImplementation::deleteStorage(Rule *r)
{
    vector<ChoiceEntry> choices;
    for (auto s : r->sortedStorages()) {
        choices.push_back( { s->storage_location->str() } );
    }
    auto ce = UI::inputChoice("Which storage to delete:", "storage>", choices);
    string s = ce->keyword;

    auto answer = UI::yesOrNo("Really delete?");

    if (answer == UIYes) {
        string rule_name = r->name;
        r->storages.erase(Path::lookup(s));
        save();
        return &rules_[rule_name];
    }
    return r;
}

RC ConfigurationImplementation::configure()
{
    vector<ChoiceEntry> choices;
    choices.push_back( { "e", "", "Edit existing rule",         [=]() { editRule(); } } );
    choices.push_back( { "n", "", "New rule",                   [=]() { createNewRule(); } } );
    choices.push_back( { "d", "", "Delete rule",                [=]() { deleteRule(); } } );
    choices.push_back( { "r", "", "Rename rule",                [=]() { renameRule(); } } );
    choices.push_back( { "c", "", "Copy rule",                  [=]() { copyRule(); } } );
    choices.push_back( { "q", "", "Quit config",                [=]() {  } } );

    for (;;) {
        UI::output("Current rules:\n\n");
        UI::output("%-20s %-20s\n", "Name", "Origin");
        UI::output("%-20s %-20s\n", "====", "======");

        for (auto &l : rules_) {
            UI::output("%-20s %s\n", l.second.name.c_str(), l.second.origin_path->c_str());
        }

        auto ce = UI::inputChoice("","e/n/d/r/c/s/q>", choices);
        if (ce->key == "q") break;
    }

    return RC::OK;
}

string keep_keys[] = { "all", "daily", "weekly", "monthly" };

size_t calcTime(string s)
{
    char c = s.back();
    size_t scale = 0;
    if (c == 'i') scale = 60;
    if (c == 'h') scale = 3600;
    if (c == 'd') scale = 3600*24;
    if (c == 'w') scale = 3600*24;
    return scale;
}

bool Keep::parse(string s)
{
    // Example:    "all:2d daily:2w weekly:2m monthly:2y"
    // Example:    "all:2d daily:1w monthly:12m"
    vector<char> data(s.begin(), s.end());
    auto i = data.begin();
    string tz, offset;
    bool eof, err, ok;
    int level = 0; // 0=all 1=daily, 2=weekly, 3=monthly

    all = daily = weekly = monthly = 0;

    while (true) {
        string key = eatToSkipWhitespace(data, i, ':', 16, &eof, &err);
        if (eof || err) goto err;
        string value = eatToSkipWhitespace(data, i, ' ', 16, &eof, &err);
        if (!eof && err) goto err;
        time_t len = 0;
        ok = parseLengthOfTime(value, &len);

        if (!ok) return false;

        if (key == "all") {
            if (level > 0) return false;
            level=1;
            all = len;
        }
        else if (key == "daily") {
            if (level > 1) return false;
            level=2;
            daily = len;
        }
        else if (key == "weekly") {
            if (level > 2) return false;
            level=3;
            weekly = len;
        }
        else if (key == "monthly") {
            if (level > 3) return false;
            level=4;
            monthly = len;
        }

        if (eof) break;
    }

    return true;

err:
    return false;
}

string Keep::str()
{
    string s;

    if (all) s += "all:"+getLengthOfTime(all)+" ";
    if (daily) s += "daily:"+getLengthOfTime(daily)+" ";
    if (weekly) s += "weekly:"+getLengthOfTime(weekly)+" ";
    if (monthly) s += "monthly:"+getLengthOfTime(monthly)+" ";
    if (s.back() == ' ') s.pop_back();
    return s;
}

bool Keep::subsetOf(const Keep &keep) {
    if (all > keep.all) return false;
    if (daily > keep.daily) return false;
    if (weekly > keep.weekly) return false;
    if (monthly > keep.monthly) return false;
    return true;
}

void ConfigurationImplementation::outputStorage(Storage *s, std::vector<ChoiceEntry> *buf)
{
    string msg;

    strprintf(msg, "      Remote: %s", s->storage_location->c_str());
    if (!buf) UI::outputln(msg);
    else buf->push_back(ChoiceEntry(msg, [=](){ editRemoteTarget(s); }));

    strprintf(msg, "        Type: %s", storage_type_names_[s->type]);
    if (!buf) UI::outputln(msg);
    else {
        buf->push_back(ChoiceEntry(msg));
        buf->back().available = false;
    }

    strprintf(msg, "        Keep: %s", s->keep.str().c_str());
    if (!buf) UI::outputln(msg);
    else buf->push_back(ChoiceEntry(msg, [=](){ editRemoteKeep(s); }));
}

bool ConfigurationImplementation::editRemoteTarget(Storage *s)
{
    for (;;) {
        UI::outputPrompt("remote>");
        string storage = UI::inputString();
        if (storage == "") return false;
        auto r = okStorage(storage);
        if (!r.first) continue;
        Path *storage_location = Path::lookup(storage);
        Rule *rule = findRuleFromStorageLocation(storage_location);
        if (rule) {
            UI::output("The storage location \"%s\" is already used in the rule %s!\n", storage.c_str(), rule->name.c_str());
            UI::output("Try again.\n");
            continue;
        }
        s->storage_location = storage_location;
        s->type = r.second;
        break;
    }
    return true;
}

void ConfigurationImplementation::editRemoteKeep(Storage *s)
{
    for (;;) {
        UI::output("Empty keep string means => all:2d daily:2w weekly:2m monthly:2y\n");
        UI::outputPrompt("remote keep>");
        string k = UI::inputString();
        if (k == "") {
            UI::output("Using default keep: all:2d daily:2w weekly:2m monthly:2y\n");
            k = "all:2d daily:2w weekly:2m monthly:2y";
        }
        bool ok = s->keep.parse(k);
        if (ok) break;
        UI::output("Invalid keep rule.\n");
    }
}

bool has_index_files_or_is_empty_(FileSystem *fs, Path *path)
{
    if (path == NULL) return false;

    vector<Path*> contents;
    if (!fs->readdir(path, &contents)) {
        return false;
    }
    if (contents.size() == 2)
    {
        // Only . and ..
        return true;
    }
    for (auto f : contents)
    {
        TarFileName tfn;
        bool ok = tfn.parseFileName(f->str());
        if (ok && tfn.type == REG_FILE)
        {
            return true;
        }
    }
    return false;
}

bool ConfigurationImplementation::isFileSystemStorage(Path *storage_location)
{
    Path *rp = storage_location->realpath();
    if (!rp)
    {
        return false;
    }

    FileStat stat;
    RC rc = fs_->stat(rp, &stat);
    if (rc.isErr() || !stat.isDirectory())
    {
        return false;
    }
    return has_index_files_or_is_empty_(fs_, storage_location);
}

bool ConfigurationImplementation::isRCloneStorage(Path *storage_location, string *type)
{
    string arg = storage_location->str();
    auto colon = arg.find(':');
    if (colon == string::npos) return false;

    string name = arg.substr(0,colon+1);

    map<string,string> configs;
    vector<char> out;
    vector<string> args;
    args.push_back("listremotes");
    args.push_back("-l");
    RC rc = sys_->invoke("rclone", args, &out);
    if (rc.isOk()) {
        auto i = out.begin();
        bool eof, err;

        for (;;) {
            eatWhitespace(out, i, &eof);
            if (eof) break;
            string target = eatTo(out, i, ':', 4096, &eof, &err);
            trimWhitespace(&target);
            target += ':';
            if (eof || err) break;
            string type = eatTo(out, i, '\n', 64, &eof, &err);
            if (err) break;
            trimWhitespace(&type);
            configs[target] = type;
        }

        if (configs.count(name) > 0) {
            if (type) {
                *type = configs[name];
            }
            return true;
        }
    }

    return false;
}

bool ConfigurationImplementation::isRSyncStorage(Path *storage_location)
{
    // An rsync location is detected by the @ sign and the server :.
    string name = storage_location->str();
    size_t at = name.find('@');
    size_t colon = name.find(':');
    if (at != string::npos &&
        colon != string::npos &&
        at < colon)
    {
        return true;
    }

    return false;
}

// Storage created on the fly depending on the command line arguments.
Storage a_storage;

Storage *ConfigurationImplementation::findStorageFrom(Path *storage_location)
{
    // This is a storage that is configured inside a rule.
    Rule *rule = findRuleFromStorageLocation(storage_location);

    if (rule)
    {
        return rule->storage(storage_location);
    }

    // Ok, not a known storage location.
    // Let us check if it is rclone, rsync or a directory.
    if (isRCloneStorage(storage_location))
    {
        a_storage.type = RCloneStorage;
        a_storage.storage_location = storage_location;
        return &a_storage;
    }
    else if (isRSyncStorage(storage_location))
    {
        a_storage.type = RSyncStorage;
        a_storage.storage_location = storage_location;
        return &a_storage;
    }
    else
    {
        Path *rp = storage_location->realpath();
        if (rp) {
            storage_location = rp;
        }
        if (isFileSystemStorage(storage_location))
        {
            a_storage.type = FileSystemStorage;
            a_storage.storage_location = storage_location;
            return &a_storage;
        }
    }

    return NULL;
}

Storage *ConfigurationImplementation::createStorageFrom(Path *storage_location)
{
    Path *rp = storage_location->realpath();
    if (rp) {
        storage_location = rp;
    }
    FileStat stat;
    RC rc = fs_->stat(storage_location, &stat);
    if (rc.isOk()) {
        // Something exists, do not create a storage here.
        return NULL;
    }
    bool b = fs_->mkDirpWriteable(storage_location);
    if (!b) {
        error(CONFIGURATION, "Could not create directory %s\n", storage_location->c_str());
    }
    info(CONFIGURATION, "Created storage directory %s\n", storage_location->c_str());
    if (isFileSystemStorage(storage_location))
    {
        a_storage.type = FileSystemStorage;
        a_storage.storage_location = storage_location;
        return &a_storage;
    }

    return NULL;
}
