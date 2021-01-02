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

#include "always.h"
#include "filesystem.h"
#include "ui.h"

#include<map>
#include<memory>
#include<string>
#include<vector>

#define LIST_OF_TYPES \
    X(LocalThenRemoteBackup,"Store locally then remotely")           \
    X(RemoteBackup,"Store remotely")                                 \
    X(RemoteMount,"Mount remote backup")                             \

enum RuleType : short {
#define X(name,info) name,
LIST_OF_TYPES
#undef X
};

// Keep examples:
// all:2d daily:2w weekly:2m monthly:2y
// all:forever
// weekly:forever
// daily:100d
// mirror

struct Keep
{
    // The following values are absolute number of seconds back in time from now.
    // Number of seconds to keep all points in time. Zero means do not store using this interval.
    uint64_t all {};

    // Number of seconds to keep the last one per day
    uint64_t daily {};

    // Number of seconds to keep the last one per week
    uint64_t weekly {};

    // Number of seconds to keep the last one per month
    uint64_t monthly {};

    Keep() = default;
    Keep(std::string s) { parse(s); }
    bool parse(std::string s);
    std::string str();
    // Return true if a storage pruned with this keep rule is a subset of
    // the same storage pruned with the k rule.
    bool subsetOf(const Keep &k);

    bool equals(Keep& k) { return all==k.all && daily==k.daily && weekly==k.weekly && monthly==k.monthly; }
};

#define LIST_OF_STORAGE_TYPES \
    X(NoSuchStorage, "Not a storage")                                   \
    X(FileSystemStorage, "Store to a directory")                         \
    X(RCloneStorage,     "Store using rclone")                           \
    X(RSyncStorage,      "Store using rsync")                            \

#define LIST_OF_STORAGE_USAGES \
    X(Always, "Always")                                  \
    X(RoundRobin, "Round robin")                         \
    X(IfAvailable, "If available")                       \
    X(WhenRequested, "When requested")                   \


enum StorageType : short {
#define X(name,info) name,
LIST_OF_STORAGE_TYPES
#undef X
};

enum StorageUsage : short {
#define X(name,info) name,
LIST_OF_STORAGE_USAGES
#undef X
};

struct Storage
{
    // Store or retrieve to/from local file system, rclone target, or rsync target.
    StorageType type {};
    // How to use this storage, always store here, round robin between other rr storages,
    // if available (typicall usb storage locations) or when requested (more expensive storage)
    StorageUsage usage {};
    // Storage location is either a filesystem path, or an rclone target (eg s3_work_crypt: or s3:/prod/bar)
    // or an rsync target (eg backup@192.168.0.1:/backups/)
    Path *storage_location {};
    // The keep rule for the storage, default setting is keep everything.
    Keep keep;

    Storage() = default;
    Storage(StorageType ty, Path *sl, std::string ke) : type(ty), storage_location(sl), keep(ke) { }
    void output(std::vector<ChoiceEntry> *buf = NULL);

    void editStorageLocation();
    void editKeep();
};

struct Rule {
    // The rule identifier.
    std::string name {};

    // LocalAndRemote backups, Remote backups only, or Remote mount only.
    RuleType type {};

    // The path in the local file system to back up.
    Path *origin_path {};

    // Additional arguments that affect how the tar files are choosen and sized.
    std::string args;

    // When mounting a remote storage for direct access, the tar files are
    // temporarily cached here, to speed up access.
    Path *cache_path {};

    // Maximum size of cache, before removing least recently used data.
    size_t cache_size {};

    // Use this storage for local backups. Then this local backups is rcloned to remote storages.
    // It points to storages[""] if NULL, then type is RemoteBackupsOnly or RemoteMount.
    Storage local;

    // All storages for this rule. Can be filesystem, rclone or rsync storages.
    // Ie the path can be a proper filesystem path, or s3_work_crypt: or foo@host:/backup
    std::map<Path*,Storage> storages;

    void output(std::vector<ChoiceEntry> *buf = NULL);
    void status();
    std::vector<Storage*> sortedStorages();
    Storage *storage(Path *storage_location);

    void generateDefaultSettingsBasedOnPath();
};

struct System;
struct FileSystem;

struct Configuration
{
    virtual bool load() = 0;
    virtual bool save() = 0;
    virtual RC configure() = 0;

    virtual Rule *rule(std::string name) = 0;
    virtual std::vector<Rule*> sortedRules() = 0;
    virtual Rule *findRuleFromStorageLocation(Path *storage_location) = 0;
    virtual Storage *findStorageFrom(Path *storage_location) = 0;
    virtual Storage *createStorageFrom(Path *storage_location) = 0;

    virtual ~Configuration() = default;
};

std::unique_ptr<Configuration> newConfiguration(ptr<System> sys, ptr<FileSystem> fs, Path *beak_conf);

#endif
