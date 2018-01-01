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

#ifndef CONFIGURATION_H
#define CONFIGURATION_H

#include<map>
#include<memory>
#include<string>
#include<vector>

#define LIST_OF_TYPES \
    X(LocalAndRemoteBackups,"Store locally and remotely")                          \
    X(RemoteBackupsOnly,"Minimize local storage")                                  \
    X(RemoteMount,"Only mount remote backups")                                     \

enum RuleType : short {
#define X(name,info) name,
LIST_OF_TYPES
#undef X
};

struct Keep {
    size_t all;
    size_t daily;
    size_t weekly;
    size_t monthly;
    size_t yearly;

    Keep() : all(0), daily(0), weekly(0), monthly(0), yearly(0) { }
    Keep(std::string s);
};

struct Storage {
    // A path or a name identifying the remote. Typically an rclone target (include the colon)
    // or a full path to a directory.
    std::string target;
    // The keep rule for the remote.
    Keep keep;
    // If pushes should round robin through the available remotes.
    // The push happens to the remote with the least recent timestamp.
    bool round_robin;
    // If the target is a path to a removable storage (typically USB drive)
    // then push here when it is mounted.
    bool on_mount;
};

struct Rule {
    // The rule identifier.
    std::string name;

    // LocalAndRemote backups, Remote backups only, or Remote mount only.
    RuleType type;

    // The path in the local file system to backed up.
    std::string path;

    // Additional arguments that affect how the tar files are choosen and sized.
    std::string args;

    // After "beak history work:" the full backup history, from local and remote
    // backup locations is mounted as a virtual file system on this path.
    std::string history_path;

    // When mounting a remote backup for direct access, the tar files are
    // temporarily cached here, to speed up access.
    std::string cache_path;

    // Maximum size of cache, before removing least recently used data.
    size_t cache_size;

    // If local backups, then store a backup here, before it is rcloned to a remote backup.
    Storage local;

    // Local/Remote storages for this rule.
    std::map<std::string,Storage> remotes;

    void output();
    void status();
    std::vector<Storage*> sortedRemotes();
};

struct FileSystem;

struct Configuration
{
    virtual bool load() = 0;
    virtual int configure() = 0;

    virtual Rule *rule(std::string name) = 0;
    virtual std::vector<Rule*> sortedRules() = 0;
};

std::unique_ptr<Configuration> newConfiguration(FileSystem *fs);

#endif
