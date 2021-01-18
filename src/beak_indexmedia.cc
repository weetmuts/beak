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

static ComponentId INDEXMEDIA = registerLogComponent("importmedia");

struct IndexMedia
{
    BeakImplementation *beak_ {};
    MediaDatabase db_;
    map<Path*,Media> medias_;
    vector<Path*> sorted_medias_;
    set<int> years_;
    map<int,string> xmq_;
    Settings *settings_ {};
    Monitor *monitor_ {};
    FileSystem *fs_ {};

    int num_ {};

    IndexMedia(BeakImplementation *beak, Settings *settings, Monitor *monitor, FileSystem *fs)
        : beak_(beak), db_(fs), settings_(settings), monitor_(monitor), fs_(fs)
    {
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

        if (!strncmp("thmb_", p->name()->c_str(), 5))
        {
            // This is a thumbnail! Skip it!
            return;
        }

        Media m;
        bool ok = m.parseFileName(p);
        if (!ok) return;

        medias_[m.normalizedFile()] = m;

        UI::clearLine();
        info(INDEXMEDIA, "Indexing %zu media files.", medias_.size());
    }

    void sortFiles()
    {
        for (auto &p : medias_)
        {
            sorted_medias_.push_back(p.first);
            years_.insert(p.second.year());
        }
        sort(sorted_medias_.begin(), sorted_medias_.end(),
          [](Path *a, Path *b)->bool { assert(a); assert(b); return strcmp(a->c_str(), b->c_str()) < 0; });
    }

    void printTodo()
    {
        info(INDEXMEDIA, "Will thumbnail and index %d files.\n", num_);
    }
};

RC BeakImplementation::indexMedia(Settings *settings, Monitor *monitor)
{
    RC rc = RC::OK;

    assert(settings->from.type == ArgOrigin);

    Path *root = settings->from.origin;

    IndexMedia index_media(this, settings, monitor, local_fs_);

    FileStat origin_dir_stat;
    local_fs_->stat(root, &origin_dir_stat);
    if (!origin_dir_stat.isDirectory())
    {
        usageError(INDEXMEDIA, "Not a directory: %s\n", root->c_str());
    }

    info(INDEXMEDIA, "Indexing media inside %s\n", root->c_str());

    local_fs_->recurse(root, [&index_media](Path *p, FileStat *st) {
            index_media.indexFile(p, st);
            return RecurseOption::RecurseContinue;
        });

    UI::clearLine();
    info(INDEXMEDIA, "Indexed %zu media files.\n", index_media.medias_.size());

    unique_ptr<ProgressStatistics> progress = monitor->newProgressStatistics(buildJobName("import", settings));
    progress->startDisplayOfProgress();

    index_media.sortFiles();

    info(INDEXMEDIA, "Generating thumbnails and indexing media...\n");

    for (int year : index_media.years_)
    {
        info(INDEXMEDIA, "%d\n", year);
        for (auto &p : index_media.medias_)
        {
            if (p.second.year() == year)
            {
                index_media.db_.generateThumbnail(&p.second, root);
                string &xmq = index_media.xmq_[year];
                string tmp;
                strprintf(tmp,
                          "        a(href='%s')\n"
                          "        {\n"
                          "            img(src='%s')\n"
                          "        }\n",
                          p.second.normalizedFile()->c_str()+1,
                          p.second.thmbFile()->c_str());
                xmq += tmp;
            }
        }
    }

    string top_xmq =
        "html {\n"
        "    head { link(rel=stylesheet href=style.css) }\n"
        "    body {\n";

    for (int year : index_media.years_)
    {
        string tmp;
        strprintf(tmp,
                  "    a(href=index_%d.html) = %d\n"
                  "    br\n", year, year);
        top_xmq += tmp;

        tmp =
            "html {\n"
            "    head { link(rel=stylesheet href=style.css) }\n"
            "    body {\n"+
            index_media.xmq_[year]+
            "    }\n"+
            "}\n";

        string filename;
        strprintf(filename, "index_%d.xmq", year);
        Path *index_xmq = root->append(filename);
        strprintf(filename, "index_%d.html", year);
        Path *index_html = root->append(filename);
        vector<char> content(tmp.begin(), tmp.end());
        local_fs_->createFile(index_xmq, &content);

        vector<char> output;
        vector<string> args;
        args.push_back("--nopp");
        args.push_back(index_xmq->str());
        RC rc = sys_->invoke("xmq",
                       args,
                       &output);
        if (rc.isOk())
        {
            local_fs_->createFile(index_html, &output);
        }
    }

    top_xmq +=
        "    }\n"
        "}\n";
    vector<char> topp(top_xmq.begin(), top_xmq.end());
    Path *index_xmq = root->append("index.xmq");
    local_fs_->createFile(index_xmq, &topp);

    Path *index_html = root->append("index.html");

    vector<char> output;
    vector<string> args;
    args.push_back("--nopp");
    args.push_back(index_xmq->str());
    rc = sys_->invoke("xmq",
                         args,
                         &output);
    if (rc.isOk())
    {
        local_fs_->createFile(index_html, &output);
    }

    string css =
        "img {\n"
        "vertial-align: top;\n"
        "}\n"
        "body {\n"
        "background: black;\n"
        "}\n"
        "a, a:link, a:visited, a:hover, a:active {\n"
        "color:white;\n"
        "}\n";
    vector<char> csss(css.begin(), css.end());

    Path *style = root->append("style.css");
    local_fs_->createFile(style, &csss);

    return rc;
}
