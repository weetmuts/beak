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

int count_newlines(vector<char> &v, size_t *out_nl_pos)
{
    int num_nl = 0;
    size_t offset = 0;
    size_t nl_pos = 0;
    for (char c : v)
    {
        offset++;
        if (c == '\n')
        {
            num_nl++;
            nl_pos = offset-1;
        }
    }

    *out_nl_pos = nl_pos;
    return num_nl;
}

RC establish_android_access(System *sys, Path *mount, vector<char> *mount_output)
{
    vector<string> args;
    args.push_back("-l");
    vector<char> output;
    int out_rc = 0;
    RC rc = sys->invoke("aft-mtp-cli", args, &output, CaptureBoth, NULL, &out_rc);

    if (out_rc != 0 || rc.isErr())
    {
        UI::output("Have you installed aft-mtp-cli? Could not run \"aft-mtp-cli -l\" to list available android devices.\n");
        return RC::ERR;
    }

    size_t nl_pos = 0;
    int num_nl = count_newlines(output, &nl_pos);

    if (num_nl == 0)
    {
        UI::output("No android device connected.\n");
        return RC::ERR;
    }
    if (num_nl > 1)
    {
        UI::output("More than one android device connected. Please connect only one.\n");
        return RC::ERR;
    }
    string str(output.begin(), output.begin()+nl_pos);

    UI::output("Importing media from: %s\n", str.c_str());

    bool printed = false;
    int num_attempts = 0;

    for (;;)
    {
        args.clear();
        args.push_back("pwd");
        output.clear();
        out_rc = 0;
        rc = sys->invoke("aft-mtp-cli", args, &output, CaptureBoth, NULL, &out_rc);
        if (out_rc == 0) break;

        num_attempts++;
        if (num_attempts > 10)
        {
            UI::clearLine();
            UI::output("No permission given to read phone after 10 attempts. Giving up.\n");
            return RC::ERR;
        }
        if (!printed)
        {
            UI::output("Check your phone and give permission to transfer files! ");
            printed = true;
        }
        else
        {
            UI::output(".");
        }
        usleep(2*1000*1000);
    }

    if (num_attempts > 0) UI::clearLine();
    args.clear();
    args.push_back(mount->str());
    rc = sys->run("aft-mtp-mount", args, &out_rc);

    if (out_rc != 0) return RC::ERR;

    strncpy(mounted_dir, mount->c_str(), 1024);
    mounted_dir[1023] = 0;

    /*
    onTerminated("unmount", [sys]{
        vector<string> args = { "-u", mounted_dir };
        int tmp;
        sys->run("fusermount", args, &tmp);
        });*/

    return rc;
}

RC disconnect_android_access(System *sys, Path *mount)
{
    vector<string> args = { "-u", mounted_dir };
    int tmp;
    sys->run("fusermount", args, &tmp);

    if (tmp != 0) return RC::ERR;

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


RC BeakImplementation::cameraMedia(Settings *settings, Monitor *monitor)
{
    RC rc = RC::OK;

    CameraType ct = CameraType::UNKNOWN;

    local_fs_->allowAccessTimeUpdates();

    assert(settings->from.type == ArgDir);
    assert(settings->to.type == ArgDir);
    Path *dir = settings->from.dir;
    Path *home = Path::lookup(getenv("HOME"));
    Path *cache = home->append(".cache/beak/temp-beak-media-import");
    Path *destination = settings->to.dir;

    Path *mount = NULL;

    if (dir->endsWith("/android:"))
    {
        ct = CameraType::MTP_ANDROID;
        mount = local_fs_->userRunDir()->append("beak")->append("android_import");
        local_fs_->mkDirpWriteable(mount);
    }

    if (dir->endsWith("/ios:"))
    {
        ct = CameraType::MTP_IOS;
        mount = local_fs_->userRunDir()->append("beak")->append("ios_import");
        local_fs_->mkDirpWriteable(mount);
    }

    vector<char> mount_output;
    rc = establish_android_access(sys_, mount, &mount_output);
    if (rc.isErr()) return RC::ERR;

    // Disconnect any mountalready.
    //disconnect_android_access(sys_, mount);

    vector<Path*> imagedirs;
    rc = find_imagedir(mount, ct, local_fs_, sys_, &imagedirs);

    if (rc.isErr()) return RC::ERR;

    vector<pair<Path*,FileStat>> files_to_copy;

    for (Path *imagedir : imagedirs)
    {
        vector<pair<Path*,FileStat>> files;
        scan_directory(this, imagedir, &files);

        for (auto &p : files)
        {
            string suffix = normalizeMediaSuffix(p.first);
            time_t pt = p.second.st_mtim.tv_sec;
            struct tm ts;
            localtime_r(&pt, &ts);

            string days;
            strprintf(days, "%04d/%02d/%02d", ts.tm_year+1900, ts.tm_mon+1, ts.tm_mday);
            Path *daydir = destination->append(days);

            vector<pair<Path*,FileStat>> existing;
            local_fs_->listFilesBelow(daydir, &existing, SortOrder::Unspecified);

            bool found = false;
            for (auto &e : existing)
            {
                string esuffix = normalizeMediaSuffix(e.first);

                if (e.second.st_size == p.second.st_size)
                {
                    debug(CAMERA, "matches existing file: %s\n", e.first->c_str());
                    found = true;
                    break;
                }
            }
            if (!found)
            {
                files_to_copy.push_back(p);
                debug(CAMERA, "copying %s\n", p.first->c_str());
            }

        }
        local_fs_->mkDirpWriteable(cache);

        auto map_fs = newMapFileSystem(local_fs_);

        for (auto &p : files_to_copy)
        {
            Path *to = cache->append(p.first->subpath(imagedir->depth())->c_str());
            map_fs->mapFile(p.second, to, p.first);
        }
        unique_ptr<ProgressStatistics> progress = monitor->newProgressStatistics(buildJobName("copying", settings), "copy");

        info(CAMERA, "Copying %zu files from camera.\n", files_to_copy.size());

        progress->startDisplayOfProgress();
        Storage st {};
        st.type = FileSystemStorage;
        st.storage_location = cache;

        storage_tool_->copyBackupIntoStorage(map_fs.get(),
                                             cache,
                                             &st,
                                             settings,
                                             progress.get(),
                                             50*1024*1024);

        if (progress->stats.num_files_stored == 0 && progress->stats.num_dirs_updated == 0)
        {
            info(CAMERA, "No copying needed WOOT?\n");
        }

    }

    rc = disconnect_android_access(sys_, mount);

    return rc;
}
