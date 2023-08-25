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

#ifndef MEDIA_H
#define MEDIA_H

#include <string>
#include <vector>
#include <set>

enum class MediaType { Unknown, IMG, VID, AUD, THMB };
enum class DateFoundFrom { EXIF, IPTC, XMP, FFMPEG, PATH, STAT };
enum class Orientation { None, Deg90, Deg180, Deg270 };

bool hasMediaFunctions();

class Media
{
public:
    // Load information from actual media file.
    bool readFile(Path *p, FileStat *st, FileSystem *fs);
    // Load information from normalized file name.
    bool parseFileName(Path *p);

    MediaType type() { return type_; }
    int width() { return width_; }
    int height() { return height_; }
    int thmbWidth() { return thmb_width_; }
    int thmbHeight() { return thmb_height_; }
    Path* normalizedFile();
    FileStat normalizedStat() { return normalized_stat_; }
    Path* sourceFile() { return source_file_; }
    FileStat sourceStat() { return source_stat_; }
    int year() { return tm_.tm_year+1900; }
    int month() { return tm_.tm_mon+1; }
    int day() { return tm_.tm_mday; }
    Path* thmbFile() {  return thmb_file_; }
    std::string ext() { return ext_; }
    std::string yymmdd() { return yymmdd_; }
    Orientation orientation() { return orientation_; }

protected:
    MediaType type_ {};
    struct timespec ts_ {};
    int width_ {};
    int height_ {};
    Orientation orientation_ {};
    size_t size_ {};
    struct tm tm_ {};
    DateFoundFrom date_from_ {};
    std::string metas_;
    std::vector<char> hash_;
    std::string ext_;
    Path *normalized_file_ {};
    FileStat normalized_stat_ {};
    Path *source_file_ {};
    FileStat source_stat_ {};
    Path *thmb_file_ {};
    int thmb_width_ {};
    int thmb_height_ {};
    std::string yymmdd_;

    void calculateThmbSize();
};

class MediaDatabase
{
public:
    void countFile(Path *p, FileStat *st);
    Media *addFile(Path *p, FileStat *st);
    std::string status(const char *tense);
    std::string statusUnknowns();
    std::string brokenFiles();
    std::string inconsistentDates();
    std::string duplicateFiles();
    RC generateThumbnail(Media *m, Path *root);

MediaDatabase(FileSystem *fs, System *sys) : fs_(fs), sys_(sys) {}

protected:
    FileSystem *fs_ {};
    System *sys_ {};
    std::map<Path*,Media> media_files_;

    int num_media_files_ {};
    int num_unknown_files_ {};
    size_t unknown_size_ {};

    std::map<std::string,int> img_suffix_precount_;
    std::map<std::string,int> vid_suffix_precount_;
    std::map<std::string,int> aud_suffix_precount_;

    std::map<std::string,int> img_suffix_count_;
    std::map<std::string,int> vid_suffix_count_;
    std::map<std::string,int> aud_suffix_count_;
    std::map<std::string,int> unknown_suffix_count_;

    std::map<std::string,size_t> img_suffix_size_;
    std::map<std::string,size_t> vid_suffix_size_;
    std::map<std::string,size_t> aud_suffix_size_;
    std::map<std::string,size_t> unknown_suffix_size_;

    // Remember any duplicates here.
    std::map<Path*,int> duplicates_;
    size_t num_duplicates_ {};
    // Remember files where the path 2019/02/03/IMG_123.JPG
    // does not match the exif/itpc/xmp content.
    // Not dangerous, but a warning should be printed.
    std::set<Path*> inconsistent_dates_;
    // Remember media files that could not be decoded.
    std::set<Path*> failed_to_understand_;
};

#endif
