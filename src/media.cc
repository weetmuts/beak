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
#include "media.h"
#include "system.h"

extern "C" {
#include <libavformat/avformat.h>
#include <libavcodec/avcodec.h>
}

#include <exiv2/exiv2.hpp>
#include <exiv2/error.hpp>

static ComponentId MEDIA = registerLogComponent("media");

struct MediaHelper
{
    map<string,string> img_suffixes_;
    map<string,string> vid_suffixes_;
    map<string,string> aud_suffixes_;

    MediaHelper();
    bool exifEntry(Exiv2::ExifData::const_iterator i,
                   struct timespec *ts, struct tm *tm,
                   Orientation *o, SHA256_CTX *sha256ctx);
    bool iptcEntry(Exiv2::IptcData::const_iterator i,
                   struct timespec *ts, struct tm *tm, SHA256_CTX *sha256ctx);
    bool xmpEntry(Exiv2::XmpData::const_iterator i,
                  struct timespec *ts, struct tm *tm, SHA256_CTX *sha256ctx);
    bool getExiv2MetaData(Path *p,
                          struct timespec *ts, struct tm *tm,
                          int *width, int *height,
                          Orientation *o,
                          vector<char> *hash,
                          string *metas);
    bool getFFMPEGMetaData(Path *p,
                           struct timespec *ts, struct tm *tm,
                           int *width, int *height,
                           vector<char> *hash, string *metas);
    bool getDateFromStat(FileStat *st, struct timespec *ts, struct tm *tm);
    bool getDateFromPath(Path *p, struct timespec *ts, struct tm *tm);
};

MediaHelper media_helper_;

MediaHelper::MediaHelper()
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

const char *toString(MediaType mt)
{
    switch (mt)
    {
    case MediaType::IMG: return "img";
    case MediaType::VID: return "vid";
    case MediaType::AUD: return "aud";
    case MediaType::THMB: return "thmb";
    case MediaType::Unknown: return "?";
    }
    assert(0);
    return "?";
}

bool MediaHelper::exifEntry(Exiv2::ExifData::const_iterator i,
                            struct timespec *ts, struct tm *tm, Orientation *o, SHA256_CTX *sha256ctx)
{
    debug(MEDIA,"    %s = %s\n", i->key().c_str(), i->value().toString().c_str());
    // Add the key to the hash.
    SHA256_Update(sha256ctx, i->key().c_str(), i->key().length());

    unsigned char buf[i->value().size()];
    i->value().copy(buf, Exiv2::littleEndian);

    // Add the value content to the hash.
    SHA256_Update(sha256ctx, buf, i->value().size());

    // Match both DateTime and DateTimeOriginal
    if (i->key().find("Exif.Image.DateTime") == 0)
    {
        // We have found DateTime or DateTimeOriginal
        string v = i->value().toString();

        debug(MEDIA, "    Found exif date: %s\n", v.c_str());

        const char *data = v.c_str();
        memset(tm, 0, sizeof(*tm));
        sscanf(data, "%d:%d:%d %d:%d:%d",
               &tm->tm_year, &tm->tm_mon, &tm->tm_mday, &tm->tm_hour, &tm->tm_min, &tm->tm_sec);
        if (tm->tm_year == 0)
        {
            // There was a date here, but it does not look ok...
            debug(MEDIA, "Empty date: %s\n", v.c_str());
            return false;
        }
        tm->tm_year -= 1900;
        tm->tm_mon -= 1;
        memset(ts, 0, sizeof(*ts));
        ts->tv_sec = mktime(tm);
        if (ts->tv_sec == -1)
        {
            // Oups the date is not a valid date!
            debug(MEDIA, "Invalid date: %s\n", v.c_str());
            return false;
        }
        // Datetime found!
        return true;
    }

    if (i->key().find("Exif.Image.Orientation") == 0)
    {
        // We have an orientation
        string v = i->value().toString();

        if (v == "1") *o = Orientation::None;
        if (v == "6") *o = Orientation::Deg90;
        if (v == "3") *o = Orientation::Deg180;
        if (v == "8") *o = Orientation::Deg270;
        return false;
    }

    return false;
}

bool MediaHelper::iptcEntry(Exiv2::IptcData::const_iterator i,
                            struct timespec *ts, struct tm *tm, SHA256_CTX *sha256ctx)
{
    debug(MEDIA,"    %s = %s\n", i->key().c_str(), i->value().toString().c_str());
    // Add the key to the hash.
    SHA256_Update(sha256ctx, i->key().c_str(), i->key().length());

    unsigned char buf[i->value().size()];
    i->value().copy(buf, Exiv2::littleEndian);

    // Add the value content to the hash.
    SHA256_Update(sha256ctx, buf, i->value().size());

    // Iptc.Application2.DateCreated = 2017-05-29
    // Iptc.Application2.TimeCreated = 17:19:21-04:00
    if (i->key().find("Iptc.Application2.DateCreated") != 0) return false;

    // We have found DateTime or DateTimeOriginal
    string v = i->value().toString();
    //if (i->typeName() == NULL || strcmp(i->typeName(),"Ascii")) return false;

    debug(MEDIA, "    Found iptc date: %s\n", v.c_str());

    const char *data = v.c_str();
    memset(tm, 0, sizeof(*tm));
    sscanf(data, "%d-%d-%d",
           &tm->tm_year, &tm->tm_mon, &tm->tm_mday);
    if (tm->tm_year == 0)
    {
        // There was a date here, but it does not look ok...
        debug(MEDIA, "Empty date: %s\n", v.c_str());
        return false;
    }
    tm->tm_year -= 1900;
    tm->tm_mon -= 1;
    memset(ts, 0, sizeof(*ts));
    ts->tv_sec = mktime(tm);
    if (ts->tv_sec == -1)
    {
        // Oups the date is not a valid date!
        debug(MEDIA, "Invalid date: %s\n", v.c_str());
        return false;
    }
    return true;
}

bool MediaHelper::xmpEntry(Exiv2::XmpData::const_iterator i,
                           struct timespec *ts, struct tm *tm, SHA256_CTX *sha256ctx)
{
    debug(MEDIA,"    %s = %s\n", i->key().c_str(), i->value().toString().c_str());
    // Add the key to the hash.
    SHA256_Update(sha256ctx, i->key().c_str(), i->key().length());

    unsigned char buf[i->value().size()];
    i->value().copy(buf, Exiv2::littleEndian);

    // Add the value content to the hash.
    SHA256_Update(sha256ctx, buf, i->value().size());

    // Xmp.xmp.CreateDate = 2017-05-29T17:19:21-04:00
    if (i->key().find("Xmp.xmp.CreateDate") != 0) return false;

    // We have found DateTime or DateTimeOriginal
    string v = i->value().toString();
    //if (i->typeName() == NULL || strcmp(i->typeName(),"Ascii")) return false;

    debug(MEDIA, "    Found xmp date: %s\n", v.c_str());

    const char *data = v.c_str();
    memset(tm, 0, sizeof(*tm));
    char sign = ' ';
    int tzhour {}, tzminutes {};
    sscanf(data, "%d-%d-%dT%d:%d:%d%c%d:%d",
           &tm->tm_year, &tm->tm_mon, &tm->tm_mday, &tm->tm_hour, &tm->tm_min, &tm->tm_sec,
           &sign, &tzhour, &tzminutes);
    if (tm->tm_year == 0)
    {
        // There was a date here, but it does not look ok...
        debug(MEDIA, "Empty date: %s\n", v.c_str());
        return false;
    }
    tm->tm_year -= 1900;
    tm->tm_mon -= 1;
    memset(ts, 0, sizeof(*ts));
    ts->tv_sec = mktime(tm);
    if (ts->tv_sec == -1)
    {
        // Oups the date is not a valid date!
        debug(MEDIA, "Invalid date: %s\n", v.c_str());
        return false;
    }
    return true;
}

bool MediaHelper::getExiv2MetaData(Path *p,
                                   struct timespec *ts, struct tm *tm,
                                   int *width, int *height,
                                   Orientation *o,
                                   vector<char> *hash,
                                   string *metas)
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
        bool meta_data_found = false;

        Exiv2::ExifData &exifData = image->exifData();
        bool ed_found = false;
        Exiv2::IptcData &iptcData = image->iptcData();
        bool id_found = false;
        Exiv2::XmpData &xmpData = image->xmpData();
        bool xd_found = false;

        SHA256_CTX sha256ctx;
        SHA256_Init(&sha256ctx);

        auto end = exifData.end();
        for (auto i = exifData.begin(); i != end; ++i)
        {
            found_datetime |= exifEntry(i, ts, tm, o, &sha256ctx);
            if (ed_found == false)
            {
                meta_data_found = true;
                ed_found = true;
                *metas += "e";
            }
        }

        auto iend = iptcData.end();
        for (auto i = iptcData.begin(); i != iend; ++i)
        {
            found_datetime |= iptcEntry(i, ts, tm, &sha256ctx);
            if (id_found == false)
            {
                meta_data_found = true;
                id_found = true;
                *metas += "i";
            }
        }

        auto xend = xmpData.end();
        for (auto i = xmpData.begin(); i != xend; ++i)
        {
            found_datetime |= xmpEntry(i, ts, tm, &sha256ctx);
            if (xd_found == false)
            {
                meta_data_found = true;
                xd_found = true;
                *metas += "x";
            }
        }

        if (meta_data_found)
        {
            hash->resize(SHA256_DIGEST_LENGTH);
            SHA256_Final((unsigned char*)&(*hash)[0], &sha256ctx);
        }
    }
    catch (std::exception &e)
    {
        debug(MEDIA, "Failed to load %s\n", e.what());
//        failed_to_understand_.insert(p);
        return false;
    }
    return found_datetime;
}

bool MediaHelper::getDateFromPath(Path *p, struct timespec *ts, struct tm *tm)
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
            debug(MEDIA, "Invalid date from path: %s\n", p->c_str());
            return false;
        }

        return true;
    }
    return false;
}

bool MediaHelper::getDateFromStat(FileStat *st, struct timespec *ts, struct tm *tm)
{
    tzset();
    localtime_r(&st->st_mtim.tv_sec, tm);
    memcpy(ts, &st->st_mtim, sizeof(*ts));
    return true;
}

bool MediaHelper::getFFMPEGMetaData(Path *p,
                                    struct timespec *ts, struct tm *tm,
                                    int *width, int *height,
                                    vector<char> *hash, string *metas)
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
        debug(MEDIA, "Cannot read video: %s because: %s\n",  p->c_str(), buf);
        //failed_to_understand_.insert(p);
        avformat_close_input(&av);
        avformat_free_context(av);
        return false;
    }
    assert(av != NULL);
    assert(av->metadata != NULL);

    bool meta_found = false;
    //int64_t duration = av->duration;
    AVDictionary *d = av->metadata;
    bool found_creation_time = false;
    AVDictionaryEntry *t = NULL;

    SHA256_CTX sha256ctx;
    SHA256_Init(&sha256ctx);

    while ((t = av_dict_get(d, "", t, AV_DICT_IGNORE_SUFFIX)))
    {
        if (meta_found == false)
        {
            meta_found = true;
            *metas += "f";
        }
        debug(MEDIA, "    %s = %s\n", t->key, t->value);
        // Add the key value to the hash.
        SHA256_Update(&sha256ctx, t->key, strlen(t->key));
        SHA256_Update(&sha256ctx, t->value, strlen(t->value));

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

        *width = pCodecCtx->width;
        *height = pCodecCtx->height;
    }

    avformat_close_input(&av);
    avformat_free_context(av);

    if (!found_creation_time)
    {
        debug(MEDIA, "no creation_time found!\n");
    }

    hash->resize(SHA256_DIGEST_LENGTH);
    SHA256_Final((unsigned char*)&(*hash)[0], &sha256ctx);

    return found_creation_time;
}

void Media::calculateThmbSize()
{
    if (width_ == height_)
    {
        thmb_height_ = thmb_width_ = 256;
    }
    else
    {
        thmb_height_ = 256;
        thmb_width_ = (int)(256.0*((double)width_)/((double)height_));
    }
}

Path *Media::normalizedFile()
{
    if (normalized_file_ != NULL) return normalized_file_;

    string name;
    string hex = "";
    if(hash_.size() > 0) hex = toHex(hash_);

    strprintf(name, "/%04d/%02d/%02d/%s_%04d%02d%02d_%02d%02d%02d_%dx%d_%zu_%lu.%lu_%s_%s.%s",
              tm_.tm_year+1900, tm_.tm_mon+1, tm_.tm_mday, toString(type_),
              tm_.tm_year+1900, tm_.tm_mon+1, tm_.tm_mday, tm_.tm_hour, tm_.tm_min, tm_.tm_sec,
              width_, height_, size_,
              ts_.tv_sec, ts_.tv_nsec,
              metas_.c_str(),
              hex.c_str(),
              ext_.c_str());

    calculateThmbSize();

    string thmb;
    strprintf(thmb, "/thumbnails/%04d/%02d/%02d/thmb_%dx%d_%s_%04d%02d%02d_%02d%02d%02d_%dx%d_%zu_%lu.%lu_%s_%s.jpg",
              tm_.tm_year+1900, tm_.tm_mon+1, tm_.tm_mday,
              thmb_width_, thmb_height_,
              toString(type_),
              tm_.tm_year+1900, tm_.tm_mon+1, tm_.tm_mday, tm_.tm_hour, tm_.tm_min, tm_.tm_sec,
              width_, height_, size_,
              ts_.tv_sec, ts_.tv_nsec,
              metas_.c_str(),
              hex.c_str());

    strprintf(yymmdd_, "%04d%02d%02d",
    tm_.tm_year+1900, tm_.tm_mon+1, tm_.tm_mday);

    normalized_file_ = Path::lookup(name);
    thmb_file_ = Path::lookup(thmb);
    return normalized_file_;
}

MediaType mediaTypeFromString(string s)
{
    if (s == "img") return MediaType::IMG;
    if (s == "vid") return MediaType::VID;
    if (s == "aud") return MediaType::AUD;
    return MediaType::Unknown;
}

bool Media::parseFileName(Path *p)
{
    string name, type, date, time, width, height, size, sec, nsec, hex;
    bool ok;
    RC rc = RC::OK;

    // .../img_20170529_181921_650x488_0_1496074761.0_ix_f77d8ac68b8755cb00f9f22df69ffcb4ae468191768393286e646d0968b95cf2.jpg
    if (p == NULL) return false;
    name = p->name()->str();
    if (name.size() == 0) return false;

    size_t p0 = name.rfind('/');
    if (p0 == string::npos) { p0=0; } else { p0++; }

    name = name.substr(p0);
    vector<char> tmp(name.begin(), name.end());
    auto i = tmp.begin();
    bool eof, err;
    type = eatTo(tmp, i, '_', 5, &eof, &err);
    if (eof || err) goto err;
    type_ = mediaTypeFromString(type);

    date = eatTo(tmp, i, '_', 8, &eof, &err);
    if (eof || err) goto err;

    time = eatTo(tmp, i, '_', 8, &eof, &err);
    if (eof || err) goto err;

    rc = parseYYYYMMDDhhmmss(date+time, &tm_);
    if (rc.isErr()) goto err;

    width = eatTo(tmp, i, 'x', 16, &eof, &err);
    if (eof || err) goto err;
    width_ = atoi(width.c_str());

    height = eatTo(tmp, i, '_', 16, &eof, &err);
    if (eof || err) goto err;
    height_ = atoi(height.c_str());

    size = eatTo(tmp, i, '_', 16, &eof, &err);
    if (eof || err) goto err;
    size_ = atol(size.c_str());

    sec = eatTo(tmp, i, '.', 16, &eof, &err);
    if (eof || err) goto err;
    ts_.tv_sec = atol(sec.c_str());

    nsec = eatTo(tmp, i, '_', 16, &eof, &err);
    if (eof || err) goto err;
    ts_.tv_nsec = atol(nsec.c_str());

    metas_ = eatTo(tmp, i, '_', 16, &eof, &err);
    if (eof || err) goto err;

    hex = eatTo(tmp, i, '.', 64, &eof, &err);
    if (eof || err) goto err;
    ok = hex2bin(hex, &hash_);
    if (!ok) goto err;

    ext_ = eatTo(tmp, i, '.', 16, &eof, &err);
    if (!eof) goto err;

    return true;

    err:
    return false;
}

bool Media::readFile(Path *p, FileStat *st, FileSystem *fs)
{
    source_file_ = p;
    source_stat_ = *st;

    width_ = 0;
    height_ = 0;
    metas_ = "";

    if (!st->isRegularFile()) return false;

    size_ = source_stat_.st_size;

    debug(MEDIA, "examining %s\n", p->c_str());

    string ext = p->name()->ext_c_str_();

    if (media_helper_.img_suffixes_.count(ext) != 0)
    {
        ext_ = media_helper_.img_suffixes_[ext];
        type_ = MediaType::IMG;
    }
    else if (media_helper_.vid_suffixes_.count(ext) != 0)
    {
        ext_ = media_helper_.vid_suffixes_[ext];
        type_ = MediaType::VID;
    }
    else if (media_helper_.aud_suffixes_.count(ext) != 0)
    {
        ext_ = media_helper_.aud_suffixes_[ext];
        type_ = MediaType::AUD;
    }
    else
    {
        type_ = MediaType::Unknown;
        ext_ = ext;
        return false;
    }

    struct timespec exif_ts {};
    struct tm exif_tm {};
    bool valid_date_from_exif = false;

    if (media_helper_.img_suffixes_.count(ext) > 0)
    {
        valid_date_from_exif = media_helper_.getExiv2MetaData(p, &exif_ts, &exif_tm, &width_, &height_, &orientation_, &hash_, &metas_);
    }

    struct timespec ffmpeg_ts {};
    struct tm ffmpeg_tm {};
    bool valid_date_from_ffmpeg = false;

    if (media_helper_.vid_suffixes_.count(ext) > 0)
    {
        valid_date_from_ffmpeg = media_helper_.getFFMPEGMetaData(p, &ffmpeg_ts, &ffmpeg_tm, &width_, &height_, &hash_, &metas_);
    }

    struct timespec path_ts {};
    struct tm path_tm {};
    bool valid_date_from_path = media_helper_.getDateFromPath(p, &path_ts, &path_tm);

    struct timespec stat_ts {};
    struct tm stat_tm {};
    media_helper_.getDateFromStat(st, &stat_ts, &stat_tm);

    if (valid_date_from_exif)
    {
        debug(MEDIA, "using exif date\n");
        ts_ = exif_ts;
        tm_ = exif_tm;
        date_from_ = DateFoundFrom::EXIF;
    }
    else if (valid_date_from_ffmpeg)
    {
        debug(MEDIA, "using ffmpeg date\n");
        ts_ = ffmpeg_ts;
        tm_ = ffmpeg_tm;
        date_from_ = DateFoundFrom::FFMPEG;
    }
    else if (valid_date_from_path)
    {
        debug(MEDIA, "using path date\n");
        ts_ = path_ts;
        tm_ = path_tm;
        date_from_ = DateFoundFrom::PATH;
    }
    else
    {
        debug(MEDIA, "using file mtime date\n");
        // There is always a valid date here....
        ts_ = stat_ts;
        tm_ = stat_tm;
        date_from_ = DateFoundFrom::STAT;
    }

    normalized_stat_ = *st;
    normalized_stat_.st_mode = 0440;
    normalized_stat_.setAsRegularFile();
    normalized_stat_.st_mtim = ts_;
    normalized_stat_.st_atim = ts_;
    normalized_stat_.st_ctim = ts_;

    if (orientation_ == Orientation::Deg90 ||
        orientation_ == Orientation::Deg270)

    {
        int tmp = height_;
        height_ = width_;
        width_ = tmp;
    }

    return true;
}

string MediaDatabase::status(const char *tense)
{
    string info;
    for (auto &p : vid_suffix_count_)
    {
        string s = humanReadable(vid_suffix_size_[p.first]);
        info += p.first+"("+to_string(p.second)+":"+s+") ";
    }
    for (auto &p : img_suffix_count_)
    {
        string s = humanReadable(img_suffix_size_[p.first]);
        info += p.first+"("+to_string(p.second)+":"+s+") ";
    }
    for (auto &p : aud_suffix_count_)
    {
        string s = humanReadable(aud_suffix_size_[p.first]);
        info += p.first+"("+to_string(p.second)+":"+s+") ";
    }
    if (unknown_suffix_count_.size() > 0)
    {
        string s = humanReadable(unknown_size_);
        info += "non-media("+to_string(num_unknown_files_)+":"+s+") ";
    }

    if (info.length() > 0) info.pop_back();

    string st;
    strprintf(st, "Scann%s %s", tense, info.c_str());
    return st;
}

string MediaDatabase::statusUnknowns()
{
    string info;
    for (auto &p : unknown_suffix_count_)
    {
        if (p.first != "")
        {
            string s = humanReadable(unknown_suffix_size_[p.first]);
            info += p.first+"("+to_string(p.second)+":"+s+") ";
        }
    }
    if (unknown_suffix_count_.count("") > 0)
    {
        string s = humanReadable(unknown_suffix_size_[""]);
        info += "unknowns("+to_string(unknown_suffix_count_[""])+":"+s+") ";
    }

    if (info.length() > 0) info.pop_back();
    return info;
}

string MediaDatabase::brokenFiles()
{
    string s;
    for (Path *f : failed_to_understand_)
    {
        s += f->str()+"\n";
    }
    return s;
}

string MediaDatabase::inconsistentDates()
{
    string s;
    for (Path *f : inconsistent_dates_)
    {
        s += f->str()+"\n";
    }
    return s;
}

string MediaDatabase::duplicateFiles()
{
    string s;
    for (auto &p : duplicates_)
    {
        s += p.first->str()+"\n";
    }
    return s;
}


Media *MediaDatabase::addFile(Path *p, FileStat *st)
{
    if (media_files_.count(p) != 0)
    {
        warning(MEDIA, "internal warning, trying to add same file again. %s\n", p->c_str());
        return &media_files_[p];
    }
    Media *m = &media_files_[p];
    bool ok = m->readFile(p, st, fs_);
    if (ok) {
        if (m->type() == MediaType::IMG)
        {
            img_suffix_count_[m->ext()]++;
            img_suffix_size_[m->ext()]+=st->st_size;
        }
        if (m->type() == MediaType::VID)
        {
            vid_suffix_count_[m->ext()]++;
            vid_suffix_size_[m->ext()]+=st->st_size;
        }
        if (m->type() == MediaType::AUD)
        {
            aud_suffix_count_[m->ext()]++;
            aud_suffix_size_[m->ext()]+=st->st_size;
        }
        return m;
    }
    return NULL;
}


RC MediaDatabase::generateThumbnail(Media *m, Path *root)
{
    if (m->type() == MediaType::THMB)
    {
        // This is a thumbnail! Skip it!
        return RC::OK;
    }
    Path *img = m->normalizedFile();
    Path *thmb = m->thmbFile();
    Path *source = img->prepend(root);
    Path *target = thmb->prepend(root);

    FileStat original, thumb;
    RC org = fs_->stat(source, &original);
    RC thm = fs_->stat(target, &thumb);

    if (org.isErr()) return RC::ERR; // Oups the original no longer exist!
    if (thm.isOk())
    {
        // The thumbnail already exist, check its timestamp.
        if (original.sameMTime(&thumb))
        {
            // The thumbnail has the same mtime as the original.
            // We assume the thumbnails does not need to be written again.
            verbose(MEDIA, "thumbnail up to date %s\n", target->c_str());
            return RC::OK;
        }
    }
    if (m->type() == MediaType::IMG)
    {
        Magick::Image image;
        try {
            // Read a file into image object
            image.read(source->c_str());
            // Resize the image to specified size (width, height, xOffset, yOffset)
            // Keep aspect ratio.
            string s = image.attribute("EXIF:Orientation");
            if (s != "1")
            {
                if (s == "6")
                {
                    printf("Rot 90\n");
                    image.rotate(90);
                }
                else if (s == "3")
                {
                    printf("Rot 180\n");
                    image.rotate(180);
                }
                else if (s == "8")
                {
                    printf("Rot 270\n");
                    image.rotate(270);
                }
                image.attribute("EXIF:Orientation", "1");
            }
            image.scale( Magick::Geometry(m->thmbWidth(), m->thmbHeight()) );
            fs_->mkDirpWriteable(target->parent());
            image.write(target->c_str());
            fs_->utime(target, &original);
            verbose(MEDIA, "wrote thumbnail %s\n", target->c_str());
        }
        catch( Magick::Exception &error_ )
        {
            warning(MEDIA, "Caught exception: %s\n", error_.what());
            return RC::ERR;
        }
    }
    if (m->type() == MediaType::VID)
    {
        fs_->mkDirpWriteable(target->parent());
        vector<char> output;
        vector<string> args;
        args.push_back("-loglevel");
        args.push_back("fatal");
        args.push_back("-y");
        args.push_back("-i");
        args.push_back(source->str());
        args.push_back("-ss");
        args.push_back("00:00:00.000");
        args.push_back("-vframes");
        args.push_back("1");
        args.push_back("-filter:v");
        args.push_back("scale=128:-1");
        args.push_back(target->str());
        RC rc = sys_->invoke("ffmpeg",
                             args,
                             &output,
                             CaptureBoth);
        if (rc.isOk())
        {
            fprintf(stderr, "VID OK\n");
            fs_->utime(target, &original);
        }
        else
        {
            info(MEDIA, "Could not thumbnail %s\n%.*s\n", source->c_str(), (int)output.size(), &output[0]);
        }

        //ffmpeg -i sdf/2015/08/03/vid_20150803_123154_1920x1080_36492225_1438597914.0_f_abc1bc7322b819140de29524998ff8aea8d4cfb50ed22cb8f5e4437839af9a30.mov -ss 00:00:00.000 -vframes 1 -filter:v scale="128:-1" thumb.jpg
    }


    return RC::OK;
}
