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

#include<assert.h>

#include"defs.h"
#include"log.h"
#include"forward.h"
#include"reverse.h"

#include<iostream>
#include<limits.h>
#include<string.h>

using namespace std;

ComponentId MAIN = registerLogComponent("main");

void printHelp(const char *app) {
    fprintf(stdout,
            "usage: %s {-r|--reverse} [options] [rootDirectory] [mountPoint]\n"
            "\n"
            "general options:\n"
            "  -h   --help      print help\n"
            "  -V   --version   print version\n"
            "  -i pattern       only paths matching regex pattern are included\n"
            "  -x pattern       paths matching regex pattern are excluded\n"
            "  -s [num],[rps]   set -ta and -tr automatically based on you upload bandwidth\n"
            "                     num bytes per second and rps requests per second\n"       
            "  -p [num]         force all directories at depth [num] to contain tars\n"
            "                   0 is the root, 1 is the first subdirectory level\n"
            "                   the default is 1.\n"
            "  -ta [num]        set the target size of the virtual tars to [num],\n"
            "                   default 10M\n"
            "  -tr [num]        trigger tar generation in a dir, when size of dir and its subdirs\n"
            "                   exceeds [num], default 20M\n"
            "  -tx pattern      trigger tar generation if the path matches the regex pattern\n"   
            "  -ln              create hard links between files with identical name, size\n"
            "                   and nano second mtime. Must be replaced with reflinks or\n"
            "                   otherwise dereferenced in a post processing step when untaring.\n"
            "  -f               foreground, ie do not start daemon\n"
            "  -r   --reverse   mount a tarredfs directory and present the original files\n"
            "  -v   --verbose   detailed output, use twice for more\n"
            "  -l [a][,b][,c]   enable verbose logging for the listed components\n"
            "  -ll              list available logging components\n"
            "  -d   --fuse-debug enable fuse debug output, this trigger foreground mode\n"
            "  -q   --quiet     quite mode\n"
            "\n"
            , app);
}

void parseForwardOptions(int *argc, char **argv, TarredFS *tfs, struct fuse_operations *tarredfs_ops)
{    
    for (int i=1; argv[i] != 0; ++i) {
        if (!strcmp(argv[i], "-h")) {
            printHelp(argv[0]);
            exit(1);
        } else
        if (!strcmp(argv[i], "-ll")) {
        	listLogComponents();
        	exit(1);
        } else
        if (!strcmp(argv[i], "-l")) {
        	setLogComponents(argv[i+1]);
            eraseArg(i, argc, argv);
            eraseArg(i, argc, argv);
            i--;
        } else
        if (!strcmp(argv[i], "-d") || !strcmp(argv[i], "--fuse-debug")) {
            setLogLevel(DEBUG);
        } else
        if (!strcmp(argv[i], "-v")) {
            setLogLevel(DEBUG);
            eraseArg(i, argc, argv);
            i--;
        } else
        if (!strcmp(argv[i], "-q")) {
            setLogLevel(QUITE);
        } else
        if (!strcmp(argv[i], "-V")  || !strcmp(argv[i], "--version")) {
            fprintf(stdout,"Tarredfs version " TARREDFS_VERSION "\n");
            fprintf(stdout,"libtar version 1.2.20 (with additional fixes)\n");
            char *argvs[3];
            argvs[0] = argv[0];
            char *vv = (char*)malloc(strlen("--version")+1);
            strcpy(vv, "--version");
            argvs[1] = vv;
            argvs[2] = 0;
            fuse_main(2, argvs, tarredfs_ops, NULL);
            exit(1);
        } else
        if (!strcmp(argv[i], "-i") || !strcmp(argv[i], "--include")) {            
            regex_t re;
            if (regcomp(&re, argv[i+1], REG_EXTENDED|REG_NOSUB) != 0) {
                fprintf(stderr, "Not a valid regexp \"%s\"\n", argv[i+1]);
            }
            tfs->filters.push_back(pair<Filter,regex_t>(Filter(argv[i+1],INCLUDE),re));
            debug(MAIN, "Includes \"%s\"\n", argv[i+1]);
            eraseArg(i, argc, argv);
            eraseArg(i, argc, argv);
            i--;            
        } else
        if (!strcmp(argv[i], "-x")) {            
            regex_t re;
            if (regcomp(&re, argv[i+1], REG_EXTENDED|REG_NOSUB) != 0) {
                fprintf(stderr, "Not a valid regexp \"%s\"\n", argv[i+1]);
            }
            tfs->filters.push_back(pair<Filter,regex_t>(Filter(argv[i+1],EXCLUDE),re));
            debug(MAIN, "Excludes \"%s\"\n", argv[i+1]);
            eraseArg(i, argc, argv);
            eraseArg(i, argc, argv);
            i--;
        } else
        if (!strcmp(argv[i], "-ta")) {
            size_t s;
            int rc = parseHumanReadable(argv[i+1], &s);
            if (rc) {
                error(MAIN,"Cannot set -ta because \"%s\" is not a proper number (e.g. 1,2K,3M,4G,5T)\n", argv[i+1]);
            }
            tfs->target_target_tar_size = s;
            tfs->tar_trigger_size = s*2;
            debug(MAIN, "main", "Target tar min size and trigger size \"%zu\"\n", tfs->target_target_tar_size);
            eraseArg(i, argc, argv);
            eraseArg(i, argc, argv);
            i--;
        } else
        if (!strcmp(argv[i], "-tr")) {
            size_t s;
            int rc = parseHumanReadable(argv[i+1], &s);
            if (rc) {
                error(MAIN,"Cannot set -tr because \"%s\" is not a proper number (e.g. 1,2K,3M,4G,5T)\n", argv[i+1]);
            }
            tfs->tar_trigger_size = s;
            debug(MAIN, "main", "Tar trigger size \"%zu\"\n", tfs->tar_trigger_size);
            eraseArg(i, argc, argv);
            eraseArg(i, argc, argv);
            i--;
        } else
        if (!strcmp(argv[i], "-tx")) {            
            regex_t re;
            if (regcomp(&re, argv[i+1], REG_EXTENDED|REG_NOSUB) != 0) {
                fprintf(stderr, "Not a valid regexp \"%s\"\n", argv[i+1]);
            }
            tfs->triggers.push_back(re);
            debug(MAIN, "Triggers on \"%s\"\n", argv[i+1]);
            eraseArg(i, argc, argv);
            eraseArg(i, argc, argv);
            i--;
        } else
        if (!strcmp(argv[i], "-p")) {
            tfs->forced_tar_collection_dir_depth = atol(argv[i+1]);
            if (tfs->forced_tar_collection_dir_depth < 0) {
                error(MAIN,"main", "Cannot set forced tar collection depth to a negative number.");
            }
            debug(MAIN, "main", "Forced tar collection at depth \"%jd\"\n", tfs->forced_tar_collection_dir_depth);
            eraseArg(i, argc, argv);
            eraseArg(i, argc, argv);
            i--;
        } else
        if (!strcmp(argv[i], "-s") && argv[i+1] != NULL) {
            string arg = argv[i+1];
            size_t bw,rqs;
            std::vector<char> data(arg.begin(), arg.end());
            auto j = data.begin();
            string bws = eatTo(data, j, ',', 64);
            string rqss = eatTo(data, j, 0, 64);
            int rc1 = parseHumanReadable(bws, &bw);
            int rc2 = parseHumanReadable(rqss, &rqs);
            if (rc1) {
                error(MAIN,"Cannot set -s bandwidth because \"%s\" is not a proper number (e.g. 1,2K,3M,4G,5T)\n", bws.c_str());
            }
            if (rc2) {
                error(MAIN,"Cannot set -s number of requests per second because \"%s\" is not a proper number\n", rqss.c_str());
            }
            tfs->target_target_tar_size = roundoffHumanReadable(bw/(rqs*10));
            tfs->tar_trigger_size = tfs->target_target_tar_size*2;
            string ta = humanReadable(tfs->target_target_tar_size);
            string tr = humanReadable(tfs->tar_trigger_size);
            info(MAIN, "Calculated target tar size %sB and trigger size %sB from %s requests per second over %sbit/s .\n", ta.c_str(), tr.c_str(),
                 rqss.c_str(), bws.c_str());
            eraseArg(i, argc, argv);
            eraseArg(i, argc, argv);
            i--;
        } else
        if (argv[i][0] != '-') {
            if (tfs->root_dir.empty()) {
                char tmp[PATH_MAX];
                const char *rc = realpath(argv[i], tmp);
                if (!rc) {
                    error(MAIN,"Could not find real path for %s\n", argv[i]);
                }                                               
                assert(rc == tmp);
                tfs->root_dir = tmp;
                tfs->root_dir_path = Path::lookup(tmp);
                // Erase the rootDir from argv.
                eraseArg(i, argc, argv);
                i--;
            } else 
            if (tfs->mount_dir.empty()) {
                char tmp[PATH_MAX];
                const char *rc = realpath(argv[i], tmp);
                if (!rc) {
                    error(MAIN,"Could not find real path for %s\nExisting mount here?\n", argv[i]);
                }                                               
                tfs->mount_dir = tmp;
                tfs->mount_dir_path = Path::lookup(tmp);
            }
        }
    }
}


void parseReverseOptions(int *argc, char **argv, ReverseTarredFS *tfs, struct fuse_operations *tarredfs_ops)
{    
    for (int i=1; argv[i] != 0; ++i) {
        if (!strncmp(argv[i], "-h", 2)) {
            printHelp(argv[0]);
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
                    error(MAIN,"Could not find real path for %s\n", argv[i]);
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
                    error(MAIN,"Could not find real path for %s\nExisting mount here?\n", argv[i]);
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

    if (argc > 1 && (!strcmp(argv[1], "--reverse") || !strcmp(argv[1], "-r"))) {
        eraseArg(1, &argc, argv);
        int rc = reversemain(argc, argv);
        exit(rc);
    }

    parseForwardOptions(&argc, argv, &forward_fs, &forward_tarredfs_ops);

    if (forward_fs.root_dir.empty() || forward_fs.mount_dir.empty()) {
        printHelp(argv[0]);
        exit(1);
    }

    forward_tarredfs_ops.getattr = ForwardGetattr<&forward_fs>::cb;
    forward_tarredfs_ops.open = open_callback;
    forward_tarredfs_ops.read = ForwardRead<&forward_fs>::cb;
    forward_tarredfs_ops.readdir = ForwardReaddir<&forward_fs>::cb;
    
    info(MAIN, "Scanning %s\n", forward_fs.root_dir.c_str());
    AddTarEntry<&forward_fs> ate;
    uint64_t start = clockGetTime();
    forward_fs.recurse(ate.add);
    uint64_t stop = clockGetTime();
    uint64_t scan_time = stop-start;
    start = stop;    

    // Find suitable directories points where virtual tars will be created.
    forward_fs.findTarCollectionDirs();
    // Remove all other directories that will be hidden inside tars.
    forward_fs.pruneDirectories();
    // Add remaining dirs as dir entries to their parent directories.
    forward_fs.addDirsToDirectories();
    // Add content (files and directories) to the tar collection dirs.
    forward_fs.addEntriesToTarCollectionDirs();
    // Calculate the tarpaths and fix/move the hardlinks.
    forward_fs.fixTarPathsAndHardLinks();
    // Group the entries into tar files.
    size_t num_tars = forward_fs.groupFilesIntoTars();
    // Sort the entries in a tar friendly order.
    forward_fs.sortTarCollectionEntries();
    
    stop = clockGetTime();
    uint64_t group_time = stop-start;
    
    info(MAIN, "Mounted %s with %zu virtual tars with %zu entries.\n"
    		"Time to scan %jdms, time to group %jdms.\n",
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
        printHelp(argv[0]);
        exit(1);
    }

    // Check that there is a taz00000000.tar file in the root dir.
    struct stat sb;
    string taz = reverse_fs.root_dir+"/taz00000000.tar";
    int rc = stat(taz.c_str(), &sb);
    if (rc || !S_ISREG(sb.st_mode)) {
        // Not a tarredfs filesystem!
        error(MAIN,"main", "Not a tarredfs filesystem! "
              "Could not find a taz00000000.tar file in the root directory!\n");
    }

    
    reverse_fs.loadCache(Path::lookupRoot());
    Entry &e = reverse_fs.entries[Path::lookupRoot()];
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

