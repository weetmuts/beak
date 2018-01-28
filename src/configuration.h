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

#define DEFAULT_KEEP "CET all:2d daily:2w weekly:2m monthly:2y"

#define LIST_OF_TYPES \
    X(LocalAndRemoteBackups,"Store locally and remotely")                          \
    X(RemoteBackupsOnly,"Minimize local backup space")                             \
    X(RemoteMount,"Only mount remote backups")                                     \

enum RuleType : short {
#define X(name,info) name,
LIST_OF_TYPES
#undef X
};

#define LIST_OF_STORAGE_TYPES \
    X(FileSystemStorage, "Push to a directory")                         \
    X(RCloneStorage,     "Push using rclone")                           \

enum StorageType : short {
#define X(name,info) name,
LIST_OF_STORAGE_TYPES
#undef X
};

struct Keep
{
    size_t all;
    size_t daily;
    size_t weekly;
    size_t monthly;
    size_t yearly;

    Keep() : all(0), daily(0), weekly(0), monthly(0), yearly(0) { }
    Keep(std::string s) { parse(s); }
    bool parse(std::string s);
    std::string str();
};

struct Storage
{
    // Either an rclone target (eg s3_work_crypt:/prod/bar) or
    // a full path to a directory (eg /home/backups/prod/bar)
    Path *target;
    // How the target string should be interpreted, filesystem, rclone, rsync, or command line.
    StorageType type;
    // The keep rule for the storage
    Keep keep;
    // If pushes should round robin through the available storages
    // The push happens to the storage with the least recent timestamp.
    bool round_robin;
    // If the target is a path to a removable storage (typically USB drive)
    // then trigger a push when it is mounted.
    bool on_mount;

    Storage() { };
    Storage(Path *t, StorageType e, std::string k) :
    target(t), type(e), keep(k), round_robin(false), on_mount(false) { }
    void output(std::vector<ChoiceEntry> *buf = NULL);

    void editTarget();
    void editKeep();
};

struct Rule {
    // The rule identifier.
    std::string name;

    // LocalAndRemote backups, Remote backups only, or Remote mount only.
    RuleType type;

    // The path in the local file system to back up.
    Path *path;

    // Additional arguments that affect how the tar files are choosen and sized.
    std::string args;

    // After "beak history work:" the full backup history, from local and remote
    // backup locations is mounted as a virtual file system on this path.
    Path *history_path;

    // When mounting a remote storage for direct access, the tar files are
    // temporarily cached here, to speed up access.
    Path *cache_path;

    // Maximum size of cache, before removing least recently used data.
    size_t cache_size;

    // Use this storage for local backups. Then this local backups is rcloned to remote storages.
    // It points to storages[""]
    Storage *local;

    // All storages for this rule. Can be filesystems or rclone storages.
    std::map<Path*,Storage> storages;

    void output(std::vector<ChoiceEntry> *buf = NULL);
    void status();
    std::vector<Storage*> sortedStorages();

    void generateDefaultSettingsBasedOnPath();
};

struct System;
struct FileSystem;

struct Configuration
{
    virtual bool load() = 0;
    virtual bool save() = 0;
    virtual int configure() = 0;

    virtual Rule *rule(std::string name) = 0;
    virtual std::vector<Rule*> sortedRules() = 0;
};

std::unique_ptr<Configuration> newConfiguration(System *sys, FileSystem *fs);

#endif
