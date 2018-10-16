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

#include "storage_rsync.h"

#include "log.h"

using namespace std;

static ComponentId RSYNC = registerLogComponent("rsync");

RC rsyncListBeakFiles(Storage *storage,
                       vector<TarFileName> *files,
                       vector<TarFileName> *bad_files,
                       vector<string> *other_files,
                       map<Path*,FileStat> *contents,
                       ptr<System> sys)
{
    assert(storage->type == RSyncStorage);

    RC rc = RC::OK;
    vector<char> out;
    vector<string> args;

    args.push_back("-r");
    string p = storage->storage_location->str()+"/"; // rsync needs the trailing slash
    args.push_back(p.c_str());
    rc = sys->invoke("rsync", args, &out);

    if (rc.isErr()) return RC::ERR;

    auto i = out.begin();
    bool eof = false, err = false;

    for (;;) {
	// Example line:
        // -r--------         43,008 2017/10/28 17:58:22 apis/z01_001509206302.681804342_0_1a599a3c00aec163169081a7e7b6dcdda25b2792daa80ba6454f81c6802d8ec4_0.gz
        eatWhitespace(out, i, &eof); if (eof) break;
        string permissions = eatTo(out, i, ' ', 64, &eof, &err); if (eof || err) break;
        eatWhitespace(out, i, &eof); if (eof) break;
        string size = eatTo(out, i, ' ', 64, &eof, &err); if (eof || err) break;
        size = keepDigits(size); // Remove commas
        eatWhitespace(out, i, &eof); if (eof) break;
        string date = eatTo(out, i, ' ', 64, &eof, &err); if (eof || err) break;
        string time = eatTo(out, i, ' ', 64, &eof, &err); if (eof || err) break;
        string file_name = eatTo(out, i, '\n', 1024, &eof, &err); if (err) break;

        TarFileName tfn;
        bool ok = tfn.parseFileName(file_name);
        // Only files that have proper beakfs names are included.
        if (ok) {
            // Check that the remote size equals the content. If there is a mismatch,
            // then for sure the file must be overwritte/updated. Perhaps there was an earlier
            // transfer interruption....
            size_t siz = (size_t)atol(size.c_str());
            if (rc.isErr()) {
                siz = -1;
            }
            if ( (tfn.type != REG_FILE && tfn.size == siz) ||
                 (tfn.type == REG_FILE && tfn.size == 0) )
            {
                files->push_back(tfn);
                Path *p = tfn.asPathWithDir(storage->storage_location);
                FileStat fs;
                fs.st_size = (off_t)siz;
                fs.st_mtim.tv_sec = tfn.sec;
                fs.st_mtim.tv_nsec = tfn.nsec;
                fs.st_mode |= S_IRUSR;
                fs.st_mode |= S_IFREG;
                (*contents)[p] = fs;
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


RC rsyncFetchFiles(Storage *storage,
                    vector<Path*> *files,
                    Path *dir,
                    System *sys,
                    FileSystem *local_fs)
{
    Path *target_dir = storage->storage_location->prepend(dir);
    string files_to_fetch;
    for (auto& p : *files) {
        Path *n = p->subpath(storage->storage_location->depth());
        files_to_fetch.append(n->str());
        files_to_fetch.append("\n");
    }

    Path *tmp = local_fs->mkTempFile("beak_fetching_", files_to_fetch);

    RC rc = RC::OK;
    vector<char> out;
    vector<string> args;
    args.push_back("-a");
    args.push_back("--files-from");
    args.push_back(tmp->c_str());
    args.push_back(storage->storage_location->c_str());
    args.push_back(target_dir->c_str());
    rc = sys->invoke("rsync", args, &out);

    local_fs->deleteFile(tmp);

    return rc;
}

void parse_rsync_verbose_output_(StoreStatistics *st,
                                 Storage *storage,
                                 char *buf,
                                 size_t len)
{
    // Parse verbose output and look for:
    // zlib-1.2.11-winapi/z01_001516784332.462127151_0_3393fb3d96b545ebf05ad9406fff9435eca8cd3eb97714883fa42d92d2fc8ded_0.gz

    string file = storage->storage_location->str()+"/"+string(buf, len);
    string dir;
    while (file.back() == '\n') file.pop_back();
    TarFileName tfn;
    if (tfn.parseFileName(file, &dir)) {
        // This was a valid verbose output beak file name.
        Path *path = tfn.asPathWithDir(Path::lookup(dir));
        size_t size = 0;

        debug(RSYNC, "copied: %ju \"%s\"\n", st->stats.file_sizes.count(path), path->c_str());

        if (st->stats.file_sizes.count(path)) {
            size = st->stats.file_sizes[path];
            st->stats.size_files_stored += size;
            st->stats.num_files_stored++;
            st->updateProgress();
        }
    }
}

RC rsyncSendFiles(Storage *storage,
                  vector<Path*> *files,
                  Path *dir,
                  StoreStatistics *st,
                  FileSystem *local_fs,
                  ptr<System> sys)
{
    string files_to_fetch;
    for (auto& p : *files) {
        // The p file will begin with /, but this is ok for rsync.
        files_to_fetch.append(p->str());
        files_to_fetch.append("\n");
    }
    Path *tmp = local_fs->mkTempFile("beak_sending_", files_to_fetch);

    vector<string> args;
    args.push_back("-a");
    args.push_back("-v");
    args.push_back("--files-from");
    args.push_back(tmp->c_str());

    string p = dir->str()+"/"; // not strictly needed since we have --files-from
    args.push_back(p.c_str());
    args.push_back(storage->storage_location->str());
    vector<char> output;
    RC rc = sys->invoke("rsync", args, &output, CaptureBoth,
                        [&st, storage](char *buf, size_t len) {
                            parse_rsync_verbose_output_(st,
                                                        storage,
                                                        buf,
                                                        len);
                        });

    local_fs->deleteFile(tmp);

    return rc;
}
