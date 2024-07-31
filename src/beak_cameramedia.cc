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
#include "system.h"
#include "util.h"

#include <unistd.h>

static ComponentId CAMERA = registerLogComponent("camera");

#include "media.h"

enum class CameraType
{
    UNKNOWN, // Unknown camera
    FILESYSTEM, // A generic (slow) filesystem access containing a DCIM directory somewhere.
    MTP_ANDROID, // Use aft-mtp-cli and aft-mtp-mount to copy from the DCIM.
    MTP_IOS // Use gphoto2 etc...
};

char mounted_dir[1024];

RC find_imagedir(Path *mount, CameraType ct, FileSystem *fs, System *sys, vector<Path*> *imagedirs)
{
    vector<pair<Path*,FileStat>> dcims;

    for (int depth = 1; depth < 4; ++depth)
    {
        vector<pair<Path*,FileStat>> mounts;
        fs->listDirsBelow(mount, &mounts, SortOrder::Unspecified, depth);
        for (auto &p : mounts)
        {
            if (p.first->endsWith("DCIM"))
            {
                dcims.push_back(p);
            }
        }
        if (dcims.size() > 0) break;
    }

    for (auto &p : dcims)
    {
        // Some silly phones use DCIM/Camera (and keeps a lot of other crap inside DCIM, like
        // DCIM/Screenshots etc.) Lets check with a quick stat if DCIM/Camera exists, if so, then use it
        // to avoid backing up a lot of non-camera data. For normal phones the DCIM directory
        // only contains images (and no other silly data).
        //
        // Also we cannot list the contents of DCIM to check the Camera dir, since this can take
        // a loooong time if DCIM is filled with many images. We use stat for this.
        Path *dcim = mount->append(p.first->str());
        Path *camera = dcim->append("Camera");
        FileStat st {};
        RC rc = fs->stat(camera, &st);
        if (rc.isOk() && st.isDirectory())
        {
            imagedirs->push_back(camera);
        }
        else
        {
            imagedirs->push_back(dcim);
        }
    }
    return RC::OK;
}



RC scan_directory(BeakImplementation *bi, Path *p, vector<pair<Path*,FileStat>> *files)
{
    UI::output("Scanning: \"%s\"", p->c_str());

    size_t num = 0;

    uint64_t start = clockGetTimeMicroSeconds();

    bi->localFS()->recurse(p, [&files, &num](Path *p, FileStat *st) {
        if (st->isRegularFile())
        {
            files->push_back({p, *st });
            num++;
        }
        return RecurseContinue;
    });

    UI::clearLine();

    if (num > 0)
    {
        uint64_t stop = clockGetTimeMicroSeconds();
        size_t millis = (stop-start)/1000;

        double seconds = ((double)millis)/1000.0;
        size_t files_per_sec = (size_t)(((double)num) / seconds);

        UI::output("Found %zu files in %zu ms at %zu files/s\n", num, millis, files_per_sec);
    }

    return RC::OK;
}

/*

    UI::clearLine();

    args.clear();
    args.push_back("ls DCIM");
    output.clear();
    out_rc = 0;
    rc = sys->invoke("aft-mtp-cli", args, &output, CaptureBoth, NULL, &out_rc);

    if (rc.isErr() || out_rc != 0)
    {
        UI::output("Error reading the DCIM directory. Giving up.\n");
    }

    args.clear();
    args.push_back("lsext-r DCIM/Camera");
    output.clear();
    out_rc = 0;
    rc = sys->invoke("aft-mtp-cli", args, &output, CaptureBoth,
                     [&num_nl, device_media](char *buf, size_t len) {
                         int n = 0;
                         char *end = buf+len;
                         for (char *i = buf; i < end; ++i) { if (*i == '\n') n++; }
                         num_nl += n;
                         if (!debug_logging_)
                         {
                             UI::clearLine();
                             UI::output("Scanning DCIM %d", num_nl);
                         }
                         else
                         {
                             UI::output("Scanning DCIM %d\n", num_nl);
                         }
                     }, &out_rc);

    if (rc.isErr() || out_rc != 0)
    {
        UI::clearLine();
        UI::output("Error reading the files below the DCIM directory. Giving up.\n");
    }

    nl_pos = 0;
    num_nl = count_newlines(output, &nl_pos);

    UI::clearLine();
    UI::output("Found %d media files in DCIM.\n", num_nl);

    parse_aft(output, device_media);

    return rc;
*/


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

RC BeakImplementation::cameraMedia(Settings *settings, Monitor *monitor)
{
    RC rc = RC::OK;

    Path *home = Path::lookup(getenv("HOME"));
    Path *cache = home->append(".cache/beak/temp-beak-media-import");

    local_fs_->allowAccessTimeUpdates();

    assert(settings->from.type == ArgStorage);
    assert(settings->from.storage->type == AftMtpStorage);
    assert(settings->to.type == ArgDir);
    Path *destination = settings->to.dir;

    // The directory name where we store the media files.
    string archive_name = destination->name()->str();

    // Establish access to the phone/camera and get the device name.
    string device_name = aftmtpEstablishAccess(sys_);

    info(CAMERA, "Importing media from %s into %s\n", device_name.c_str(), archive_name.c_str());

    unique_ptr<ProgressStatistics> progress = monitor->newProgressStatistics(buildJobName("listing", settings), "list");

    map<Path*,FileStat> files;

    // Just list the files in the android phone, The crappy mtp protocol is soooo slow just listing the files.
    // Anyway we do this once (hopefully unless there is a random disconnect) then we can check size and date
    // for if we think we have imported this file already. I.e. no need to download slowly over mtp to do
    // the full processing.
    for (;;)
    {
        rc = aftmtpListFiles(settings->from.storage,
                             &files,
                             sys_,
                             progress.get());

        if (rc.isOk()) break;
        // The mtp link crashed already in the first transfer.... Blech.
        aftmtpReEstablishAccess(sys_, true);
    }

    UI::output("Found ... new files not yet in %s", archive_name.c_str());

    vector<pair<Path*,FileStat>> potential_files_to_copy;
    for (auto &p : files)
    {
        bool already_exists = check_if_already_exists(p.first, p.second, local_fs_, destination);
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
    local_fs_->mkDirpWriteable(cache);

    // Downloading from the phone/camera using mtp can take some time, lets track the progress.
    progress = monitor->newProgressStatistics(buildJobName("copying", settings), "copy");

    vector<Path*> files_to_copy;
    for (auto &p : potential_files_to_copy)
    {
        // Check if the file has already been copied to the temporary dir.
        // Remember that the usb connection to the phone can break at any time.
        // We want to pickup where we left off.
        Path *dest_file = p.first->prepend(cache);
        addWork(progress.get(), p.first, p.second, local_fs_, dest_file, &files_to_copy);
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
    rc = aftmtpFetchFiles(settings->from.storage,
                          &files_to_copy,
                          cache,
                          sys_,
                          local_fs_,
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

    rc = importMedia(settings, monitor);

    return rc;
}
