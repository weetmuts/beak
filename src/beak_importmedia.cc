/*
 Copyright (C) 2020 Fredrik Öhrström

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

#include <Magick++.h>

#include "beak.h"
#include "beak_implementation.h"
#include "backup.h"
#include "filesystem_helpers.h"
#include "log.h"
#include "media.h"
#include "storagetool.h"
#include "system.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

#include <exiv2/exiv2.hpp>
#include <exiv2/error.hpp>

static ComponentId IMPORTMEDIA = registerLogComponent("importmedia");

struct ImportMediaData
{
    BeakImplementation *beak_ {};
    MediaDatabase db_;
    Settings *settings_ {};
    Monitor *monitor_ {};
    FileSystem *fs_ {};

    ImportMediaData(BeakImplementation *beak, Settings *settings, Monitor *monitor, FileSystem *fs, System *sys)
        : beak_(beak), db_(fs, sys), settings_(settings), monitor_(monitor), fs_(fs)
    {
    }

    void scanFile(Path *p, FileStat *st, MapFileSystem *map_fs)
    {
        Media *m = db_.addFile(p, st);
        if (m && m->type() != MediaType::Unknown)
        {
            map_fs->mapFile(m->normalizedStat(), m->normalizedFile(), p);
        }
    }

    void printTodo()
    {
        string s = db_.statusUnknowns();
        if (s != "")
        {
            info(IMPORTMEDIA, "Ignored non-media files: %s\n", s.c_str());
        }
        s = db_.brokenFiles();
        if (s != "")
        {
            info(IMPORTMEDIA, "Broken media files that cannot be imported:\n%s", s.c_str());
        }
        s = db_.inconsistentDates();
        if (s != "")
        {
            /*info(IMPORTMEDIA, "Note! %zu files had a path that was inconsistent with the meta data creation date.\n",
              db_.inconsistent_dates_.size());*/
            verbose(IMPORTMEDIA, "%s", s.c_str());
        }
        s = db_.duplicateFiles();
        if (s != "")
        {
            /*
            info(IMPORTMEDIA, "Note! %zu duplicates into to %zu normalized media files found.",
            duplicate_count_, duplicates_.size());*/
        }
    }
};

RC BeakImplementation::importMedia(Settings *settings, Monitor *monitor)
{
    RC rc = RC::OK;

    assert(settings->from.type == ArgOrigin);
    assert(settings->to.type == ArgStorage);

    ImportMediaData import_media(this, settings, monitor, local_fs_, sys_);

    auto map_fs = newMapFileSystem(local_fs_);
    MapFileSystem *fs = map_fs.get();

    FileStat origin_dir_stat;
    local_fs_->stat(settings->from.origin, &origin_dir_stat);
    if (!origin_dir_stat.isDirectory())
    {
        usageError(IMPORTMEDIA, "Not a directory: %s\n", settings->from.origin->c_str());
    }

    info(IMPORTMEDIA, "Importing media into %s\n",
         settings->to.storage->storage_location->c_str());

    local_fs_->recurse(settings->from.origin, [&import_media,fs](Path *p, FileStat *st) {
            import_media.scanFile(p, st, fs);
            return RecurseOption::RecurseContinue;
        });

    UI::clearLine();
    string st = import_media.db_.status("ed");
    info(IMPORTMEDIA, "%s\n", st.c_str());

    import_media.printTodo();

    unique_ptr<ProgressStatistics> progress = monitor->newProgressStatistics(buildJobName("import", settings));

    progress->startDisplayOfProgress();
    storage_tool_->copyBackupIntoStorage(map_fs.get(),
                              Path::lookupRoot(),
                              settings->to.storage,
                              settings,
                              progress.get());

    if (progress->stats.num_files_stored == 0 && progress->stats.num_dirs_updated == 0) {
        info(IMPORTMEDIA, "No imports needed, everything was up to date.\n");
    }

    return rc;
}
