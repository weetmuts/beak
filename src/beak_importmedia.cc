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
#include "storagetool.h"
#include "system.h"


extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

#include <exiv2/exiv2.hpp>
#include <exiv2/error.hpp>

static ComponentId IMPORTMEDIA = registerLogComponent("importmedia");

string generate_name(string prefix, struct timespec &ts, struct tm &tm, size_t size, string ext, int width, int height)
{
    string name;
    strprintf(name, "/%04d/%02d/%02d/%s_%04d%02d%02d_%02d%02d%02d_%lu.%lu_%dx%d_%zu.%s",
              tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday, prefix.c_str(),
              tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday, tm.tm_hour, tm.tm_min, tm.tm_sec, ts.tv_sec, ts.tv_nsec,
              width, height,
              size, ext.c_str());
    return name;
}

struct ImportMediaData
{
    BeakImplementation *beak_ {};
    int num_ {};
    int unknown_count_ {};
    size_t unknown_size_ {};

    map<string,int> img_suffix_count_;
    map<string,int> vid_suffix_count_;
    map<string,int> aud_suffix_count_;
    map<string,int> unknown_suffix_count_;

    map<string,size_t> img_suffix_size_;
    map<string,size_t> vid_suffix_size_;
    map<string,size_t> aud_suffix_size_;
    map<string,size_t> unknown_suffix_size_;

    Settings *settings_ {};
    Monitor *monitor_ {};
    FileSystem *fs_ {};

    set<Path*> inconsistent_dates_;
    set<Path*> failed_to_understand_;

    set<Path*> files_up_to_date_;
    map<Path*,Path*> copy_files_;

    map<string,string> img_suffixes_;
    map<string,string> vid_suffixes_;
    map<string,string> aud_suffixes_;

    string xmq_;

    ImportMediaData(BeakImplementation *beak, Settings *settings, Monitor *monitor, FileSystem *fs)
        : beak_(beak), settings_(settings), monitor_(monitor), fs_(fs)
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

        vid_suffixes_["wmv"] = "wmv";
        vid_suffixes_["WMV"] = "wmv";

        img_suffixes_["jpg"] = "jpg";
        img_suffixes_["jpeg"] = "jpg";

        img_suffixes_["JPG"] = "jpg";
        img_suffixes_["JPEG"] = "jpg";

        img_suffixes_["ogg"] = "ogg";
        img_suffixes_["OGG"] = "ogg";

        img_suffixes_["png"] = "png";
        img_suffixes_["PNG"] = "png";

        Magick::InitializeMagick(NULL);
    }

    string status(const char *tense)
    {
        string info;
        for (auto &p : vid_suffix_count_)
        {
            string s = humanReadable(vid_suffix_size_[p.first]);
            info += p.first+"("+to_string(p.second)+" / "+s+") ";
        }
        for (auto &p : img_suffix_count_)
        {
            string s = humanReadable(img_suffix_size_[p.first]);
            info += p.first+"("+to_string(p.second)+" / "+s+") ";
        }
        for (auto &p : aud_suffix_count_)
        {
            string s = humanReadable(aud_suffix_size_[p.first]);
            info += p.first+"("+to_string(p.second)+" / "+s+") ";
        }
        if (unknown_suffix_count_.size() > 0)
        {
            string s = humanReadable(unknown_size_);
            info += "non-media("+to_string(unknown_count_)+" / "+s+") ";
        }
        if (info.length() > 0) info.pop_back();
        string st;
        strprintf(st, "Index%s %s", tense, info.c_str());
        return st;
    }

    string statusUnknowns()
    {
        string info;
        for (auto &p : unknown_suffix_count_)
        {
            if (p.first != "")
            {
                string s = humanReadable(img_suffix_size_[p.first]);
                info += p.first+"("+to_string(p.second)+" á "+s+") ";
            }
        }
        if (unknown_suffix_count_.count("") > 0)
        {
            string s = humanReadable(img_suffix_size_[""]);
            info += "unknowns("+to_string(img_suffix_count_[""])+" á "+s+") ";
        }

        if (info.length() > 0) info.pop_back();
        return info;
    }

    string brokenFiles()
    {
        string s;
        for (Path *f : failed_to_understand_)
        {
            s += f->str()+"\n";
        }
        return s;
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
            if (ts->tv_sec == -1)
            {
                // Oups the date is not a valid date!
                debug(IMPORTMEDIA, "Invalid date from path: %s\n", p->c_str());
                return false;
            }

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

    bool getDateFromVideo(Path *p, struct timespec *ts, struct tm *tm, int *width, int *height)
    {
        AVFormatContext* av = avformat_alloc_context();
        av_register_all();
        av_log_set_level(AV_LOG_FATAL);
        int rc = avformat_open_input(&av, p->c_str(), NULL, NULL);

        if (rc)
        {
            memset(ts, 0, sizeof(*ts));
            memset(tm, 0, sizeof(*tm));
            char buf[1024];
            av_strerror(rc, buf, 1024);
            debug(IMPORTMEDIA, "Cannot read video: %s because: %s\n",  p->c_str(), buf);
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
            debug(IMPORTMEDIA, "    %s = %s\n", t->key, t->value);
            if (!strcmp(t->key, "creation_time"))
            {
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

        AVCodecParameters* pCodecCtx;
        int video_stream_index = -1;
        int ret = avformat_find_stream_info(av, NULL);
        assert(ret >= 0);

        for(unsigned int i = 0; i < av->nb_streams; i++)
        {
            if(av->streams[i]->codecpar->codec_type == AVMEDIA_TYPE_VIDEO)
            {
                video_stream_index = i;
                break;
            }
        }
        if (video_stream_index != -1)
        {
            // Get a pointer to the codec context for the video stream
            pCodecCtx = av->streams[video_stream_index]->codecpar;
            assert(pCodecCtx != NULL);

            printf("\n");
            printf("Width: %d\n", pCodecCtx->width);
            printf("Height: %d\n", pCodecCtx->height);
            *width = pCodecCtx->width;
            *height = pCodecCtx->height;
        }

        avformat_close_input(&av);
        avformat_free_context(av);

        if (!found_creation_time)
        {
            debug(IMPORTMEDIA, "no creation_time found!\n");
        }

        return found_creation_time;
    }

    bool getDateFromExif(Path *p, struct timespec *ts, struct tm *tm, int *width, int *height)
    {
        bool found_datetime = false;
        try
        {
            Exiv2::LogMsg::setLevel(Exiv2::LogMsg::Level::mute);
            Exiv2::Image::AutoPtr image = Exiv2::ImageFactory::open(p->c_str());
            assert(image.get() != 0);
            image->readMetadata();
            *width = image->pixelWidth();
            *height = image->pixelHeight();
            Exiv2::ExifData &exifData = image->exifData();
            if (exifData.empty())
            {
                debug(IMPORTMEDIA, "no meta data found\n");
                return false;
            }
            else
            {
                Exiv2::ExifData::const_iterator end = exifData.end();
                for (Exiv2::ExifData::const_iterator i = exifData.begin(); i != end; ++i)
                {
                    debug(IMPORTMEDIA,"    %s = %s\n", i->key().c_str(), i->value().toString().c_str());
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
                            if (tm->tm_year == 0)
                            {
                                // There was a date here, but it does not look ok...
                                debug(IMPORTMEDIA, "Empty date: %s\n", v.c_str());
                                break;
                            }
                            tm->tm_year -= 1900;
                            tm->tm_mon -= 1;
                            memset(ts, 0, sizeof(*ts));
                            ts->tv_sec = mktime(tm);
                            if (ts->tv_sec == -1)
                            {
                                // Oups the date is not a valid date!
                                debug(IMPORTMEDIA, "Invalid date: %s\n", v.c_str());
                                break;
                            }
                            found_datetime = true;
                        }
                        break;
                    }
                }
            }
        }
        catch (std::exception &e)
        {
            debug(IMPORTMEDIA, "Failed to load %s\n", e.what());
            failed_to_understand_.insert(p);
            return false;
        }
        return found_datetime;
    }

    void handleFile(Path *p, FileStat *st, MapFileSystem *map_fs)
    {
        ssize_t size = st->st_size;
        const char *prefix = "img";
        int width = 0;
        int height = 0;
        if (st->isDirectory()) return;

        if (num_ % 100 == 0)
        {
            UI::clearLine();
            string st = status("ing");
            info(IMPORTMEDIA, "%s\n", st.c_str());
        }
        num_++;

        debug(IMPORTMEDIA, "===== examining %s\n", p->c_str());

        string ext = p->name()->ext_c_str_();

        if (img_suffixes_.count(ext) != 0) {
            ext = img_suffixes_[ext];
            img_suffix_count_[ext]++;
            img_suffix_size_[ext]+=size;

        }
        else if (vid_suffixes_.count(ext) != 0) {
            ext = vid_suffixes_[ext];
            vid_suffix_count_[ext]++;
            vid_suffix_size_[ext]+=size;
        }
        else if (aud_suffixes_.count(ext) != 0) {
            ext = aud_suffixes_[ext];
            aud_suffix_count_[ext]++;
            aud_suffix_size_[ext]+=size;
        }
        else
        {
            unknown_suffix_count_[ext]++;
            unknown_suffix_size_[ext]+=size;
            unknown_count_++;
            unknown_size_ += size;
            return;
        }

        struct timespec exif_ts {};
        struct tm exif_tm {};
        bool valid_date_from_exif = false;

        if (img_suffixes_.count(ext) > 0)
        {
            valid_date_from_exif = getDateFromExif(p, &exif_ts, &exif_tm, &width, &height);
            prefix = "img";
            string tmp = generate_name(prefix, exif_ts, exif_tm, size, ext, width, height);
            debug(IMPORTMEDIA, "exif  %s\n", tmp.c_str());
        }

        struct timespec ffmpeg_ts {};
        struct tm ffmpeg_tm {};
        bool valid_date_from_ffmpeg = false;

        if (p->name()->hasExtension("mov") ||
            p->name()->hasExtension("mp4"))
        {
            valid_date_from_ffmpeg = getDateFromVideo(p, &ffmpeg_ts, &ffmpeg_tm, &width, &height);
            prefix = "vid";
            string tmp = generate_name(prefix, exif_ts, ffmpeg_tm, size, ext, width, height);
            debug(IMPORTMEDIA, "ffmpeg %s\n", tmp.c_str());
        }

        struct timespec path_ts {};
        struct tm path_tm {};
        bool valid_date_from_path = getDateFromPath(p, &path_ts, &path_tm);

        if (!valid_date_from_exif && !valid_date_from_ffmpeg && valid_date_from_path)
        {
            string tmp = generate_name(prefix, path_ts, path_tm, size, ext, width, height);
            debug(IMPORTMEDIA, "path %s\n", tmp.c_str());
        }

        struct timespec stat_ts {};
        struct tm stat_tm {};
        bool valid_date_from_stat = getDateFromStat(st, &stat_ts, &stat_tm);

        if (!valid_date_from_exif && !valid_date_from_ffmpeg && !valid_date_from_path && valid_date_from_stat)
        {
            string tmp = generate_name(prefix, stat_ts, stat_tm, size, ext, width, height);
            debug(IMPORTMEDIA, "mtime %s\n", tmp.c_str());
        }

        if (valid_date_from_exif && valid_date_from_path &&
            (exif_tm.tm_year != path_tm.tm_year ||
             exif_tm.tm_mon != path_tm.tm_mon ||
             exif_tm.tm_mday != path_tm.tm_mday))
        {
            // Oups, image was perhaps wrongly categorized before?
            inconsistent_dates_.insert(p);
            debug(IMPORTMEDIA, "Inconsisten exif date vs path %s\n", p->c_str());
        }

        if (valid_date_from_ffmpeg && valid_date_from_path &&
            (ffmpeg_tm.tm_year != path_tm.tm_year ||
             ffmpeg_tm.tm_mon != path_tm.tm_mon ||
             ffmpeg_tm.tm_mday != path_tm.tm_mday))
        {
            // Oups, video was perhaps wrongly categorized before?
            inconsistent_dates_.insert(p);
            debug(IMPORTMEDIA, "Inconsisten ffmpeg date vs path %s\n", p->c_str());
        }
        struct timespec ts;
        struct tm tm;

        if (valid_date_from_exif)
        {
            debug(IMPORTMEDIA, "using exif date\n");
            ts = exif_ts;
            tm = exif_tm;
        }
        else if (valid_date_from_ffmpeg)
        {
            debug(IMPORTMEDIA, "using ffmpeg date\n");
            ts = ffmpeg_ts;
            tm = ffmpeg_tm;
        }
        else if (valid_date_from_path)
        {
            debug(IMPORTMEDIA, "using path date\n");
            ts = path_ts;
            tm = path_tm;
        }
        else if (valid_date_from_stat)
        {
            debug(IMPORTMEDIA, "using file mtime date\n");
            // There is always a valid date here....
            ts = stat_ts;
            tm = stat_tm;
        }
        else
        {
            debug(IMPORTMEDIA, "ERROR %s\n", p->c_str());
            exit(1);
        }

        string name = generate_name(prefix, ts, tm, size, ext, width, height);
        Path *target = Path::lookup(name);

        FileStat normalized = *st;
        normalized.st_mode = 0440;
        normalized.setAsRegularFile();
        normalized.st_mtim = ts;
        normalized.st_atim = ts;
        normalized.st_ctim = ts;

        if (failed_to_understand_.count(p) == 0)
        {
            map_fs->mapFile(normalized, target, p);
        }
    }

    void printTodo()
    {
        string s = statusUnknowns();
        info(IMPORTMEDIA, "Ignored %zu files: %s\n", unknown_count_, s.c_str());
        s = brokenFiles();
        if (s != "")
        {
            info(IMPORTMEDIA, "Warning! Broken files that cannot be imported:\n%s\n", s.c_str());
        }
    }
};

RC BeakImplementation::importMedia(Settings *settings, Monitor *monitor)
{
    RC rc = RC::OK;

    assert(settings->from.type == ArgOrigin);
    assert(settings->to.type == ArgStorage);

    ImportMediaData import_media(this, settings, monitor, local_fs_);

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
            import_media.handleFile(p, st, fs);
            return RecurseOption::RecurseContinue;
        });

    UI::clearLine();
    string st = import_media.status("ed");
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
