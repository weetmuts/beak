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

#include "storage_gphoto2.h"

#include<unistd.h>
#include<algorithm>

#include "log.h"

#include <gphoto2/gphoto2.h>

using namespace std;

static ComponentId GPHOTO2 = registerLogComponent("gphoto2");

Camera	*camera {};
GPContext *context {};

RC gphoto2ListBeakFiles(Storage *storage,
                       vector<TarFileName> *files,
                       vector<TarFileName> *bad_files,
                       vector<string> *other_files,
                       map<Path*,FileStat> *contents,
                       ptr<System> sys,
                       ProgressStatistics *st)
{
    assert(storage->type == GPhoto2Storage);
    return RC::OK;
}

RC gphoto2SendFiles(Storage *storage,
                   vector<Path*> *files,
                   Path *local_dir,
                   FileSystem *local_fs,
                   ptr<System> sys,
                   ProgressStatistics *st)
{
    assert(storage->type == GPhoto2Storage);
    return RC::OK;
}


RC gphoto2FetchFiles(Storage *storage,
                    vector<Path*> *files,
                    Path *local_dir,
                    System *sys,
                    FileSystem *local_fs,
                    ProgressStatistics *progress)
{
    assert(storage->type == GPhoto2Storage);
    return RC::OK;
}


RC gphoto2DeleteFiles(Storage *storage,
                     std::vector<Path*> *files,
                     FileSystem *local_fs,
                     ptr<System> sys,
                     ProgressStatistics *progress)
{
    assert(storage->type == GPhoto2Storage);
    return RC::OK;
}

RC gphoto2ListFiles(Storage *storage,
                   map<Path*,FileStat> *contents,
                   ptr<System> sys,
                   ProgressStatistics *st)
{
    assert(storage->type == GPhoto2Storage);
    CameraFilesystem *fs;
    CameraList *list;
    GPContext *context;

    int x = gp_filesystem_list_files(fs,
                                     "foler",
                                     list,
                                     context);

    assert(x);
    return RC::OK;
}

static void errordumper(GPLogLevel level,
                        const char *domain,
                        const char *str,
                        void *data)
{
    fprintf(stderr, "%s\n", str);
}

string gphoto2EstablishAccess(System *sys)
{
    gp_log_add_func(GP_LOG_ERROR, errordumper, NULL);
    gp_camera_new(&camera);

    return "FFOF";
}
