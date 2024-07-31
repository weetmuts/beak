/*
 Copyright (C) 2018-2024 Fredrik Öhrström

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

#include "storage_aftmtp.h"

#include<unistd.h>
#include<algorithm>

#include "log.h"

using namespace std;

static ComponentId AFTMTP = registerLogComponent("aftmtp");

// Ugly global variable should be moved into an rclone_aftmtp instance instead.
map<Path*,FileStat> map_path_id_;

RC aftmtp_prime_files(Storage *storage, ptr<System> sys);

RC aftmtpListBeakFiles(Storage *storage,
                       vector<TarFileName> *files,
                       vector<TarFileName> *bad_files,
                       vector<string> *other_files,
                       map<Path*,FileStat> *contents,
                       ptr<System> sys,
                       ProgressStatistics *st)
{
    assert(storage->type == AftMtpStorage);

    RC rc = RC::OK;
    vector<char> out;
    vector<string> args;

    args.push_back("-e");
    args.push_back("-C");
    args.push_back("lsext-r "+storage->storage_location->str());
    rc = sys->invoke("aft-mtp-cli", args, &out);

    if (rc.isErr()) return RC::ERR;

    auto i = out.begin();
    bool eof = false, err = false;

    // 2360       65537      ExifJpeg    2366292 2024-06-06 14:42:11  20240606_144210.jpg

    for (;;) {
        eatWhitespace(out, i, &eof);
        if (eof) break;
        string id = eatTo(out, i, ' ', 64, &eof, &err);
        if (eof || err) break;
        eatWhitespace(out, i, &eof);
        string whereid = eatTo(out, i, ' ', 64, &eof, &err);
        if (eof || err) break;
        eatWhitespace(out, i, &eof);
        string type = eatTo(out, i, ' ', 64, &eof, &err);
        if (eof || err) break;
        eatWhitespace(out, i, &eof);
        string size = eatTo(out, i, ' ', 64, &eof, &err);
        if (eof || err) break;
        eatWhitespace(out, i, &eof);
        string date = eatTo(out, i, ' ', 64, &eof, &err);
        if (eof || err) break;
        eatWhitespace(out, i, &eof);
        string time = eatTo(out, i, ' ', 64, &eof, &err);
        if (eof || err) break;
        eatWhitespace(out, i, &eof);
        string file_name = eatTo(out, i, '\n', 4096, &eof, &err);
        if (err) break;

        TarFileName tfn;
        string dir;
        bool ok = tfn.parseFileName(file_name, &dir);
        // Only files that have proper beakfs names are included.
        if (ok) {
            size_t siz = (size_t)atol(size.c_str());
            if (tfn.ondisk_size == siz)
            {
                files->push_back(tfn);
                Path *p = Path::lookup(dir)->prepend(storage->storage_location);
                char filename[1024];
                tfn.writeTarFileNameIntoBuffer(filename, sizeof(filename), p);
                Path *file_path = Path::lookup(filename);
                FileStat fs;
                fs.st_size = (off_t)siz;
                fs.st_mtim.tv_sec = tfn.sec;
                fs.st_mtim.tv_nsec = tfn.nsec;
                fs.st_mode |= S_IRUSR;
                fs.st_mode |= S_IFREG;
                (*contents)[file_path] = fs;
            }
            else
            {
                bad_files->push_back(tfn);
            }
        } else {
            other_files->push_back(file_name);
        }
    }
    if (err) return RC::ERR;

    return RC::OK;
}

RC aftmtpSendFiles(Storage *storage,
                   vector<Path*> *files,
                   Path *local_dir,
                   FileSystem *local_fs,
                   ptr<System> sys,
                   ProgressStatistics *st)
{
    string files_to_send;
    for (auto& p : *files) {
        files_to_send.append(p->c_str());
        files_to_send.append("\n");
    }
    Path *tmp = local_fs->mkTempFile("beak_sending_", files_to_send);

    vector<string> args;
    args.push_back("copy");
    args.push_back("-vv");
    args.push_back("--stats-one-line");
    args.push_back("--stats=10s");
    args.push_back("--include-from");
    args.push_back(tmp->c_str());
    args.push_back(local_dir->c_str());
    args.push_back(storage->storage_location->str());
    vector<char> output;
    RC rc = sys->invoke("aftmtp", args, &output, CaptureBoth);

    local_fs->deleteFile(tmp);

    return rc;
}

RC attempt_download(Storage *storage,
                    Path *p,
                    Path *local_dir,
                    System *sys,
                    FileSystem *local_fs,
                    ProgressStatistics *progress)
{
    vector<string> args;
    FileStat fs = map_path_id_[p];
    string cmd;

    Path *dest_file = p->prepend(local_dir);
    Path *dest_dir = dest_file->parent();
    local_fs->mkDirpWriteable(dest_dir);

    strprintf(cmd, "get-id %d %s", fs.st_ino, dest_file->c_str());
    args.push_back("-e");
    args.push_back("-C");
    args.push_back(cmd);

    RC rc = RC::OK;
    vector<char> output;
    int output_rc = 0;
    rc = sys->invoke("aft-mtp-cli", args, &output, CaptureBoth,
                     NULL,
                     &output_rc);

    if (rc.isErr() || output_rc != 0)
    {
        // Ouch, mtp disconnected....crap.
        string out = string(output.begin(), output.end());
        std::remove_if(out.begin(), out.end(), [](char c){return c=='\n';});
        UI::output("Another mtp error: \"%s\"\n", out.c_str());
        return RC::ERR;
    }

    if (progress->stats.file_sizes.count(p))
    {
        size_t siz = progress->stats.file_sizes[p];
        progress->stats.size_files_stored += siz;
        progress->stats.num_files_stored++;
        progress->updateProgress();
    }
    else
    {
        printf("PRUTT KOKO %s\n", p->c_str());
    }

    return RC::OK;
}

RC aftmtpFetchFiles(Storage *storage,
                    vector<Path*> *files,
                    Path *local_dir,
                    System *sys,
                    FileSystem *local_fs,
                    ProgressStatistics *progress)
{
    assert(storage->type == AftMtpStorage);

    for (auto& p : *files)
    {
        if (map_path_id_.count(p) == 0)
        {
            warning(AFTMTP, "Internal problem, id cache of file %s is lost. Skipping file.\n", p->c_str());
            continue;
        }

        for (;;)
        {
            RC rc = attempt_download(storage,
                                     p,
                                     local_dir,
                                     sys,
                                     local_fs,
                                     progress);

            if (rc.isOk()) break;
            // Crap, the connection to the phone broke. Bad USB, Bad Protocol, Bad Microsoft who designed this shit.
            // Ask the user to unplug/replug, hopefull we can continuet the download.
            aftmtpReEstablishAccess(sys, true);
            // Perform a ls-r to re-enable get-id downloads.
            aftmtp_prime_files(storage, sys);
        }
    }

    return RC::OK;
}


RC aftmtpDeleteFiles(Storage *storage,
                     std::vector<Path*> *files,
                     FileSystem *local_fs,
                     ptr<System> sys,
                     ProgressStatistics *progress)
{
    string files_to_delete;
    for (auto& p : *files) {
        files_to_delete.append(p->c_str());
        files_to_delete.append("\n");
        debug(AFTMTP, "delete \"%s\"\n", p->c_str());
    }

    Path *tmp = local_fs->mkTempFile("beak_deleting_", files_to_delete);

    RC rc = RC::OK;
    vector<string> args;
    args.push_back("delete");
    args.push_back("--include-from");
    args.push_back(tmp->c_str());
    args.push_back(storage->storage_location->c_str());
    vector<char> output;
    rc = sys->invoke("aftmtp", args, &output, CaptureBoth);

    local_fs->deleteFile(tmp);

    return rc;
}

RC aftmtpListFiles(Storage *storage,
                   map<Path*,FileStat> *contents,
                   ptr<System> sys,
                   ProgressStatistics *st)
{
    assert(storage->type == AftMtpStorage);

    RC rc = RC::OK;
    vector<char> out;
    vector<string> args;
    // Remove aftmtp: 7 chars.
    string dir = storage->storage_location->str().substr(7);
    args.push_back("-e");
    args.push_back("-C");
    args.push_back(string("lsext-r ")+dir);
    int num = 0;
    UI::clearLine();
    UI::output("Scanning aftmtp:%s ...", dir.c_str());
    rc = sys->invoke("aft-mtp-cli", args, &out,
                     CaptureBoth,
                     [&num,dir](char *buf, size_t len)
                     {
                         num++;
                         if (num % 100 == 0)
                         {
                             UI::clearLine();
                             UI::output("Scanning aftmtp:%s %d", dir.c_str(), num);
                         }
                     }
        );

    UI::clearLine();

    if (rc.isErr()) return RC::ERR;

    auto i = out.begin();
    bool eof = false, err = false;

    // 2360       65537      ExifJpeg    2366292 2024-06-06 14:42:11  20240606_144210.jpg

    for (;;) {
        eatWhitespace(out, i, &eof);
        if (eof) break;
        string id = eatTo(out, i, ' ', 64, &eof, &err);
        if (eof || err) break;
        eatWhitespace(out, i, &eof);
        string whereid = eatTo(out, i, ' ', 64, &eof, &err);
        if (eof || err) break;
        eatWhitespace(out, i, &eof);
        string type = eatTo(out, i, ' ', 64, &eof, &err);
        if (eof || err) break;
        eatWhitespace(out, i, &eof);
        string size = eatTo(out, i, ' ', 64, &eof, &err);
        if (eof || err) break;
        eatWhitespace(out, i, &eof);
        string date = eatTo(out, i, ' ', 64, &eof, &err);
        if (eof || err) break;
        eatWhitespace(out, i, &eof);
        string time = eatTo(out, i, ' ', 64, &eof, &err);
        if (eof || err) break;
        eatWhitespace(out, i, &eof);
        string file_name = eatTo(out, i, '\n', 4096, &eof, &err);
        file_name.pop_back();
        if (err) break;

        if (id == "selected") continue; // Skip first line.

        size_t ino = (size_t)atol(id.c_str());
        if (ino == 0) continue;

        size_t siz = (size_t)atol(size.c_str());
        if (siz == 0) continue;

        time_t sec {};
        string datetime = date+" "+time;
        RC rc = parseDateTime(datetime, &sec);
        if (rc.isErr()) continue;

        FileStat fs {};
        fs.st_ino = ino;
        fs.st_size = (off_t)siz;
        fs.st_mtim.tv_sec = sec;
        fs.st_mode |= S_IRUSR;
        fs.st_mode |= S_IFREG;
        Path *file_path = Path::lookup(file_name)->prepend(storage->storage_location);
        (*contents)[file_path] = fs;
        map_path_id_[file_path] = fs;
        debug(AFTMTP, "list \"%s\" %zu %s %d \n", file_path->c_str(), fs.st_size, datetime.c_str(), fs.st_ino);
    }
    return RC::OK;
}

RC aftmtp_prime_files(Storage *storage,
                      ptr<System> sys)
{
    assert(storage->type == AftMtpStorage);

    RC rc = RC::OK;
    vector<char> out;
    vector<string> args;
    // Remove aftmtp: 7 chars.
    string dir = storage->storage_location->str().substr(7);
    args.push_back("-e");
    args.push_back("-C");
    args.push_back(string("ls-r ")+dir);
    int num = 0;
    UI::clearLine();
    UI::output("Re-Scanning aftmtp:%s ...", dir.c_str());
    rc = sys->invoke("aft-mtp-cli", args, &out,
                     CaptureBoth,
                     [&num,dir](char *buf, size_t len)
                     {
                         num++;
                         if (num % 100 == 0)
                         {
                             UI::clearLine();
                             UI::output("Re-Scanning aftmtp:%s %d", dir.c_str(), num);
                         }
                     }
        );

    UI::clearLine();

    if (rc.isErr()) return RC::ERR;

    return RC::OK;
}

string aftmtpEstablishAccess(System *sys)
{
    // Check that aft-mtp-cli binary is installed.
    vector<string> args;
    args.push_back("--help");
    vector<char> output;
    int out_rc = 0;
    RC rc = sys->invoke("aft-mtp-cli", args, &output, CaptureBoth, NULL, &out_rc);

    if (out_rc != 0 || rc.isErr())
    {
        usageError(AFTMTP, "Have you installed aft-mtp-cli? Could not run \"aft-mtp-cli --help\"\n");
    }

    return aftmtpReEstablishAccess(sys, false);
}

string aftmtpReEstablishAccess(System *sys, bool hint_unplug)
{
    for (;;)
    {
        bool printed = false;
        int num_attempts = 0;

        for (;;)
        {
            vector<string> args;
            args.push_back("-e");
            args.push_back("-C");
            args.push_back("pwd");
            vector<char> output;
            int out_rc = 0;
            RC rc = sys->invoke("aft-mtp-cli", args, &output, CaptureBoth, NULL, &out_rc);
            if (rc.isOk() && out_rc == 0) break;

            num_attempts++;
            if (num_attempts > 20)
            {
                UI::clearLine();
                usageError(AFTMTP, "No permission given to read phone after 20 attempts. Giving up.\n");
            }
            if (!printed)
            {
                if (hint_unplug)
                {
                    UI::output("Unplug/replug and give permission to transfer files!\n");
                }
                else
                {
                    UI::output("Plugin your phone and give permission to transfer files! ");
                }
                printed = true;
            }
            else
            {
                if (!hint_unplug) UI::output(".");
            }
            sleep(2);
        }

        if (num_attempts > 0) UI::clearLine();

        vector<string> args;
        args.push_back("-e");
        args.push_back("-C");
        args.push_back("-l");
        vector<char> output;
        int out_rc = 0;
        RC rc = sys->invoke("aft-mtp-cli", args, &output, CaptureBoth, NULL, &out_rc);
        if (rc.isErr() || out_rc != 0)
        {
            usageError(AFTMTP, "Oups! Could not do \"aft-mtp-cli -l\" even though pwd worked. Giving up.\n");
        }

        size_t nl_pos = 0;
        int num_nl = count_newlines(output, &nl_pos);

        if (num_nl > 1)
        {
            usageError(AFTMTP, "Oups! Only connect a single android device. Please unplug one. Giving up.\n");
        }
        string out = string(output.begin(), output.begin()+nl_pos);
        if (hint_unplug)
        {
            UI::output("Reconnected to %s\n", out.c_str());
        }

        return out;
    }

    return "?";
}
