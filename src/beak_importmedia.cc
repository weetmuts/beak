/*
 Copyright (C) 2020-2023 Fredrik Öhrström

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

// This must be before log.h since it declares a debug macro.
#include <Magick++.h>

#include "beak.h"
#include "beak_implementation.h"
#include "backup.h"
#include "filesystem_helpers.h"
#include "log.h"
#include "storagetool.h"
#include "system.h"

static ComponentId IMPORTMEDIA = registerLogComponent("importmedia");

#include "media.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

#include <exiv2/exiv2.hpp>
#include <exiv2/error.hpp>

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

    void countFile(Path*p, FileStat *st)
    {
        db_.countFile(p, st);
    }

    void scanFile(Path *p, FileStat *st, MapFileSystem *map_fs)
    {
        Media *m = db_.addFile(p, st);
        if (m && m->type() != MediaType::Unknown)
        {
            map_fs->mapFile(m->normalizedStat(), m->normalizedFile(), p);
            UI::clearLine();
            string status = db_.status("ing");
            info(IMPORTMEDIA, "%s", status.c_str());
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

    assert(settings->from.type == ArgDir || settings->from.type == ArgFile);
    assert(settings->to.type == ArgStorage);

    // When importing, do not worry if the access times get updated.
    local_fs_->allowAccessTimeUpdates();

    std::vector<std::pair<Filter,Match>> filters;
    for (auto &e : settings->include) {
        Match m;
        bool rc = m.use(e);
        if (!rc) {
            error(IMPORTMEDIA, "Not a valid glob \"%s\"\n", e.c_str());
        }
        filters.push_back(pair<Filter,Match>(Filter(e.c_str(), INCLUDE), m));
        debug(IMPORTMEDIA, "Includes \"%s\"\n", e.c_str());
    }
    for (auto &e : settings->exclude) {
        Match m;
        bool rc = m.use(e);
        if (!rc) {
            error(IMPORTMEDIA, "Not a valid glob \"%s\"\n", e.c_str());
        }
        filters.push_back(pair<Filter,Match>(Filter(e.c_str(), EXCLUDE), m));
        debug(IMPORTMEDIA, "Excludes \"%s\"\n", e.c_str());
    }

    auto map_fs = newMapFileSystem(local_fs_);
    MapFileSystem *fs = map_fs.get();

    ImportMediaData import_media(this, settings, monitor, local_fs_, sys_);

    info(IMPORTMEDIA, "Importing media into %s\n",
         settings->to.storage->storage_location->c_str());

    if (settings->from.type == ArgDir)
    {
        local_fs_->recurse(settings->from.dir, [&import_media,fs,&filters](Path *p, FileStat *st) {
                int status = 0;
                for (auto & f : filters) {
                    bool match  = f.second.match(p->c_str());
                    int rc = (match)?0:1;
                    if (f.first.type == INCLUDE) {
                        status |= rc;
                    } else {
                        status |= !rc;
                    }
                }
                if (!status) {
                    import_media.countFile(p, st);
                }
                return RecurseOption::RecurseContinue;
            });

        local_fs_->recurse(settings->from.dir, [&import_media,fs,&filters](Path *p, FileStat *st) {
                int status = 0;
                for (auto & f : filters) {
                    bool match  = f.second.match(p->c_str());
                    int rc = (match)?0:1;
                    if (f.first.type == INCLUDE) {
                        status |= rc;
                    } else {
                        status |= !rc;
                    }
                }
                if (!status) {
                    import_media.scanFile(p, st, fs);
                }
                return RecurseOption::RecurseContinue;
            });
    }
    else
    {
        FileStat st;
        local_fs_->stat(settings->from.file, &st);
        import_media.scanFile(settings->from.file, &st, fs);
    }

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
