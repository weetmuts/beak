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
#include "log.h"
#include "system.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

#include <exiv2/exiv2.hpp>

static ComponentId SLURP = registerLogComponent("slurp");

struct SlurpData
{
    BeakImplementation *beak_ {};
    size_t *sizes_ {};
    size_t *num_ {};
    Settings *settings_ {};
    Monitor *monitor_ {};
    Path *to_ {};
    FileSystem *fs_ {};

    set<Path*> unknown_files_;
    size_t unknown_sizes_ {};
    set<Path*> zero_length_files_;
    set<Path*> inconsistent_dates_;

    set<Path*> files_up_to_date_;
    set<Path*> remove_files_;
    map<Path*,Path*> link_files_;
    map<Path*,Path*> copy_files_;

    SlurpData(BeakImplementation *beak, size_t *sizes, size_t *num, Settings *settings, Monitor *monitor, Path *to, FileSystem *fs)
        : beak_(beak), sizes_(sizes), num_(num), settings_(settings), monitor_(monitor), to_(to), fs_(fs)
    {
        fs_->mkDirpWriteable(to_);
    }

    bool getDateFromPath(Path *p, int *year, int *month, int *day, int *hour, int *minute, int *seconds, long *nanos)
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
            *year = atoi(y);
            *month = atoi(m);
            *day = atoi(d);
            *hour = 0;
            *minute = 0;
            *seconds = 0;
            *nanos = 0;
        }
        return false;
    }

    bool getDateFromStat(FileStat *st, int *year, int *month, int *day, int *hour, int *minute, int *seconds, long *nanos)
    {
        struct tm datetime;
        tzset();
        localtime_r(&st->st_mtim.tv_sec, &datetime);
        *year = 1900+datetime.tm_year;
        *month = 1+datetime.tm_mon;
        *day = datetime.tm_mday;
        *hour = datetime.tm_hour;
        *minute = datetime.tm_min;
        *seconds = datetime.tm_sec;
        *nanos = st->st_mtim.tv_nsec;
        return true;
    }

    bool getDateFromVideo(Path *p, int *year, int *month, int *day, int *hour, int *minute, int *seconds, long *nanos)
    {
        AVFormatContext* av = avformat_alloc_context();
        av_register_all();
        int rc = avformat_open_input(&av, p->c_str(), NULL, NULL);

        if (rc)
        {
            *year = 0;
            *month = 0;
            *day = 0;
            *hour = 0;
            *minute = 0;
            *seconds = 0;
            *nanos = 0;
            char buf[1024];
            av_strerror(rc, buf, 1024);
            info(SLURP, "Cannot read video: %s because: %s\n",  p->c_str(), buf);
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
//                info(SLURP, "%s = %s\n", t->key, t->value);
                string ct = t->value;
                time_t tv_sec;
                long tv_nsec;
                RC rc = parseDateTimeUTCNanos(ct, &tv_sec, &tv_nsec);
                if (rc.isOk())
                {
                    struct tm datetime;
                    tzset();
                    localtime_r(&tv_sec, &datetime);
                    *year = 1900+datetime.tm_year;
                    *month = 1+datetime.tm_mon;
                    *day = datetime.tm_mday;
                    *hour = datetime.tm_hour;
                    *minute = datetime.tm_min;
                    *seconds = datetime.tm_sec;
                    *nanos = tv_nsec;
                    found_creation_time = true;
                    break;
                }
            }
        }

        avformat_close_input(&av);
        avformat_free_context(av);
        return found_creation_time;
    }

    bool getDateFromExif(Path *p, int *year, int *month, int *day, int *hour, int *minute, int *seconds, long *nanos)
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
                //info(SLURP, "no meta data found\n");
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
                            sscanf(data, "%d:%d:%d %d:%d:%d", year, month, day, hour, minute, seconds);
                            *nanos = 0;
                            found_datetime = true;
                        }
                        break;
                    }
                }
            }
        }
        catch (std::exception &e)
        {
            info(SLURP, "Failed to load %s\n", e.what());
            return false;
        }
        return found_datetime;
    }

    const char *fromExt(const char *ext)
    {
        if (!strcmp(ext, "jpg")) return "jpg";
        if (!strcmp(ext, "jpeg")) return "jpg";
        if (!strcmp(ext, "JPG")) return "jpg";
        if (!strcmp(ext, "JPEG")) return "jpg";
        if (!strcmp(ext, "png")) return "png";
        if (!strcmp(ext, "PNG")) return "png";
        if (!strcmp(ext, "mov")) return "mov";
        if (!strcmp(ext, "MOV")) return "mov";
        if (!strcmp(ext, "mp4")) return "mp4";
        if (!strcmp(ext, "MP4")) return "MP4";
        return ext;
    }

    void handleFile(Path *p, FileStat *st)
    {
        ssize_t size = st->st_size;
        const char *prefix = "img";

        if (size == 0)
        {
            zero_length_files_.insert(p);
            return;
        }
        if (!p->name()->hasExtension("jpg") &&
            !p->name()->hasExtension("jpeg") &&
            !p->name()->hasExtension("mov") &&
            !p->name()->hasExtension("mp4"))
        {
            unknown_files_.insert(p);
            unknown_sizes_ += size;
            return;
        }
        int pY, pM, pD, ph, pm, ps;
        long pn;
        bool valid_date_from_path = getDateFromPath(p, &pY, &pM, &pD, &ph, &pm, &ps, &pn);

        int sY, sM, sD, sh, sm, ss;
        long sn;
        bool valid_date_from_stat = getDateFromStat(st, &sY, &sM, &sD, &sh, &sm, &ss, &sn);

        int eY, eM, eD, eh, em, es;
        long en;
        bool valid_date_from_exif = false;
        if (p->name()->hasExtension("jpg") ||
            p->name()->hasExtension("png"))
        {
            valid_date_from_exif = getDateFromExif(p, &eY, &eM, &eD, &eh, &em, &es, &en);
            prefix = "img";
        }

        int fY, fM, fD, fh, fm, fs;
        long fn;
        bool valid_date_from_ffmpeg = false;

        if (p->name()->hasExtension("mov") ||
            p->name()->hasExtension("mp4"))
        {
            valid_date_from_ffmpeg = getDateFromVideo(p, &fY, &fM, &fD, &fh, &fm, &fs, &fn);
            prefix = "vid";
        }

        if (valid_date_from_exif && valid_date_from_path &&
            (eY != pY || eM != pM || eD != pD))
        {
            // Oups, image was perhaps wrongly categorized before?
            inconsistent_dates_.insert(p);
            info(SLURP, "Inconsisten exif date vs path %s\n", p->c_str());
            return;
        }
        if (valid_date_from_ffmpeg && valid_date_from_path &&
            (fY != pY || fM != pM || fD != pD))
        {
            // Oups, video was perhaps wrongly categorized before?
            inconsistent_dates_.insert(p);
            info(SLURP, "Inconsisten ffmpeg date vs path %s\n", p->c_str());
            return;
        }
        int Y, M, D, h, m, s;
        long n;

        if (valid_date_from_exif)
        {
            Y = eY;
            M = eM;
            D = eD;
            h = eh;
            m = em;
            s = es;
            n = en;
        }
        else if (valid_date_from_ffmpeg)
        {
            Y = fY;
            M = fM;
            D = fD;
            h = fh;
            m = fm;
            s = fs;
            n = fn;
        }
        else if (valid_date_from_path)
        {
            Y = pY;
            M = pM;
            D = pD;
            h = ph;
            m = pm;
            s = ps;
            n = pn;
        }
        else if (valid_date_from_stat)
        {
            // There is always a valid date here....
            Y = sY;
            M = sM;
            D = sD;
            h = sh;
            m = sm;
            s = ss;
            n = sn;
        }
        else
        {
            info(SLURP, "ERROR %s\n", p->c_str());
            exit(1);
        }

        bool can_hardlink = true;
        if (sY != Y || sM != M || sD != D ||
            sh != h || sm != m || ss != s ||
            sn != n)
        {
            // Oups, the timestamp does not match the required timestamp.
            // Sometimes the cameras generate almost the right mtime, diffing
            // a second or two.... Lets copy the file instead of hard linking
            // since we do not want to touch the source directory.
            can_hardlink = false;
        }

        string name;
        strprintf(name, "%04d/%02d/%02d/%s_%04d-%02d-%02d_%02d%02d%02d.%d_%zu.%s",
                  Y, M, D, prefix, Y, M, D, h, m, s, n, size, fromExt(p->name()->ext_c_str_()));
        //info(SLURP, "FROM %s TO %s\n", p->c_str(), name.c_str());
        Path *target = Path::lookup(to_->str()+"/"+name);
        FileStat tst;
        RC check = fs_->stat(target, &tst);
        if (check.isErr())
        {
            if (can_hardlink)
            {
                link_files_[target] = p;
            }
            else
            {
                copy_files_[target] = p;
            }
        }
        else
        {
            struct tm dt;
            localtime_r(&tst.st_mtim.tv_sec, &dt);
            int tY = dt.tm_year+1900;
            int tM = dt.tm_mon+1;
            int tD = dt.tm_mday;
            int th = dt.tm_hour;
            int tm = dt.tm_min;
            int ts = dt.tm_sec;

            if (tY != Y ||
                tM != M ||
                tD != D ||
                th != h ||
                tm != m ||
                ts != s ||
                tst.st_size != size)
            {
                info(SLURP, "File diff %d %d %d %d %d %d %zd, %s\n",
                      Y-tY, M-tM, D-tD, h-th, m-tm, s-ts, size-tst.st_size, target->c_str());

                remove_files_.insert(target);
                if (can_hardlink)
                {
                    link_files_[target] = p;
                }
                else
                {
                    copy_files_[target] = p;
                }
            }
            else
            {
                files_up_to_date_.insert(target);
            }
        }
    }

    void printTodo()
    {
        info(SLURP, "Skipping %zu already up to date files.\n", files_up_to_date_.size());
        info(SLURP, "Removing %zu files.\n", remove_files_.size());
        info(SLURP, "Hard linking %zu files.\n", link_files_.size());
        info(SLURP, "Copying %zu files.\n", copy_files_.size());
    }

    void doTodo()
    {
        for (Path *f : remove_files_)
        {
            fs_->deleteFile(f);
        }

        for (auto &p : link_files_)
        {
            Path *target = p.first;
            Path *origin = p.second;
            bool ok = fs_->mkDirpWriteable(target->parent());
            if (ok)
            {
                ok = fs_->createHardLink(target, NULL, origin);
                if (!ok)
                {
                    warning(SLURP, "Could not create hard link %s\n", target->c_str());
                }
            }
        }
    }

};

RC BeakImplementation::slurp(Settings *settings, Monitor *monitor)
{
    RC rc = RC::OK;

    assert(settings->from.type == ArgDir);
    assert(settings->to.type == ArgDir);
    size_t sizes = 0;
    size_t num = 0;

    SlurpData slurp(this, &sizes, &num, settings, monitor, settings->to.dir, local_fs_);

    local_fs_->recurse(settings->from.dir, [&slurp](Path *p, FileStat *st) {
            slurp.handleFile(p, st);
            return RecurseOption::RecurseContinue;
        });

    UI::clearLine();
    slurp.printTodo();
    slurp.doTodo();
    return rc;
}
