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

#include "diff.h"
#include "index.h"
#include "filesystem.h"
#include "log.h"
#include "tarentry.h"
#include "tarfile.h"
#include "util.h"

using namespace std;

ComponentId INDEX = registerLogComponent("index");

int Index::loadIndex(vector<char> &v,
                     vector<char>::iterator &i,
                     IndexEntry *ie, IndexTar *it,
                     Path *dir_to_prepend,
                     function<void(IndexEntry*)> on_entry,
                     function<void(IndexTar*)> on_tar)
{
    vector<char>::iterator ii = i;

    bool eof, err;
    string header = eatTo(v, i, separator, 30 * 1024 * 1024, &eof, &err);

    vector<char> data(header.begin(), header.end());
    auto j = data.begin();

    string type = eatTo(data, j, '\n', 64, &eof, &err);
    string msg = eatTo(data, j, '\n', 1024, &eof, &err); // Message can be 1024 bytes long
    string uid = eatTo(data, j, '\n', 10 * 1024 * 1024, &eof, &err); // Accept up to a ~million uniq uids
    string gid = eatTo(data, j, '\n', 10 * 1024 * 1024, &eof, &err); // Accept up to a ~million uniq gids
    string files = eatTo(data, j, '\n', 64, &eof, &err);

    if (type != "#beak " XSTR(BEAK_VERSION))
    {
        failure(INDEX,
                "Type was not \"#beak " XSTR(BEAK_VERSION) "\" as expected! It was \"%s\"\n",
                type.c_str());
        return ERR;
    }

    int num_files = 0;
    int n = sscanf(files.c_str(), "#files %d", &num_files);
    if (n != 1) {
        failure(INDEX, "File format error gz file. [%d]\n", __LINE__);
        return ERR;
    }

    debug(INDEX, "Loading gz for '%s' with >%s< and %d files.\n", dir_to_prepend->c_str(), msg.c_str(), num_files);

    eof = false;
    while (i != v.end() && !eof && num_files > 0)
    {
        ii = i;
        bool got_entry = eatEntry(v, i, dir_to_prepend, &ie->fs, &ie->offset,
                                  &ie->tar, &ie->path, &ie->link,
                                  &ie->is_sym_link, &ie->is_hard_link,
                                  &eof, &err);
        if (err) {
            failure(INDEX, "Could not parse tarredfs-contents file in >%s<\n>%s<\n",
                    dir_to_prepend->c_str(), ii);
            break;
        }
        if (!got_entry) break;
        on_entry(ie);
        num_files--;
    }

    if (num_files != 0) {
        failure(INDEX, "Error in gz file format!");
        return ERR;
    }

    string tar_header = eatTo(v, i, separator, 30 * 1024 * 1024, &eof, &err);

    vector<char> tar_data(tar_header.begin(), tar_header.end());
    j = tar_data.begin();

    string tars = eatTo(data, j, '\n', 64, &eof, &err);

    int num_tars = 0;
    n = sscanf(tars.c_str(), "#tars %d", &num_tars);
    if (n != 1) {
        failure(INDEX, "File format error gz file. [%d]\n", __LINE__);
        return ERR;
    }

    eof = false;
    while (i != v.end() && !eof && num_tars > 0) {
        string name = eatTo(v, i, separator, 4096, &eof, &err); // Max path names 4096 bytes
        if (err) {
            failure(INDEX, "Could not parse tarredfs-tars file!\n");
            break;
        }
        // Remove the newline at the end.
        name.pop_back();
        Path *p = Path::lookup(name);
        debug(INDEX,"  found tar %s in dir %s\n", p->name()->c_str(), p->parent()->c_str());
        it->path = p;
        on_tar(it);
        num_tars--;
    }

    if (num_tars != 0) {
        failure(INDEX, "File format error gz file. [%d]\n", __LINE__);
        return ERR;
    }
    return OK;


};
