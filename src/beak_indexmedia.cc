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

static ComponentId INDEXMEDIA = registerLogComponent("importmedia");

struct IndexMedia
{
    BeakImplementation *beak_ {};

    Settings *settings_ {};
    Monitor *monitor_ {};
    FileSystem *fs_ {};

    int num_ {};
    vector<Path*> index_files_;

    map<string,string> img_suffixes_;
    map<string,string> vid_suffixes_;
    map<string,string> aud_suffixes_;

    string xmq_;

    IndexMedia(BeakImplementation *beak, Settings *settings, Monitor *monitor, FileSystem *fs)
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
        string info = "gurka";
        string st;
        strprintf(st, "Index%s %d %s", tense, num_, info.c_str());
        return st;
    }

    void indexFile(Path *p, FileStat *st)
    {
        if (st->isDirectory()) return;

        assert(p != NULL);
        index_files_.push_back(p);

        if (num_ % 100 == 0)
        {
            UI::clearLine();
            string st = status("ing");
            info(INDEXMEDIA, "%s\n", st.c_str());
        }
        num_++;

    }

    void sortFiles()
    {
        sort(index_files_.begin(), index_files_.end(),
             [](Path *a, Path *b)->bool { assert(a); assert(b); return strcmp(a->c_str(), b->c_str()) < 0; });
    }

    void printTodo()
    {
        info(INDEXMEDIA, "Will thumbnail and index %d files.\n", num_);
    }

    RC generateThumbnail(Path *root, Path *img)
    {
        string ext = img->name()->ext_c_str_();
        if (img_suffixes_.count(ext) == 0) return RC::OK;

        // Is this a thumbnail?
        if (!strncmp("thmb_", img->name()->c_str(), 5))
        {
            // This is a thumbnail! Skip it!
            return RC::OK;
        }

        Path *prefix = img->parent();
        prefix = prefix->subpath(root->depth());
        Path *target = Path::lookup(root->str()+"/thumbnails/"+prefix->str()+"/"+string("thmb_")+img->name()->str());

        FileStat original, thumb;
        RC org = fs_->stat(img, &original);
        RC thm = fs_->stat(target, &thumb);

        string imglink;
        strprintf(imglink, "a(href=%s){ img(src=%s) }\n", img->subpath(root->depth())->c_str(), target->subpath(root->depth())->c_str());
        xmq_ += imglink;

        if (org.isErr()) return RC::ERR; // Oups the original no longer exist!
        if (thm.isOk())
        {
            // The thumbnail already exist, check its timestamp.
            if (original.sameMTime(&thumb))
            {
                // The thumbnail has the same mtime as the original.
                // We assume the thumbnails does not need to be written again.
                verbose(INDEXMEDIA, "thumbnail up to date %s\n", target->c_str());
                return RC::OK;
            }
        }
        Magick::Image image;
        try {
            // Read a file into image object
            image.read(img->c_str());
            // Crop the image to specified size (width, height, xOffset, yOffset)
            image.resize( Magick::Geometry(256, 128, 0, 0) );
            fs_->mkDirpWriteable(target->parent());
            image.write(target->c_str());
            fs_->utime(target, &original);
            verbose(INDEXMEDIA, "wrote thumbnail %s\n", target->c_str());
        }
        catch( Magick::Exception &error_ )
        {
            cout << "Caught exception: " << error_.what() << endl;
            return RC::ERR;
        }
        return RC::OK;
    }
};

RC BeakImplementation::indexMedia(Settings *settings, Monitor *monitor)
{
    RC rc = RC::OK;

    assert(settings->from.type == ArgOrigin);

    Path *src = settings->from.origin;

    IndexMedia index_media(this, settings, monitor, local_fs_);

    FileStat origin_dir_stat;
    local_fs_->stat(src, &origin_dir_stat);
    if (!origin_dir_stat.isDirectory())
    {
        usageError(INDEXMEDIA, "Not a directory: %s\n", src->c_str());
    }

    info(INDEXMEDIA, "Indexing media inside %s\n", src->c_str());

    local_fs_->recurse(src, [&index_media](Path *p, FileStat *st) {
            index_media.indexFile(p, st);
            return RecurseOption::RecurseContinue;
        });

    UI::clearLine();
    string st = index_media.status("ed");
    info(INDEXMEDIA, "%s\n", st.c_str());

    index_media.printTodo();

    unique_ptr<ProgressStatistics> progress = monitor->newProgressStatistics(buildJobName("import", settings));
    progress->startDisplayOfProgress();

    index_media.sortFiles();

    info(INDEXMEDIA, "Generating thumbnails and indexing media... %d\n", index_media.index_files_.size());

    for (auto p : index_media.index_files_)
    {
        index_media.generateThumbnail(src, p);
    }
    /*
    local_fs_->recurse(src, [&index_media,settings,src](Path *p, FileStat *st) {
            if (st->isRegularFile())
            {
                index_media.generateThumbnail(src, p);
            }
            return RecurseOption::RecurseContinue;
            });*/
    info(INDEXMEDIA, "done\n");

    index_media.xmq_ = "html { body(bgcolor=black) {" + index_media.xmq_ + "} }";

    fprintf(stderr, "%s", index_media.xmq_.c_str());
    return rc;
}
