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

#include "storage_rclone.h"

#include "log.h"
#include "statistics.h"

using namespace std;

static ComponentId RCLONE = registerLogComponent("rclone");

RC rcloneListBeakFiles(Storage *storage,
                       vector<TarFileName> *files,
                       vector<TarFileName> *bad_files,
                       vector<string> *other_files,
                       map<Path*,FileStat> *contents,
                       ptr<System> sys,
                       ProgressStatistics *st)
{
    assert(storage->type == RCloneStorage);

    RC rc = RC::OK;
    vector<char> out;
    vector<string> args;

    args.push_back("ls");
    args.push_back(storage->storage_location->c_str());
    rc = sys->invoke("rclone", args, &out);

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
        string dir;
        bool ok = tfn.parseFileName(file_name, &dir);
        // Only files that have proper beakfs names are included.
        if (ok) {
            // Check that the remote size equals the content. If there is a mismatch,
            // then for sure the file must be overwritte/updated. Perhaps there was an earlier
            // transfer interruption....
            tfn.last_size = tfn.size;
            size_t siz = (size_t)atol(size.c_str());
            if ( (tfn.type != REG_FILE && tfn.size == siz) ||
                 (tfn.type == REG_FILE && tfn.size == siz) )
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


void parse_rclone_verbose_output(ProgressStatistics *st,
                                 Storage *storage,
                                 char *buf,
                                 size_t len)
{
    // Parse verbose output and look for:
    // 2018/01/29 20:05:36 INFO  : code/src/s01_001517180913.689221661_11659264_b6f526ca4e988180fe6289213a338ab5a4926f7189dfb9dddff5a30ab50fc7f3_0.tar: Copied (new)
    // And look for stat lines like:
    // 2019/01/29 22:32:37 INFO  :       185M / 2.370 GBytes, 8%, 3.079 MBytes/s, ETA 12m8s (xfr#0/242)
    size_t from, to;
    // Find the beginning of the file path.
    for (from=1; from<len-1; ++from) {
        if (buf[from-1] == ' ' && buf[from] == ':' && buf[from+1] == ' ') {
            from = from+2;
            break;
        }
    }
    // Find the end of the file path.
    for (to=len-2; to>from; --to) {
        if (buf[to] == ':' && buf[to+1] == ' ') {
            break;
        }
    }
    if (from == to) {
        // Perhaps a stat line
        // Sadly the stats are currently not usable.
        size_t slash = 0;
        for (slash=from; slash<len-1; ++slash) {
            if (buf[slash] == '/') {
                break;
            }
        }
        string size_hint_s = string(buf+from, buf+slash);
        size_t size_hint = 0;
        RC rc = parseHumanReadable(size_hint_s, &size_hint);
        if (rc.isOk()) {
            debug(RCLONE, "stat found \"%s\" => %zu \n", size_hint_s.c_str(), size_hint);
            st->updateStatHint(size_hint);
        } else {
            debug(RCLONE, "could not parse stat \"%s\"\n", size_hint_s.c_str());
        }
    }
    string file = storage->storage_location->str()+"/"+string(buf+from, to-from);
    TarFileName tfn;
    string dir;
    if (tfn.parseFileName(file, &dir)) {
        size_t size = 0;
        Path *dirp = Path::lookup(dir);
        string file_path = tfn.asStringWithDir(dirp);
        Path *path = Path::lookup(file_path);
        debug(RCLONE, "copied: %ju \"%s\"\n", st->stats.file_sizes.count(path), path->c_str());

        if (st->stats.file_sizes.count(path)) {
            size = st->stats.file_sizes[path];
            st->stats.size_files_stored += size;
            st->stats.num_files_stored++;
            st->updateProgress();
        } else {
            debug(RCLONE, "Error! No file size found for %s\n", path->c_str());
        }
    }
}

RC rcloneSendFiles(Storage *storage,
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
    args.push_back("-v");
    args.push_back("--stats-one-line");
    args.push_back("--stats=10s");
    args.push_back("--include-from");
    args.push_back(tmp->c_str());
    args.push_back(local_dir->c_str());
    args.push_back(storage->storage_location->str());
    vector<char> output;
    RC rc = sys->invoke("rclone", args, &output, CaptureBoth,
                        [&st, storage](char *buf, size_t len) {
                            parse_rclone_verbose_output(st,
                                                        storage,
                                                        buf,
                                                        len);
                        });

    local_fs->deleteFile(tmp);

    return rc;
}

RC rcloneFetchFiles(Storage *storage,
                    vector<Path*> *files,
                    Path *local_dir,
                    System *sys,
                    FileSystem *local_fs,
                    ProgressStatistics *progress)
{
    Path *target_dir = storage->storage_location->prepend(local_dir);

    string files_to_fetch;
    for (auto& p : *files) {
        // Drop the leading storage location (eg s3_work_crypt:).
        // Rclone is only interested in the actual file name, without
        // a leading slash.
        Path *pp = p->subpath(1);
        files_to_fetch.append(pp->c_str());
        files_to_fetch.append("\n");
        debug(RCLONE, "fetch \"%s\"\n", pp->c_str());
    }

    Path *tmp = local_fs->mkTempFile("beak_fetching_", files_to_fetch);

    RC rc = RC::OK;
    vector<string> args;
    args.push_back("copy");
    args.push_back("--include-from");
    args.push_back(tmp->c_str());
    args.push_back(storage->storage_location->c_str());
    args.push_back(target_dir->c_str());
    vector<char> output;
    rc = sys->invoke("rclone", args, &output, CaptureBoth,
                     [&progress, storage](char *buf, size_t len) {
                         parse_rclone_verbose_output(progress,
                                                     storage,
                                                     buf,
                                                     len);
                     });
    local_fs->deleteFile(tmp);

    return rc;
}


RC rcloneDeleteFiles(Storage *storage,
                     std::vector<Path*> *files,
                     FileSystem *local_fs,
                     ptr<System> sys,
                     ProgressStatistics *progress)
{
    string files_to_delete;
    for (auto& p : *files) {
        files_to_delete.append(p->c_str());
        files_to_delete.append("\n");
        debug(RCLONE, "delete \"%s\"\n", p->c_str());
    }

    Path *tmp = local_fs->mkTempFile("beak_deleting_", files_to_delete);

    RC rc = RC::OK;
    vector<string> args;
    args.push_back("delete");
    args.push_back("--include-from");
    args.push_back(tmp->c_str());
    args.push_back(storage->storage_location->c_str());
    vector<char> output;
    rc = sys->invoke("rclone", args, &output, CaptureBoth,
                     [&progress, storage](char *buf, size_t len) {
                         parse_rclone_verbose_output(progress,
                                                     storage,
                                                     buf,
                                                     len);
                     });

    local_fs->deleteFile(tmp);

    return rc;
}
