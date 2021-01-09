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

#include "beak.h"
#include "beak_implementation.h"
#include "backup.h"
#include "filesystem_helpers.h"
#include "log.h"
#include "storagetool.h"
#include "system.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

#include <exiv2/exiv2.hpp>

static ComponentId IMPORTMEDIA = registerLogComponent("importmedia");

string generate_name(string prefix, struct timespec &ts, struct tm &tm, size_t size, string ext)
{
    string name;
    strprintf(name, "/%04d/%02d/%02d/%s_%04d%02d%02d_%02d%02d%02d_%lu.%lu_%zu.%s",
              tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday, prefix.c_str(),
              tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, ts.tv_sec, ts.tv_nsec,
              size, ext.c_str());
    return name;
}

struct ImportMediaData
{
    BeakImplementation *beak_ {};
    size_t *sizes_ {};
    size_t *num_ {};
    Settings *settings_ {};
    Monitor *monitor_ {};
    FileSystem *fs_ {};

    set<Path*> unknown_files_;
    size_t unknown_sizes_ {};
    set<Path*> zero_length_files_;
    set<string> unknown_suffixes_;
    set<Path*> inconsistent_dates_;
    set<Path*> failed_to_understand_;

    set<Path*> files_up_to_date_;
    set<Path*> remove_files_;
    map<Path*,Path*> copy_files_;

    map<string,string> img_suffixes_;
    map<string,string> vid_suffixes_;
    map<string,string> aud_suffixes_;

    ImportMediaData(BeakImplementation *beak, size_t *sizes, size_t *num, Settings *settings, Monitor *monitor, FileSystem *fs)
        : beak_(beak), sizes_(sizes), num_(num), settings_(settings), monitor_(monitor), fs_(fs)
    {
        vid_suffixes_["avi"] = "avi";
        vid_suffixes_["AVI"] = "avi";

        vid_suffixes_["flv"] = "flv";
        vid_suffixes_["FLV"] = "flv";

        vid_suffixes_["m4v"] = "m4v";
        vid_suffixes_["M4V"] = "m4v";

        vid_suffixes_["mov"] = "mov";
        vid_suffixes_["MOV"] = "mov";

        vid_suffixes_["mkv"] = "mkv";
        vid_suffixes_["MKV"] = "mkv";

        vid_suffixes_["mp4"] = "mp4";
        vid_suffixes_["MP4"] = "mp4";

        vid_suffixes_["webm"] = "webm";
        vid_suffixes_["WEBM"] = "webm";

        img_suffixes_["jpg"] = "jpg";
        img_suffixes_["jpeg"] = "jpg";

        img_suffixes_["JPG"] = "jpg";
        img_suffixes_["JPEG"] = "jpg";

        img_suffixes_["ogg"] = "ogg";
        img_suffixes_["OGG"] = "ogg";

        img_suffixes_["png"] = "png";
        img_suffixes_["PNG"] = "png";

    }

    bool getDateFromPath(Path *p, struct timespec *ts, struct tm *tm)
    {
        if (p->parent() == NULL) return false;
        p = p->parent();
        const char *d = p->name()->c_str();
        if (p->parent() == NULL) return false;
        p = p->parent();
        const char *m = p->name()->c_str();
        if (p->parent() == NULL) return false;
        p = p->parent();
        const char *y = p->name()->c_str();
        if (isDate(y, m, d))
        {
            memset(tm, 0, sizeof(*tm));
            tm->tm_year = atoi(y)-1900;
            tm->tm_mon = atoi(m)-1;
            tm->tm_mday = atoi(d);
            tm->tm_hour = 0;
            tm->tm_min = 0;
            tm->tm_sec = 0;
            ts->tv_nsec = 0;
            ts->tv_sec = mktime(tm);
            return true;
        }
        return false;
    }

    bool getDateFromStat(FileStat *st, struct timespec *ts, struct tm *tm)
    {
        tzset();
        localtime_r(&st->st_mtim.tv_sec, tm);
        memcpy(ts, &st->st_mtim, sizeof(*ts));
        return true;
    }

    bool getDateFromVideo(Path *p, struct timespec *ts, struct tm *tm)
    {
        AVFormatContext* av = avformat_alloc_context();
        av_register_all();
        int rc = avformat_open_input(&av, p->c_str(), NULL, NULL);

        if (rc)
        {
            memset(ts, 0, sizeof(*ts));
            memset(tm, 0, sizeof(*tm));
            char buf[1024];
            av_strerror(rc, buf, 1024);
            verbose(IMPORTMEDIA, "Cannot read video: %s because: %s\n",  p->c_str(), buf);
            failed_to_understand_.insert(p);
            avformat_close_input(&av);
            avformat_free_context(av);
            return false;
        }
        assert(av != NULL);
        assert(av->metadata != NULL);

        //int64_t duration = av->duration;
        AVDictionary *d = av->metadata;
        bool found_creation_time = false;
        AVDictionaryEntry *t = NULL;
        //av_dict_set(&d, "foo", "bar", 0); // add an entry
        //char *k = av_strdup("key");       // if your strings are already allocated,
        //char *v = av_strdup("value");     // you can avoid copying them like this
        //av_dict_set(&d, k, v, AV_DICT_DONT_STRDUP_KEY | AV_DICT_DONT_STRDUP_VAL);
        while ((t = av_dict_get(d, "", t, AV_DICT_IGNORE_SUFFIX)))
        {
            if (!strcmp(t->key, "creation_time"))
            {
                verbose(IMPORTMEDIA, "%s = %s\n", t->key, t->value);
                string ct = t->value;
                RC rc = parseDateTimeUTCNanos(ct, &ts->tv_sec, &ts->tv_nsec);
                if (rc.isOk())
                {
                    tzset();
                    localtime_r(&ts->tv_sec, tm);
                    found_creation_time = true;
                    break;
                }
            }
        }

        avformat_close_input(&av);
        avformat_free_context(av);

        if (!found_creation_time) failed_to_understand_.insert(p);

        return found_creation_time;
    }

    bool getDateFromExif(Path *p, struct timespec *ts, struct tm *tm)
    {
        bool found_datetime = false;
        try
        {
            Exiv2::Image::AutoPtr image = Exiv2::ImageFactory::open(p->c_str());
            assert(image.get() != 0);
            image->readMetadata();
            Exiv2::ExifData &exifData = image->exifData();
            if (exifData.empty())
            {
                verbose(IMPORTMEDIA, "no meta data found\n");
                return false;
            }
            else
            {
                Exiv2::ExifData::const_iterator end = exifData.end();
                for (Exiv2::ExifData::const_iterator i = exifData.begin(); i != end; ++i)
                {
                    // Match both DateTime and DateTimeOriginal
                    if (i->key().find("Exif.Image.DateTime") == 0)
                    {
                        const char *tn = i->typeName();
                        tn = tn?tn : "Unknown";
                        string v = i->value().toString();
                        if (!strcmp(tn,"Ascii"))
                        {
                            const char *data = v.c_str();
                            memset(tm, 0, sizeof(*tm));
                            sscanf(data, "%d:%d:%d %d:%d:%d",
                                   &tm->tm_year, &tm->tm_mon, &tm->tm_mday, &tm->tm_hour, &tm->tm_min, &tm->tm_sec);
                            tm->tm_year -= 1900;
                            tm->tm_mon -= 1;
                            memset(ts, 0, sizeof(*ts));
                            ts->tv_sec = mktime(tm);
                            found_datetime = true;
                        }
                        break;
                    }
                }
            }
        }
        catch (std::exception &e)
        {
            verbose(IMPORTMEDIA, "Failed to load %s\n", e.what());
            return false;
        }
        return found_datetime;
    }

    void handleFile(Path *p, FileStat *st, MapFileSystem *map_fs)
    {
        ssize_t size = st->st_size;
        const char *prefix = "img";

        if (st->isDirectory()) return;

        verbose(IMPORTMEDIA, "file %s\n", p->c_str());

        if (size == 0)
        {
            zero_length_files_.insert(p);
            verbose(IMPORTMEDIA, "zero size, ignoring.\n");
            return;
        }

        string ext = p->name()->ext_c_str_();

        if (img_suffixes_.count(ext) == 0 &&
            vid_suffixes_.count(ext) == 0 &&
            aud_suffixes_.count(ext) == 0)
        {
            unknown_files_.insert(p);
            unknown_suffixes_.insert(ext);
            unknown_sizes_ += size;
            verbose(IMPORTMEDIA, "unknown extension.\n");
            return;
        }

        struct timespec path_ts {};
        struct tm path_tm {};
        bool valid_date_from_path = getDateFromPath(p, &path_ts, &path_tm);

        if (valid_date_from_path) {
            string tmp = generate_name(prefix, path_ts, path_tm, size, ext);
            verbose(IMPORTMEDIA, "path %s\n", tmp.c_str());
        }

        struct timespec stat_ts {};
        struct tm stat_tm {};
        bool valid_date_from_stat = getDateFromStat(st, &stat_ts, &stat_tm);

        if (valid_date_from_stat) {
            string tmp = generate_name(prefix, stat_ts, stat_tm, size, ext);
            verbose(IMPORTMEDIA, "mtime %s\n", tmp.c_str());
        }

        struct timespec exif_ts {};
        struct tm exif_tm {};
        bool valid_date_from_exif = false;

        if (img_suffixes_.count(ext) > 0)
        {
            valid_date_from_exif = getDateFromExif(p, &exif_ts, &exif_tm);
            prefix = "img";
            string tmp = generate_name(prefix, exif_ts, exif_tm, size, ext);
            verbose(IMPORTMEDIA, "exif  %s\n", tmp.c_str());
        }

        struct timespec ffmpeg_ts {};
        struct tm ffmpeg_tm {};
        bool valid_date_from_ffmpeg = false;

        if (p->name()->hasExtension("mov") ||
            p->name()->hasExtension("mp4"))
        {
            valid_date_from_ffmpeg = getDateFromVideo(p, &ffmpeg_ts, &ffmpeg_tm);
            prefix = "vid";
            string tmp = generate_name(prefix, exif_ts, ffmpeg_tm, size, ext);
            verbose(IMPORTMEDIA, "ffmpeg %s\n", tmp.c_str());
        }

        if (valid_date_from_exif && valid_date_from_path &&
            (exif_tm.tm_year != path_tm.tm_year ||
             exif_tm.tm_mon != path_tm.tm_mon ||
             exif_tm.tm_mday != path_tm.tm_mday))
        {
            // Oups, image was perhaps wrongly categorized before?
            inconsistent_dates_.insert(p);
            verbose(IMPORTMEDIA, "Inconsisten exif date vs path %s\n", p->c_str());
        }

        if (valid_date_from_ffmpeg && valid_date_from_path &&
            (ffmpeg_tm.tm_year != path_tm.tm_year ||
             ffmpeg_tm.tm_mon != path_tm.tm_mon ||
             ffmpeg_tm.tm_mday != path_tm.tm_mday))
        {
            // Oups, video was perhaps wrongly categorized before?
            inconsistent_dates_.insert(p);
            verbose(IMPORTMEDIA, "Inconsisten ffmpeg date vs path %s\n", p->c_str());
        }
        struct timespec ts;
        struct tm tm;

        if (valid_date_from_exif)
        {
            verbose(IMPORTMEDIA, "using exif date\n");
            ts = exif_ts;
            tm = exif_tm;
        }
        else if (valid_date_from_ffmpeg)
        {
            verbose(IMPORTMEDIA, "using ffmpeg date\n");
            ts = ffmpeg_ts;
            tm = ffmpeg_tm;
        }
        else if (valid_date_from_path)
        {
            verbose(IMPORTMEDIA, "using path date\n");
            ts = path_ts;
            tm = path_tm;
        }
        else if (valid_date_from_stat)
        {
            verbose(IMPORTMEDIA, "using file mtime date\n");
            // There is always a valid date here....
            ts = stat_ts;
            tm = stat_tm;
        }
        else
        {
            verbose(IMPORTMEDIA, "ERROR %s\n", p->c_str());
            exit(1);
        }

        string name = generate_name(prefix, ts, tm, size, ext);
        Path *target = Path::lookup(name);

        FileStat normalized = *st;
        normalized.st_mode = 0440;
        normalized.setAsRegularFile();
        normalized.st_mtim = ts;
        normalized.st_atim = ts;
        normalized.st_ctim = ts;

        /*
        string a = st->str();
        string b = normalized.str();

        fprintf(stderr, "A=%s\nB=%s\n", a.c_str(), b.c_str());
        */
        FileStat existing;

        RC check = fs_->stat(target, &existing);
        if (check.isErr())
        {
            map_fs->mapFile(normalized, target, p);
            copy_files_[target] = p;
            verbose(IMPORTMEDIA, "copying %s\n", name.c_str());
        }
        else
        {
            // The file exists, compare the desired timestamp ts, with the file tst timestamp.
            if (ts.tv_sec != existing.st_mtim.tv_sec ||
                ts.tv_nsec != existing.st_mtim.tv_nsec ||
                size != existing.st_size)
            {
                struct tm tmt;
                localtime_r(&existing.st_mtim.tv_sec, &tmt);

                int ydiff = tm.tm_year-tmt.tm_year;
                int modiff = tm.tm_mon-tmt.tm_mon;
                int ddiff = tm.tm_mday-tmt.tm_mday;
                int hdiff = tm.tm_hour-tmt.tm_hour;
                int mdiff = tm.tm_min-tmt.tm_min;
                int sdiff = tm.tm_sec-tmt.tm_sec;

                verbose(IMPORTMEDIA, "File diff %d %d %d %d %d %d %zd, %s\n",
                     ydiff, modiff, ddiff, hdiff, mdiff, sdiff, size-existing.st_size, target->c_str());

                verbose(IMPORTMEDIA, "remove %s\n", name.c_str());
                remove_files_.insert(target);
                verbose(IMPORTMEDIA, "copying %s\n", name.c_str());
                map_fs->mapFile(normalized, target, p);
                copy_files_[target] = p;
            }
            else
            {
                verbose(IMPORTMEDIA, "nochange %s\n", name.c_str());
                files_up_to_date_.insert(target);
            }
        }
    }

    void printTodo()
    {
        info(IMPORTMEDIA, "Skipping %zu already up to date files.\n", files_up_to_date_.size());
        info(IMPORTMEDIA, "Removing %zu files.\n", remove_files_.size());
        info(IMPORTMEDIA, "Copying %zu files.\n", copy_files_.size());
    }

};

RC BeakImplementation::importMedia(Settings *settings, Monitor *monitor)
{
    RC rc = RC::OK;

    assert(settings->from.type == ArgOrigin);
    assert(settings->to.type == ArgStorage);

    size_t sizes = 0;
    size_t num = 0;

    ImportMediaData import_media(this, &sizes, &num, settings, monitor, local_fs_);

    auto map_fs = newMapFileSystem(local_fs_);
    MapFileSystem *fs = map_fs.get();

    FileStat origin_dir_stat;
    local_fs_->stat(settings->from.origin, &origin_dir_stat);
    if (!origin_dir_stat.isDirectory())
    {
        usageError(IMPORTMEDIA, "Not a directory: %s\n", settings->from.origin->c_str());
    }
    local_fs_->recurse(settings->from.origin, [&import_media,fs](Path *p, FileStat *st) {
            import_media.handleFile(p, st, fs);
            return RecurseOption::RecurseContinue;
        });

    UI::clearLine();
    import_media.printTodo();

    unique_ptr<ProgressStatistics> progress = monitor->newProgressStatistics(buildJobName("import", settings));

    progress->startDisplayOfProgress();
    storage_tool_->copyBackupIntoStorage(map_fs.get(),
                              Path::lookupRoot(),
                              settings->to.storage,
                              settings,
                              progress.get());

    return rc;
}
