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

#include "system.h"

using namespace std;

Storage StorageTool::checkFileSystemStorage(ptr<System> sys, std::string name)
{
    Path *rp = Path::lookup(name)->realpath();
    if (!rp)
    {
        return Storage();
    }

    return Storage { FileSystemStorage, rp, "" };
}

Storage StorageTool::checkRCloneStorage(ptr<System> sys, string name)
{
    map<string,string> configs;
    vector<char> out;
    vector<string> args;
    args.push_back("listremotes");
    args.push_back("-l");
    RCC rcc = sys->invoke("rclone", args, &out);
    if (rcc.isOk()) {
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

Storage StorageTool::checkRSyncStorage(ptr<System> sys, std::string name)
{
    return Storage();
}

/*
RC BeakImplementation::listStorageFiles(Storage *storage, vector<TarFileName> *files)
{
    RC rc = OK;
    vector<char> out;
    vector<string> args;
    args.push_back("ls");
    args.push_back(storage->c_str());
    rc = sys_->invoke("rclone", args, &out);

    if (rc != OK) return ERR;
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
            // then for sure the file must be overwritte/updated. Perhaps there was an earlire
            // transfer interruption....
            if (tfn.size == (size_t)atol(size.c_str())) {
                files->push_back(tfn);
            }
        }
    }
    if (err) return ERR;

    return OK;
}
*/
