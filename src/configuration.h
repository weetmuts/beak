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

#define DEFAULT_KEEP "all:2d daily:2w weekly:2m monthly:2y"

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
// tz:+0100 all:2d daily:2w weekly:2m monthly:2y
// tz:+0100 all:forever
// tz:+0100 weekly:forever
// tz:+0100 daily:100d
// tz:+0100 mirror

struct Keep
{
    // Time zone offset. E.g. CET(Central European) = 3600 IST (Indian Standard) = 5.5*3600
    time_t tz_offset {};

    // The following values are absolute number of seconds back in time from now.
    // Number of seconds to keep all points in time. Zero means do not store using this interval.
    time_t all {};

    // Number of seconds to keep the last one per day
    time_t daily {};

    // Number of seconds to keep the last one per week
    time_t weekly {};

    // Number of seconds to keep the last one per month
    time_t monthly {};

    // Number of seconds to keep the last one per year
    time_t yearly {};

    // Never keep more than the latest backup. I.e. mirror.
    bool mirror {};

    Keep() = default;
    Keep(std::string s) { parse(s); }
    bool parse(std::string s);
    std::string str();
    // By default it keeps everything forever.
    bool keepAllForever() { return all==0 && daily==0 && weekly==0 && monthly==0 && yearly==0 and mirror==false; }
};

#define LIST_OF_STORAGE_TYPES \
    X(NoSuchStorage, "Not a storage")                                   \
    X(FileSystemStorage, "Store to a directory")                         \
    X(RCloneStorage,     "Store using rclone")                           \
    X(RSyncStorage,      "Store using rsync")                            \

enum StorageType : short {
#define X(name,info) name,
LIST_OF_STORAGE_TYPES
#undef X
};

struct Storage
{
    // Store or retrieve to/from local file system, rclone target, or rsync target.
    StorageType type {};
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

    // After "beak history work:" the full backup history, from local and remote
    // backup locations is mounted as a virtual file system on this path.
    Path *history_path {};

    // When mounting a remote storage for direct access, the tar files are
    // temporarily cached here, to speed up access.
    Path *cache_path {};

    // Maximum size of cache, before removing least recently used data.
    size_t cache_size {};

    // Use this storage for local backups. Then this local backups is rcloned to remote storages.
    // It points to storages[""] if NULL, then type is RemoteBackupsOnly or RemoteMount.
    Storage *local {};

    // If modified by the configuration ui, and not yet saved,
    bool needs_saving {};

    // All storages for this rule. Can be filesystems or rclone storages.
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

    virtual ~Configuration() = default;
};

std::unique_ptr<Configuration> newConfiguration(ptr<System> sys, ptr<FileSystem> fs);

#endif
