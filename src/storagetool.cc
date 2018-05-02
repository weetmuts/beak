/*
 Copyright (C) 2018 Fredrik Öhrström

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

#include "storagetool.h"

#include "log.h"
#include "system.h"

//static ComponentId STORAGETOOL = registerLogComponent("storagetool");

using namespace std;

struct StorageToolImplementation : public StorageTool
{
    StorageToolImplementation(ptr<System> sys, ptr<FileSystem> fs);

    RC listBeakFiles(Storage *storage,
                     std::vector<TarFileName> *files,
                     std::vector<TarFileName> *bad_files,
                     std::vector<std::string> *other_files);
    RC sendBeakFilesToStorage(Path *dir, Storage *storage, std::vector<TarFileName*> *files);
    RC fetchBeakFilesFromStorage(Storage *storage, std::vector<TarFileName*> *files, Path *dir);

    ptr<System> sys_;
    ptr<FileSystem> sys_fs_;
};

unique_ptr<StorageTool> newStorageTool(ptr<System> sys, ptr<FileSystem> fs)
{
    return unique_ptr<StorageTool>(new StorageToolImplementation(sys, fs));
}

StorageToolImplementation::StorageToolImplementation(ptr<System>sys, ptr<FileSystem> fs)
    : sys_(sys), sys_fs_(fs)
{
}

RC StorageToolImplementation::sendBeakFilesToStorage(Path *dir, Storage *storage, vector<TarFileName*> *files)
{
    assert(storage->type == RCloneStorage);

    string files_to_fetch;
    Path *tmp;
    if (files) {
        for (auto& tfn : *files) {
            files_to_fetch.append(tfn->path->str());
            files_to_fetch.append("\n");
        }
    }

    tmp = sys_fs_->mkTempFile("beak_fetching", files_to_fetch);

    RC rc = RC::OK;
    vector<char> out;
    vector<string> args;
    args.push_back("copy");
    if (files) {
        args.push_back("--include-from");
        args.push_back(tmp->c_str());
    }
    args.push_back(dir->c_str());
    args.push_back(storage->storage_location->c_str());
    rc = sys_->invoke("rclone", args, &out);

    return rc;
}

RC StorageToolImplementation::fetchBeakFilesFromStorage(Storage *storage, vector<TarFileName*> *files, Path *dir)
{
    assert(storage->type == RCloneStorage);

    string files_to_fetch;
    for (auto& tfn : *files) {
        files_to_fetch.append(tfn->path->str());
        files_to_fetch.append("\n");
    }

    Path *tmp = sys_fs_->mkTempFile("beak_fetching", files_to_fetch);

    RC rc = RC::OK;
    vector<char> out;
    vector<string> args;
    args.push_back("copy");
    args.push_back("--include-from");
    args.push_back(tmp->c_str());
    args.push_back(storage->storage_location->c_str());
    args.push_back(dir->c_str());
    rc = sys_->invoke("rclone", args, &out);

    return rc;
}

RC StorageToolImplementation::listBeakFiles(Storage *storage,
                                            vector<TarFileName> *files,
                                            vector<TarFileName> *bad_files,
                                            vector<string> *other_files)
{
    assert(storage->type == RCloneStorage);

    RC rc = RC::OK;
    vector<char> out;
    vector<string> args;
    args.push_back("ls");
    args.push_back(storage->storage_location->c_str());
    rc = sys_->invoke("rclone", args, &out);

    if (rc.isErr()) return RC::ERR;
    auto i = out.begin();
    bool eof = false, err = false;

    for (;;) {
	// Example line:
	// 12288 z01_001506595429.268937346_0_7eb62d8e0097d5eaa99f332536236e6ba9dbfeccf0df715ec96363f8ddd495b6_0.gz
        eatWhitespace(out, i, &eof);
        if (eof) break;
        string size = eatTo(out, i, ' ', 64, &eof, &err);
        if (eof || err) break;
        string file_name = eatTo(out, i, '\n', 4096, &eof, &err);
        if (err) break;
        TarFileName tfn;
        bool ok = TarFile::parseFileName(file_name, &tfn);
        // Only files that have proper beakfs names are included.
        if (ok) {
            // Check that the remote size equals the content. If there is a mismatch,
            // then for sure the file must be overwritte/updated. Perhaps there was an earlier
            // transfer interruption....
            if ( (tfn.type != REG_FILE && tfn.size == (size_t)atol(size.c_str())) ||
                 (tfn.type == REG_FILE && tfn.size == 0) )
            {
                files->push_back(tfn);
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



StoreStatistics::StoreStatistics() {
    memset(this, 0, sizeof(StoreStatistics));
    start = prev = clockGetTime();
    info_displayed = false;
}

//Tar emot objekt: 100% (814178/814178), 669.29 MiB | 6.71 MiB/s, klart.
//Analyserar delta: 100% (690618/690618), klart.

void StoreStatistics::displayProgress()
{
    if (num_files == 0 || num_files_to_store == 0) return;
    uint64_t now = clockGetTime();
    if ((now-prev) < 500000 && num_files_to_store < num_files) return;
    prev = now;
    info_displayed = true;
    UI::clearLine();
    int percentage = (int)(100.0*(float)size_files_stored / (float)size_files_to_store);
    string mibs = humanReadableTwoDecimals(size_files_stored);
    float secs = ((float)((now-start)/1000))/1000.0;
    string speed = humanReadableTwoDecimals(((double)size_files_stored)/secs);
    if (num_files > num_files_to_store) {
        UI::output("Incremental store: %d%% (%ju/%ju), %s | %.2f s %s/s ",
                   percentage, num_files_stored, num_files_to_store, mibs.c_str(), secs, speed.c_str());
    } else {
        UI::output("Full store: %d%% (%ju/%ju), %s | %.2f s %s/s ",
                   percentage, num_files_stored, num_files_to_store, mibs.c_str(), secs, speed.c_str());
    }
}

void StoreStatistics::finishProgress()
{
    if (info_displayed == false || num_files == 0 || num_files_to_store == 0) return;
    displayProgress();
    UI::output(", done.\n");
}
