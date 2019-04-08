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

#include <string>
#include <set>

#include "index.h"
#include "filesystem.h"
#include "log.h"
#include "tarentry.h"
#include "util.h"

using namespace std;

ComponentId INDEX = registerLogComponent("index");

RC Index::loadIndex(vector<char> &v,
                    vector<char>::iterator &i,
                    IndexEntry *ie, IndexTar *it,
                    Path *dir_to_prepend,
                    size_t *size,
                    function<void(IndexEntry*)> on_entry,
                    function<void(IndexTar*)> on_tar)
{
    vector<char>::iterator ii = i;

    bool eof, err;
    string header = eatTo(v, i, separator, 30 * 1024 * 1024, &eof, &err);

    vector<char> data(header.begin(), header.end());
    auto j = data.begin();

    // The first line should be #beak 0.8
    string type = eatTo(data, j, '\n', 64, &eof, &err);

    int beak_version = 0;
    if (!startsWith(type, "#beak ")) {
        failure(INDEX, "Not a proper \"#beak x.x\" header in index file. [%d]\n", __LINE__);
        return RC::ERR;
    }
    string vers = type.substr(6);
    if (vers == "0.7") {
        beak_version = 70;
    } else if (vers == "0.8") {
        beak_version = 80;
    } else if (vers == "0.81") {
        beak_version = 81;
    } else {
        failure(INDEX,
                "Version was \"%s\" which is not the supported 0.7 or 0.8\n",
                type.c_str());
        return RC::ERR;
    }

    string config;
    int num_files = 0;

    for (;;) {
        string line = eatTo(data, j, '\n', 1024, &eof, &err); // Command line switches can be 1024 bytes long
        if (err)
        {
            failure(INDEX, "Unexpected error reading index file. [%d]\n", __LINE__);
            return RC::ERR;
        }
        debug(INDEX, "Read \"%s\"\n", line.c_str());
        if (startsWith(line, "#config "))
        {
            config = line.substr(8);
        }
        else if (startsWith(line, "#size ")) {
            int n = sscanf(line.c_str(), "#size %zu", size);
            if (n != 1) {
                failure(INDEX, "File format error gz file. [%d]\n", __LINE__);
                return RC::ERR;
            }
        }
        else if (startsWith(line, "#filecolumns ")) {
            // Just ignore columns, it is for information only right now.
        }
        else if (startsWith(line, "#uids ")) {
            // Ignore the uid info.
        }
        else if (startsWith(line, "#gids ")) {
            // Ignore the uid info.
        }
        else if (startsWith(line, "#files ")) {
            int n = sscanf(line.c_str(), "#files %d", &num_files);
            if (n != 1) {
                failure(INDEX, "File format error gz file. [%d]\n", __LINE__);
                return RC::ERR;
            }
            break;
        }
        else {
            debug(INDEX, "Ignoring unknown entry: %s\n", line.c_str());
        }
    }

    const char *dtp = "";
    if (dir_to_prepend) dtp = dir_to_prepend->c_str();
    debug(INDEX, "loading gz for %s with %s and %d files prepend \"%s\".\n", dtp, config.c_str(), num_files, dtp);
    eof = false;
    while (i != v.end() && !eof && num_files > 0)
    {
        ii = i;
        bool got_entry = eatEntry(beak_version, v, i, dir_to_prepend, &ie->fs, &ie->offset,
                                  &ie->tar, &ie->path, &ie->link,
                                  &ie->is_sym_link, &ie->is_hard_link,
                                  &ie->num_parts, &ie->part_offset,
                                  &ie->part_size, &ie->last_part_size,
                                  &eof, &err);
        if (err) {
            failure(INDEX, "Could not parse index file in >%s<\n>%s<\n", dtp, ii);
            break;
        }
        if (!got_entry) break;
        on_entry(ie);
        num_files--;
    }

    if (num_files != 0) {
        failure(INDEX, "Error in gz file format!");
        return RC::ERR;
    }

    string tar_header = eatTo(v, i, separator, 30 * 1024 * 1024, &eof, &err);

    vector<char> tar_data(tar_header.begin(), tar_header.end());
    j = tar_data.begin();

    string tars = eatTo(data, j, '\n', 64, &eof, &err);

    int num_tars = 0;
    int n = sscanf(tars.c_str(), "#tars %d", &num_tars);
    if (n != 1) {
        failure(INDEX, "File format error gz file. [%d]\n", __LINE__);
        return RC::ERR;
    }
    debug(INDEX,"found num tars %d\n", num_tars);

    string name;
    eof = false;
    while (i != v.end() && !eof && num_tars > 0) {
        name = eatTo(v, i, separator, 4096, &eof, &err); // Max path names 4096 bytes
        if (err) {
            failure(INDEX, "Could not parse tarredfs-tars file!\n");
            break;
        }
        // Remove the newline at the end.
        name.pop_back();
        if (name.length()==0) continue;
        auto dots = name.find(" ... ");

        if (dots != string::npos)
        {
            TarFileName fromfile, tofile;
            string from = name.substr(0,dots);
            string to = name.substr(dots+5);
            Path *dir = Path::lookup(from)->parent();
            fromfile.parseFileName(from);
            tofile.parseFileName(to);
            fromfile.last_size = tofile.size;
            for (uint i=0; i<fromfile.num_parts; ++i) {
                char buf[1024];
                fromfile.part_nr = i;
                fromfile.writeTarFileNameIntoBuffer(buf, sizeof(buf), dir);
                Path *pp = Path::lookup(buf);
                it->path = pp;
                on_tar(it);
            }
            num_tars--;
        }
        else
        {
            Path *p = Path::lookup(name);
            if (p->parent()) {
                debug(INDEX, "found tar %d %s in dir %s\n", num_tars,  p->name()->c_str(), p->parent()->c_str());
            } else {
                debug(INDEX, "found tar %d %s\n", num_tars, p->name()->c_str());
            }

            it->path = p;
            on_tar(it);
            num_tars--;
        }
    }

    if (num_tars != 0) {
        failure(INDEX, "File format error gz file. [%d]\n", __LINE__);
        return RC::ERR;
    }

    string parts = eatTo(v, i, separator, 4096, &eof, &err); // Max path names 4096 bytes
    if (err) {
        failure(INDEX, "Could not parse tarredfs-tars file!\n");
        return RC::ERR;
    }

    int num_parts = 0;
    n = sscanf(parts.c_str(), "#parts %d", &num_parts);
    if (n != 1) {
        failure(INDEX, "File format error gz file.\"%s\"[%d]\n", parts.c_str(), __LINE__);
        return RC::ERR;
    }
    debug(INDEX,"found num parts %d\n", num_parts);
    eof = false;
    while (i != v.end() && !eof && num_parts > 0) {
        string name = eatTo(v, i, separator, 4096, &eof, &err); // Max path names 4096 bytes
        if (err) {
            failure(INDEX, "Could not parse tarredfs-tars file!\n");
            break;
        }
        // Remove the newline at the end.
        name.pop_back();
        num_parts--;
    }

    if (num_parts != 0) {
        failure(INDEX, "File format error gz file. [%d]\n", __LINE__);
        return RC::ERR;
    }

    auto endofcontent = i;
    string sha256s = eatTo(v, i, separator, 4096, &eof, &err); // sha256
    if (err) {
        failure(INDEX, "Could not parse tarredfs-tars file!\n");
        return RC::ERR;
    }

    if (beak_version >= 81) {
        char hex[65];
        hex[64] = 0;
        n = sscanf(sha256s.c_str(), "#end %64s", hex);
        if (n != 1) {
            failure(INDEX, "File format error gz file. [%d]\n", __LINE__);
            return RC::ERR;
        }

        string read_hexs = string(hex);
        vector<char> sha256_hash;
        sha256_hash.resize(SHA256_DIGEST_LENGTH);
        {
            SHA256_CTX sha256ctx;
            SHA256_Init(&sha256ctx);
            SHA256_Update(&sha256ctx, (unsigned char*)&v[0], endofcontent-v.begin());
            SHA256_Final((unsigned char*)&sha256_hash[0], &sha256ctx);
        }
        string calc_hexs = toHex(sha256_hash);
        debug(INDEX, "index checksum: %s calculated: %s\n",
              read_hexs.c_str(), calc_hexs.c_str());

        if (read_hexs != calc_hexs) {
            failure(INDEX, "Index file checksum did not match!\nRead:       %s\nCalculated: %s\n",
                    read_hexs.c_str(), calc_hexs.c_str());
            return RC::ERR;
        }
    }
    return RC::OK;
};
