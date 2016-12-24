/*  
    Copyright (C) 2016 Fredrik Öhrström

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

#define TARREDFS_VERSION "0.1"

#define FUSE_USE_VERSION 26
#define _XOPEN_SOURCE 500

#include<assert.h>


#include"log.h"
#include"tarfile.h"
#include"tarentry.h"
#include"util.h"
#include"libtar.h"

#include<errno.h>

#include<fcntl.h>
#include<ftw.h>
#include<fuse.h>

#include<limits.h>

#include<pthread.h>

#include<regex.h>

#include<stddef.h>
#include<stdint.h>
#include<stdio.h>
#include<stdlib.h>
#include<string.h>
#include<syslog.h>

#include<time.h>
#include<sys/timeb.h>

#include<unistd.h>

#include<algorithm>
#include<functional>
#include<map>
#include<set>
#include<string>
#include<sstream>
#include<vector>
#include<set>

#include<iostream>

#include"forward.h"
#include"reverse.h"

using namespace std;

void printForwardHelp(const char *app) {
    fprintf(stdout,
            "usage: %s [options] [rootDirectory] [mountPoint]\n"
            "\n"
            "general options:\n"
            "    -h   --help      print help\n"
            "    -V   --version   print version\n"
            "    -f               foreground, ie do not start daemon\n"
            "    -v   --verbose   detailed output\n"
            "    -q               quite\n"
            "    -p [num]         force all directories at depth [num] to be chunk points\n"
            "    -i pattern       exlude these files from \n"
            "    -x pattern       exlude these files from \n"
            "\n"
            , app);
}

void printReverseHelp(const char *app) {
    fprintf(stdout,
            "usage: %s --reverse [options] [rootDirectory] [mountPoint]\n"
            "\n"
            "general options:\n"
            "    -h   --help      print help\n"
            "    -f               foreground, ie do not start daemon\n"
            "    -v   --verbose   detailed output\n"
            "    -q               quite\n"
            "\n"
            , app);
}

void parseForwardOptions(int *argc, char **argv, TarredFS *tfs, struct fuse_operations *tarredfs_ops)
{    
    for (int i=1; argv[i] != 0; ++i) {
        if (!strncmp(argv[i], "-h", 2)) {
            printForwardHelp(argv[0]);
            exit(1);
        } else
        if (!strncmp(argv[i], "-d", 2)) {
            setLogLevel(DEBUG);
        } else
        if (!strncmp(argv[i], "-v", 2)) {
            setLogLevel(VERBOSE);
            eraseArg(i, argc, argv);
            i--;
        } else
        if (!strncmp(argv[i], "-q", 2)) {
            setLogLevel(QUITE);
        } else
        if (!strncmp(argv[i], "-V", 2)  || !strncmp(argv[i], "--version", 9)) {
            fprintf(stdout,"Tarredfs version " TARREDFS_VERSION "\n");
            char *argvs[3];
            argvs[0] = argv[0];
            char *vv = (char*)malloc(strlen("--version"+1));
            strcpy(vv, "--version");
            argvs[1] = vv;
            argvs[2] = 0;
            fuse_main(2, argvs, tarredfs_ops, NULL);
            exit(1);
        } else
        if (!strncmp(argv[i], "-i", 2)) {            
            regex_t re;
            if (regcomp(&re, argv[i+1], REG_EXTENDED|REG_NOSUB) != 0) {
                fprintf(stderr, "Not a valid regexp \"%s\"\n", argv[i+1]);
            }
            tfs->filters.push_back(pair<Filter,regex_t>(Filter(argv[i+1],INCLUDE),re));
            debug("Includes \"%s\"\n", argv[i+1]);
            eraseArg(i, argc, argv);
            eraseArg(i, argc, argv);
            i--;            
        } else
        if (!strncmp(argv[i], "-x", 2)) {            
            regex_t re;
            if (regcomp(&re, argv[i+1], REG_EXTENDED|REG_NOSUB) != 0) {
                fprintf(stderr, "Not a valid regexp \"%s\"\n", argv[i+1]);
            }
            tfs->filters.push_back(pair<Filter,regex_t>(Filter(argv[i+1],EXCLUDE),re));
            debug("Excludes \"%s\"\n", argv[i+1]);
            eraseArg(i, argc, argv);
            eraseArg(i, argc, argv);
            i--;
        } else
        if (!strncmp(argv[i], "-mn", 3)) {
            tfs->target_min_tar_size = tfs->tar_trigger_size = atol(argv[i+1]);             
            debug("Target tar min size and trigger size \"%zu\"\n", tfs->target_min_tar_size);
            eraseArg(i, argc, argv);
            eraseArg(i, argc, argv);
            i--;
        } else
        if (!strncmp(argv[i], "-tr", 3)) {
            tfs->tar_trigger_size = atol(argv[i+1]);             
            debug("Tar trigger size \"%zu\"\n", tfs->tar_trigger_size);
            eraseArg(i, argc, argv);
            eraseArg(i, argc, argv);
            i--;
        } else
        if (!strncmp(argv[i], "-p", 2)) {
            tfs->forced_chunk_depth = atol(argv[i+1]);
            if (tfs->forced_chunk_depth < 0) {
                error("Cannot set forced chunk depth to a negative number.");
            }
            debug("Forced chunks at depth \"%jd\"\n", tfs->forced_chunk_depth);
            eraseArg(i, argc, argv);
            eraseArg(i, argc, argv);
            i--;
        } else
        if (argv[i][0] != '-') {
            if (tfs->root_dir.empty()) {
                char tmp[PATH_MAX];
                const char *rc = realpath(argv[i], tmp);
                if (!rc) {
                    error("Could not find real path for %s\n", argv[i]);
                }                                               
                assert(rc == tmp);
                tfs->root_dir = tmp;
                // Erase the rootDir from argv.
                eraseArg(i, argc, argv);
                i--;
            } else 
            if (tfs->mount_dir.empty()) {
                char tmp[PATH_MAX];
                const char *rc = realpath(argv[i], tmp);
                if (!rc) {
                    error("Could not find real path for %s\nExisting mount here?\n", argv[i]);
                }                                               
                tfs->mount_dir = tmp;
            }
        }
    }
}


void parseReverseOptions(int *argc, char **argv, ReverseTarredFS *tfs, struct fuse_operations *tarredfs_ops)
{    
    for (int i=1; argv[i] != 0; ++i) {
        if (!strncmp(argv[i], "-h", 2)) {
            printReverseHelp(argv[0]);
            exit(1);
        } else
        if (!strncmp(argv[i], "-d", 2)) {
            setLogLevel(DEBUG);
        } else
        if (!strncmp(argv[i], "-v", 2)) {
            setLogLevel(VERBOSE);
            eraseArg(i, argc, argv);
            i--;
        } else
        if (!strncmp(argv[i], "-q", 2)) {
            setLogLevel(QUITE);
        } else

        if (argv[i][0] != '-') {
            if (tfs->root_dir.empty()) {
                char tmp[PATH_MAX];
                const char *rc = realpath(argv[i], tmp);
                if (!rc) {
                    error("Could not find real path for %s\n", argv[i]);
                }                                               
                assert(rc == tmp);
                tfs->root_dir = tmp;
                // Erase the rootDir from argv.
                eraseArg(i, argc, argv);
                i--;
            } else 
            if (tfs->mount_dir.empty()) {
                char tmp[PATH_MAX];
                const char *rc = realpath(argv[i], tmp);
                if (!rc) {
                    error("Could not find real path for %s\nExisting mount here?\n", argv[i]);
                }                                               
                tfs->mount_dir = tmp;
            }
        }
    }
}


template<TarredFS*fs> struct AddTarEntry {
    static int add(const char *fpath, const struct stat *sb, int tflag, struct FTW *ftwbuf) {
        return fs->addTarEntry(fpath, sb, ftwbuf);
    }
};

template<TarredFS*fs> struct ForwardGetattr {
    static int cb(const char *path, struct stat *stbuf) {
        return fs->getattrCB(path, stbuf);
    }
};

template<TarredFS*fs> struct ForwardReaddir {
    static int cb(const char *path, void *buf, fuse_fill_dir_t filler,
           off_t offset, struct fuse_file_info *fi) {
        return fs->readdirCB(path, buf, filler, offset, fi);
    }
};

template<TarredFS*fs> struct ForwardRead {
    static int cb(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
        return fs->readCB(path, buf, size, offset, fi);
    }
};

static TarredFS forward_fs;
static struct fuse_operations forward_tarredfs_ops;

int reversemain(int argc, char *argv[]);
int open_callback(const char *path, struct fuse_file_info *fi)
{
    return 0;
}

int main(int argc, char *argv[])
{
    if (argc > 1 && !strncmp(argv[1], "--reverse", 9)) {
        eraseArg(1, &argc, argv);
        int rc = reversemain(argc, argv);
        exit(rc);
    }

    parseForwardOptions(&argc, argv, &forward_fs, &forward_tarredfs_ops);

    if (forward_fs.root_dir.empty() || forward_fs.mount_dir.empty()) {
        printForwardHelp(argv[0]);
        exit(1);
    }

    forward_tarredfs_ops.getattr = ForwardGetattr<&forward_fs>::cb;
    forward_tarredfs_ops.open = open_callback;
    forward_tarredfs_ops.read = ForwardRead<&forward_fs>::cb;
    forward_tarredfs_ops.readdir = ForwardReaddir<&forward_fs>::cb;
    
    info("Scanning %s\n", forward_fs.root_dir.c_str());
    AddTarEntry<&forward_fs> ate;
    uint64_t start = clockGetTime();
    forward_fs.recurse(ate.add);
    uint64_t stop = clockGetTime();
    uint64_t scan_time = stop-start;
    start = stop;    
    // Find suitable chunk points where tars will be created.
    forward_fs.findChunkPoints();
    // Remove all other directories that will be hidden inside tars.
    forward_fs.pruneDirectories();
    // Add remaining chunk point as dir entries to their parent directories.
    forward_fs.addDirsToDirectories();
    // Add content (files and directories) to the chunk points.
    forward_fs.addEntriesToChunkPoints();
    // Sort the entries in a tar friendly order.
    forward_fs.sortChunkPointEntries();
    // Group the entries into tar files.
    size_t num_tars = forward_fs.groupFilesIntoTars();
    stop = clockGetTime();
    uint64_t group_time = stop-start;
    
    info("Mounted %s with %zu virtual tars with %zu entries, scan %jdms group %jdms.\n",
         forward_fs.mount_dir.c_str(), num_tars, forward_fs.files.size(),
         scan_time/1000, group_time/1000);

    return fuse_main(argc, argv, &forward_tarredfs_ops, NULL);
}

template<ReverseTarredFS*fs> struct ReverseGetattr {
    static int cb(const char *path, struct stat *stbuf) {
        return fs->getattrCB(path, stbuf);
    }
};

template<ReverseTarredFS*fs> struct ReverseReaddir {
    static int cb(const char *path, void *buf, fuse_fill_dir_t filler,
           off_t offset, struct fuse_file_info *fi) {
        return fs->readdirCB(path, buf, filler, offset, fi);
    }
};

template<ReverseTarredFS*fs> struct ReverseRead {
    static int cb(const char *path, char *buf, size_t size, off_t offset, struct fuse_file_info *fi) {
        return fs->readCB(path, buf, size, offset, fi);
    }
};

template<ReverseTarredFS*fs> struct ReverseReadlink {
    static int cb(const char *path, char *buf, size_t size) {
        return fs->readlinkCB(path, buf, size);
    }
};

static ReverseTarredFS reverse_fs;
static struct fuse_operations reverse_tarredfs_ops;

int reversemain(int argc, char *argv[])
{
    parseReverseOptions(&argc, argv, &reverse_fs, &reverse_tarredfs_ops);

    reverse_tarredfs_ops.getattr = ReverseGetattr<&reverse_fs>::cb;
    reverse_tarredfs_ops.open = open_callback;
    reverse_tarredfs_ops.read = ReverseRead<&reverse_fs>::cb;
    reverse_tarredfs_ops.readdir = ReverseReaddir<&reverse_fs>::cb;
    reverse_tarredfs_ops.readlink = ReverseReadlink<&reverse_fs>::cb;

    if (reverse_fs.root_dir.empty() || reverse_fs.mount_dir.empty()) {
        printReverseHelp(argv[0]);
        exit(1);
    }

    // Check that there is a taz00000000.tar file in the root dir.
    struct stat sb;
    string taz = reverse_fs.root_dir+"/taz00000000.tar";
    int rc = stat(taz.c_str(), &sb);
    if (rc || !S_ISREG(sb.st_mode)) {
        // Not a tarredfs filesystem!
        error("Not a tarredfs filesystem! "
              "Could not find a taz00000000.tar file in the root directory!\n");
    }

    reverse_fs.loadCache("/");
    Entry &e = reverse_fs.entries["/"];
    time_t s=0, n=0;
    
    for (auto i : e.dir) {
        if (i->secs > s || (i->secs == s && i->nanos > n)) {
            s = i->secs;
            n = i->nanos;
        }
    }
    e.secs = s;
    e.nanos = n;
    
    return fuse_main(argc, argv, &reverse_tarredfs_ops, NULL);
}

