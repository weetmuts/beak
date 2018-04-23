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
    Storage checkFileSystemStorage(std::string name);
    Storage checkRCloneStorage(std::string name);
    Storage checkRSyncStorage(std::string name);

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

Storage StorageToolImplementation::checkFileSystemStorage(std::string name)
{
    Path *rp = Path::lookup(name)->realpath();
    if (!rp)
    {
        return Storage();
    }

    return Storage { FileSystemStorage, rp, "" };
}

Storage StorageToolImplementation::checkRCloneStorage(string name)
{
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
            if (eof || err) break;
            string type = eatTo(out, i, '\n', 64, &eof, &err);
            if (err) break;
            trimWhitespace(&type);
            configs[target+":"] = type;
        }

        if (configs.count(name) > 0) {
            return Storage { RCloneStorage, Path::lookup(name), "" };
        }
    }

    return Storage();
}

Storage StorageToolImplementation::checkRSyncStorage(std::string name)
{
    // An rsync location is detected by the @ sign and the server :.
    size_t at = name.find('@');
    size_t colon = name.find(':');
    if (at != string::npos &&
        colon != string::npos &&
        at < colon)
    {
        return Storage { RSyncStorage, Path::lookup(name), "" };
    }

    return Storage();
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
    args.push_back(storage->target_path->c_str());
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
    args.push_back(storage->target_path->c_str());
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
    args.push_back(storage->target_path->c_str());
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
