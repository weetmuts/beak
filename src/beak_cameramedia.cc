/*
 Copyright (C) 2024 Fredrik Öhrström

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

// This must be before log.h since it declares a debug macro.
#include<assert.h>
#include<algorithm>
#include<set>

#include "beak.h"
#include "beak_implementation.h"
#include "backup.h"
#include "filesystem_helpers.h"
#include "log.h"
#include "media.h"
#include "storagetool.h"
#include "storage_aftmtp.h"
#include "storage_gphoto2.h"
#include "system.h"
#include "util.h"

#include <unistd.h>

static ComponentId CAMERA = registerLogComponent("camera");

#include "media.h"

bool check_if_already_exists(Path *file, FileStat stat, FileSystem *fs, Path *destination)
{
    string suffix = normalizeMediaSuffix(file);
    time_t pt = stat.st_mtim.tv_sec;
    struct tm ts;
    localtime_r(&pt, &ts);

    string days;
    strprintf(days, "%04d/%02d/%02d", ts.tm_year+1900, ts.tm_mon+1, ts.tm_mday);
    Path *daydir = destination->append(days);

    debug(CAMERA, "Check if %s %zu %d in day %s\n", file->c_str(), stat.st_size, stat.st_ino, daydir->c_str());

    vector<pair<Path*,FileStat>> existing;
    fs->listFilesBelow(daydir, &existing, SortOrder::Unspecified);

    bool found = false;
    for (auto &e : existing)
    {
        string esuffix = normalizeMediaSuffix(e.first);

        if (e.second.st_size == stat.st_size && suffix == esuffix)
        {
            debug(CAMERA, "matches existing file: %s\n", e.first->c_str());
            found = true;
            break;
        }
    }

    return found;
}

RC import_aft_mtp_cli(Settings *settings,
                      Monitor *monitor,
                      System *sys,
                      FileSystem *local_fs,
                      BeakImplementation *bi)
{
    assert(settings->from.storage->type == AftMtpStorage);
    Path *home = Path::lookup(getenv("HOME"));
    Path *cache = home->append(".cache/beak/temp-beak-media-import");

    local_fs->allowAccessTimeUpdates();

    assert(settings->to.type == ArgDir);
    Path *destination = settings->to.dir;

    // The directory name where we store the media files.
    string archive_name = destination->name()->str();

    // Establish access to the phone/camera and get the device name.
    string device_name = aftmtpEstablishAccess(sys);

    info(CAMERA, "Importing media from %s into %s\n", device_name.c_str(), archive_name.c_str());

    unique_ptr<ProgressStatistics> progress = monitor->newProgressStatistics(buildJobName("listing", settings), "list");

    map<Path*,FileStat> files;

    // Just list the files in the android phone, The crappy mtp protocol is soooo slow just listing the files.
    // Anyway we do this once (hopefully unless there is a random disconnect) then we can check size and date
    // for if we think we have imported this file already. I.e. no need to download slowly over mtp to do
    // the full processing.
    for (;;)
    {
        RC rc = aftmtpListFiles(settings->from.storage,
                             &files,
                             sys,
                             progress.get());

        if (rc.isOk()) break;
        // The mtp link crashed already in the first transfer.... Blech.
        aftmtpReEstablishAccess(sys, true);
    }

    UI::output("Found ... new files not yet in %s", archive_name.c_str());

    vector<pair<Path*,FileStat>> potential_files_to_copy;
    for (auto &p : files)
    {
        bool already_exists = check_if_already_exists(p.first, p.second, local_fs, destination);
        if (!already_exists)
        {
            potential_files_to_copy.push_back(p);
            debug(CAMERA, "potential download %s\n", p.first->c_str());
            UI::clearLine();
            UI::output("Found %zu new files not yet in %s", potential_files_to_copy.size(), archive_name.c_str());
        }
    }
    UI::clearLine();
    if (potential_files_to_copy.size() == 0)
    {
        UI::clearLine();
        info(CAMERA, "All files imported into %s already.\n", archive_name.c_str());
        return RC::OK;
    }

    // We have some potential files that we do not think have been imported yet.
    // Lets download them into a temporary dir from which we can import them properly.
    local_fs->mkDirpWriteable(cache);

    // Downloading from the phone/camera using mtp can take some time, lets track the progress.
    progress = monitor->newProgressStatistics(buildJobName("copying", settings), "copy");

    vector<Path*> files_to_copy;
    for (auto &p : potential_files_to_copy)
    {
        // Check if the file has already been copied to the temporary dir.
        // Remember that the usb connection to the phone can break at any time.
        // We want to pickup where we left off.
        Path *dest_file = p.first->prepend(cache);
        addWork(progress.get(), p.first, p.second, local_fs, dest_file, &files_to_copy);
        // The file is added (or not) to files_to_copy.
    }

    size_t already_in_cache = potential_files_to_copy.size()-files_to_copy.size();
    size_t already_imported = files.size()-files_to_copy.size();
    info(CAMERA, "Downloading %zu files (%zu already in cache and %zu already fully imported into %s).\n",
         files_to_copy.size(),
         already_in_cache,
         already_imported,
         archive_name.c_str());

    progress->startDisplayOfProgress();
    RC rc = aftmtpFetchFiles(settings->from.storage,
                          &files_to_copy,
                          cache,
                          sys,
                          local_fs,
                          progress.get());

    progress->finishProgress();

    // Now import the cache into the real archive.
    settings->from.type = ArgDir;
    settings->from.dir = cache;

    settings->to.type = ArgStorage;
    Storage st {};
    settings->to.storage = &st;
    settings->to.storage->storage_location = destination;
    settings->to.storage->type = FileSystemStorage;

    rc = bi->importMedia(settings, monitor);

    return rc;
}

RC import_gphoto2(Settings *settings,
                  Monitor *monitor,
                  System *sys,
                  FileSystem *local_fs,
                  BeakImplementation *bi)
{
    assert(settings->from.storage->type == GPhoto2Storage);
    //Path *home = Path::lookup(getenv("HOME"));
    //Path *cache = home->append(".cache/beak/temp-beak-media-import");

    local_fs->allowAccessTimeUpdates();

    assert(settings->to.type == ArgDir);
    Path *destination = settings->to.dir;

    // The directory name where we store the media files.
    string archive_name = destination->name()->str();

    // Establish access to the phone/camera and get the device name.
    string device_name = gphoto2EstablishAccess(sys);

    info(CAMERA, "Importing media from %s into %s\n", device_name.c_str(), archive_name.c_str());

    return RC::OK;
/*
    unique_ptr<ProgressStatistics> progress = monitor->newProgressStatistics(buildJobName("listing", settings), "list");

    map<Path*,FileStat> files;

    // Just list the files in the android phone, The crappy mtp protocol is soooo slow just listing the files.
    // Anyway we do this once (hopefully unless there is a random disconnect) then we can check size and date
    // for if we think we have imported this file already. I.e. no need to download slowly over mtp to do
    // the full processing.
    for (;;)
    {
        RC rc = aftmtpListFiles(settings->from.storage,
                             &files,
                             sys,
                             progress.get());

        if (rc.isOk()) break;
        // The mtp link crashed already in the first transfer.... Blech.
        aftmtpReEstablishAccess(sys, true);
    }

    UI::output("Found ... new files not yet in %s", archive_name.c_str());

    vector<pair<Path*,FileStat>> potential_files_to_copy;
    for (auto &p : files)
    {
        bool already_exists = check_if_already_exists(p.first, p.second, local_fs, destination);
        if (!already_exists)
        {
            potential_files_to_copy.push_back(p);
            debug(CAMERA, "potential download %s\n", p.first->c_str());
            UI::clearLine();
            UI::output("Found %zu new files not yet in %s", potential_files_to_copy.size(), archive_name.c_str());
        }
    }
    UI::clearLine();
    if (potential_files_to_copy.size() == 0)
    {
        UI::clearLine();
        info(CAMERA, "All files imported into %s already.\n", archive_name.c_str());
        return RC::OK;
    }

    // We have some potential files that we do not think have been imported yet.
    // Lets download them into a temporary dir from which we can import them properly.
    local_fs->mkDirpWriteable(cache);

    // Downloading from the phone/camera using mtp can take some time, lets track the progress.
    progress = monitor->newProgressStatistics(buildJobName("copying", settings), "copy");

    vector<Path*> files_to_copy;
    for (auto &p : potential_files_to_copy)
    {
        // Check if the file has already been copied to the temporary dir.
        // Remember that the usb connection to the phone can break at any time.
        // We want to pickup where we left off.
        Path *dest_file = p.first->prepend(cache);
        addWork(progress.get(), p.first, p.second, local_fs, dest_file, &files_to_copy);
        // The file is added (or not) to files_to_copy.
    }

    size_t already_in_cache = potential_files_to_copy.size()-files_to_copy.size();
    size_t already_imported = files.size()-files_to_copy.size();
    info(CAMERA, "Downloading %zu files (%zu already in cache and %zu already fully imported into %s).\n",
         files_to_copy.size(),
         already_in_cache,
         already_imported,
         archive_name.c_str());

    progress->startDisplayOfProgress();
    RC rc = aftmtpFetchFiles(settings->from.storage,
                          &files_to_copy,
                          cache,
                          sys,
                          local_fs,
                          progress.get());

    progress->finishProgress();

    // Now import the cache into the real archive.
    settings->from.type = ArgDir;
    settings->from.dir = cache;

    settings->to.type = ArgStorage;
    Storage st {};
    settings->to.storage = &st;
    settings->to.storage->storage_location = destination;
    settings->to.storage->type = FileSystemStorage;

    rc = bi->importMedia(settings, monitor);

    return rc;
*/
}

RC BeakImplementation::cameraMedia(Settings *settings, Monitor *monitor)
{
    assert(settings->from.type == ArgStorage);

    if (settings->from.storage->type == AftMtpStorage)
    {
        return import_aft_mtp_cli(settings, monitor, sys_, local_fs_, this);
    }

    if (settings->from.storage->type == GPhoto2Storage)
    {
        return import_gphoto2(settings, monitor, sys_, local_fs_, this);
    }

    return RC::ERR;
}
