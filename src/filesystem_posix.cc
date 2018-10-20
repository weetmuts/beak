/*
 Copyright (C) 2017-2018 Fredrik Öhrström

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

#include "filesystem.h"

#include "log.h"

#include <assert.h>
#include <dirent.h>
#include <fcntl.h>
#include <ftw.h>
#include <grp.h>
#include <linux/kdev_t.h>
#include <pwd.h>
#include <sys/inotify.h>
#include <sys/ioctl.h>
#include <sys/types.h>
#include <unistd.h>

using namespace std;

static ComponentId FILESYSTEM = registerLogComponent("filesystem");
static ComponentId WATCH = registerLogComponent("watch");

bool FileStat::isRegularFile() { return S_ISREG(st_mode); }
bool FileStat::isDirectory() { return S_ISDIR(st_mode); }
void FileStat::setAsRegularFile() { st_mode |= S_IFREG; }
void FileStat::setAsDirectory() { st_mode |= S_IFDIR; }
void FileStat::setAsExecutable() { st_mode |= S_IXUSR | S_IRUSR | S_IWUSR; }

bool FileStat::isSymbolicLink() { return S_ISLNK(st_mode); }
bool FileStat::isCharacterDevice() { return S_ISCHR(st_mode); }
bool FileStat::isBlockDevice() { return S_ISBLK(st_mode); }
bool FileStat::isFIFO() { return S_ISFIFO(st_mode); }
bool FileStat::isSOCK() { return S_ISSOCK(st_mode); }
bool FileStat::isISUID() { return st_mode & S_ISUID; } // set uid
bool FileStat::isISGID() { return st_mode & S_ISGID; } // set gid
bool FileStat::isISVTX() { return st_mode & S_ISVTX; } // sticky

bool FileStat::isIRUSR() { return st_mode & S_IRUSR; }
bool FileStat::isIWUSR() { return st_mode & S_IWUSR; }
bool FileStat::isIXUSR() { return st_mode & S_IXUSR; }

bool FileStat::isIRGRP() { return st_mode & S_IRGRP; }
bool FileStat::isIWGRP() { return st_mode & S_IWGRP; }
bool FileStat::isIXGRP() { return st_mode & S_IXGRP; }

bool FileStat::isIROTH() { return st_mode & S_IROTH; }
bool FileStat::isIWOTH() { return st_mode & S_IWOTH; }
bool FileStat::isIXOTH() { return st_mode & S_IXOTH; }

void FileStat::setIWUSR() { st_mode |= S_IWUSR; }

string FileStat::uidName()
{
    struct passwd pwd, *res;
    char buf[1024];
    int err = getpwuid_r(st_uid, &pwd, buf, 1024, &res);
    if (!err && res) {
        return string(res->pw_name);
    }
    return to_string(st_uid);
}

string FileStat::gidName()
{
    struct group grp, *res;
    char buf[1024];
    int err = getgrgid_r(st_gid, &grp, buf, 1024, &res);
    if (!err && res) {
        return res->gr_name;
    }
    return to_string(st_gid);
}

struct FileSystemImplementationPosix : FileSystem
{
    bool readdir(Path *p, vector<Path*> *vec);
    ssize_t pread(Path *p, char *buf, size_t count, off_t offset);
    RC recurse(Path *p, function<RecurseOption(Path *path, FileStat *stat)> cb);
    RC recurse(Path *p, function<RecurseOption(const char *path, const struct stat *sb)> cb);
    RC listFilesBelow(Path *p, std::vector<Path*> *files, SortOrder so);
    RC ctimeTouch(Path *file);
    RC stat(Path *p, FileStat *fs);
    RC chmod(Path *p, FileStat *stat);
    RC utime(Path *p, FileStat *stat);
    Path *mkTempFile(string prefix, string content);
    Path *mkTempDir(string prefix);
    Path *mkDir(Path *p, string name);
    RC rmDir(Path *p);
    RC loadVector(Path *file, size_t blocksize, std::vector<char> *buf);
    RC createFile(Path *file, std::vector<char> *buf);
    bool createFile(Path *path, FileStat *stat,
                     std::function<size_t(off_t offset, char *buffer, size_t len)> cb);
    bool createSymbolicLink(Path *path, FileStat *stat, string target);
    bool createHardLink(Path *path, FileStat *stat, Path *target);
    bool createFIFO(Path *path, FileStat *stat);
    bool readLink(Path *path, string *target);
    bool deleteFile(Path *file);

    RC enableWatch();
    RC addWatch(Path *dir);
    int endWatch();

    FileSystemImplementationPosix() : FileSystem("FileSystemImplementationPosix") {}

private:

    int inotify_fd_ {};
};

FileSystem *default_file_system_ {};

Path *cache_dir_ {};

Path *configuration_file_ {};

Path *initCacheDir_()
{
    Path *home = Path::lookup(getenv("HOME"));
    return home->append(".cache/beak");
}

Path *initConfigurationFile_()
{
    Path *home = Path::lookup(getenv("HOME"));
    return home->append(".config/beak/beak.conf");
}

unique_ptr<FileSystem> newDefaultFileSystem()
{
    if (!cache_dir_) {
        cache_dir_ = initCacheDir_();
    }
    if (!configuration_file_) {
        configuration_file_ = initConfigurationFile_();
    }
    default_file_system_ = new FileSystemImplementationPosix();
    return unique_ptr<FileSystem>(default_file_system_);
}

bool FileSystemImplementationPosix::readdir(Path *p, vector<Path*> *vec)
{
    DIR *dp = NULL;
    struct dirent *dptr = NULL;

    if (NULL == (dp = opendir(p->c_str())))
    {
        return false;
    }
    while(NULL != (dptr = ::readdir(dp)))
    {
        vec->push_back(Path::lookup(dptr->d_name));
    }
    closedir(dp);

    return true;
}

ssize_t FileSystemImplementationPosix::pread(Path *p, char *buf, size_t size, off_t offset)
{
    int fd = open(p->c_str(), O_RDONLY | O_NOATIME);
    if (fd == -1) {
        // This might be a file not owned by you, if so, open fails if O_NOATIME is enabled.
        fd = open(p->c_str(), O_RDONLY);
        if (fd == -1) {
            // Give up permanently.
            return -1;
        }
        warning(FILESYSTEM,"You are not the owner of \"%s\" so backing up causes its access time to be updated.\n", p->c_str());
    }
    ssize_t n = ::pread(fd, buf, size, offset);
    close(fd);
    return n;
}

thread_local function<RecurseOption(Path *path, FileStat *stat)> recurse_cb1_;
thread_local FileStat recurse_stat_;

static int recurseCB1_(const char *fpath, const struct stat *sb, int tflag, struct FTW *ftwbuf)
{
    Path *p = Path::lookup(fpath);
    recurse_stat_.loadFrom(sb);
    RecurseOption ro = recurse_cb1_(p, &recurse_stat_);
    switch (ro) {
    case RecurseContinue: return FTW_CONTINUE;
    case RecurseSkipSubTree: return FTW_SKIP_SUBTREE;
    case RecurseStop: return FTW_STOP;
    }
    assert(0);
}

RC FileSystemImplementationPosix::recurse(Path *p, function<RecurseOption(Path *path, FileStat *stat)> cb)
{
    // Recurse into the root dir. Maximum 256 levels deep.
    // Look at symbolic links (ie do not follow them) so that
    // we can store the links in the tar file.
    recurse_cb1_ = cb;

    // Warning! nftw depth first is a standard depth first. I.e.
    // alfa/x.cc is sorted before
    // beta/gamma/y.cc
    // because it walks in alphabetic order, then recurses.
    //
    // The depth first sort used by depthFirstSortPath for the files map and others
    // will sort beta/gamma/y.cc before alfa/x.cc because it is deeper.
    // Therefore, do not expect nftw to produce the files in the same order as
    // the are later iterated after being stored in the maps.

    // Thus the work done in addEntry simply records the file system entries.
    // Relationships between the entries, like hard links, are calculated later,
    // because they expect earlier entries to be deeper or equal depth.
    int rc = nftw(p->c_str(), recurseCB1_, 256, FTW_PHYS|FTW_ACTIONRETVAL);

    if (rc  == -1) {
        return RC::ERR;
    }
    return RC::OK;
}

thread_local function<RecurseOption(const char *path, const struct stat *sb)> recurse_cb2_;

static int recurseCB2_(const char *fpath, const struct stat *sb, int tflag, struct FTW *ftwbuf)
{
    return recurse_cb2_(fpath, sb);
}

RC FileSystemImplementationPosix::recurse(Path *p, function<RecurseOption(const char *path, const struct stat *sb)> cb)
{
    recurse_cb2_ = cb;
    int rc = nftw(p->c_str(), recurseCB2_, 256, FTW_PHYS|FTW_ACTIONRETVAL);

    if (rc  == -1) {
        return RC::ERR;
    }
    return RC::OK;
}

RC FileSystemImplementationPosix::listFilesBelow(Path *p, std::vector<Path*> *files, SortOrder so)
{
    // TODO
    return RC::ERR;
}

RC FileSystemImplementationPosix::ctimeTouch(Path *p)
{
    struct stat sb;
    int rc = ::lstat(p->c_str(), &sb);
    if (rc) return RC::ERR;
    rc = ::chmod(p->c_str(), sb.st_mode);
    if (rc) return RC::ERR;

    return RC::OK;
}

RC FileSystemImplementationPosix::stat(Path *p, FileStat *fs)
{
    struct stat sb;
    int rc = ::lstat(p->c_str(), &sb);
    if (rc) return RC::ERR;
    fs->loadFrom(&sb);
    return RC::OK;
}

RC FileSystemImplementationPosix::chmod(Path *p, FileStat *fs)
{
    int rc = ::chmod(p->c_str(), fs->st_mode);
    if (rc) return RC::ERR;
    return RC::OK;
}

RC FileSystemImplementationPosix::utime(Path *p, FileStat *fs)
{
    struct timespec times[2];
    times[0].tv_sec = fs->st_atim.tv_sec;
    times[0].tv_nsec = fs->st_atim.tv_nsec;
    times[1].tv_sec = fs->st_mtim.tv_sec;
    times[1].tv_nsec = fs->st_mtim.tv_nsec;

    // Why always AT_SYMLINK_NOFOLLOW? Because beak never intends
    // to follow symlinks when storing files! beak stores symlinks themselves!
    assert(p->c_str()[0] == '/'); // Must be an absolute path, or utimensat wont work.
    int rc = utimensat(0, p->c_str(), times, AT_SYMLINK_NOFOLLOW);
    if (rc) {
        failure(FILESYSTEM,"Could not set modify time for \"%s\" (%s)\n", p->c_str(), strerror(errno));
        return RC::ERR;
    }
    return RC::OK;
}

Path *FileSystemImplementationPosix::mkTempFile(string prefix, string content)
{
    string p = "/tmp/"+prefix+"XXXXXX";
    char name[p.length()+1];
    strcpy(name, p.c_str());
    int fd = mkstemp(name);
    if (fd == -1) {
        error(FILESYSTEM, "Could not create temp file!\n");
    }
    size_t n = write(fd, &content[0], content.size());
    if (n != content.size()) {
        error(FILESYSTEM, "Could not write temp file!\n");
    }
    close(fd);
    return Path::lookup(name);
}

Path *FileSystemImplementationPosix::mkTempDir(string prefix)
{
    string p = "/tmp/"+prefix+"XXXXXX";
    char name[p.length()+1];
    strcpy(name, p.c_str());
    char *mount = mkdtemp(name);
    if (!mount) {
        error(FILESYSTEM, "Could not create temp directory!");
    }
    return Path::lookup(mount);
}

Path *FileSystemImplementationPosix::mkDir(Path *p, string name)
{
    Path *n = p->append(name);
    int rc = mkdir(n->c_str(), 0775);
    if (rc != 0) error(FILESYSTEM, "Could not create directory: \"%s\"\n", n->c_str());
    return n;
}

RC FileSystemImplementationPosix::rmDir(Path *p)
{
    int rc = rmdir(p->c_str());

    if (rc != 0) return RC::ERR;
    return RC::OK;
}

dev_t MakeDev(int maj, int min)
{
    return MKDEV(maj, min);
}

int MajorDev(dev_t d)
{
    return MAJOR(d);
}

int MinorDev(dev_t d)
{
    return MINOR(d);
}

RC FileSystemImplementationPosix::loadVector(Path *file, size_t blocksize, vector<char> *buf)
{
    char block[blocksize+1];

    int fd = open(file->c_str(), O_RDONLY);
    if (fd == -1) {
        return RC::ERR;
    }
    while (true) {
        ssize_t n = read(fd, block, sizeof(block));
        if (n == -1) {
            if (errno == EINTR) {
                continue;
            }
            failure(FILESYSTEM,"Could not read from gzfile %s errno=%d\n", file->c_str(), errno);
            close(fd);
            return RC::ERR;
        }
        buf->insert(buf->end(), block, block+n);
        if (n < (ssize_t)sizeof(block)) {
            break;
        }
    }
    close(fd);
    return RC::OK;
}

RC FileSystemImplementationPosix::createFile(Path *file, vector<char> *buf)
{
    int fd = open(file->c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd == -1) {
        FileStat fs;
        RC rc = FileSystemImplementationPosix::stat(file, &fs);
        if (rc.isOk()) {
            if (!fs.isIWUSR()) {
                // There was a non-write file in place. Let us remove it.
                deleteFile(file);
                fd = open(file->c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
            }
        }
        if (fd == -1) {
            failure(FILESYSTEM,"Could not create file %s from buffer (errno=%d)\n", file->c_str(), errno);
            return RC::ERR;
        }
    }
    char *p = buf->data();
    ssize_t total = buf->size();
    while (true) {
        ssize_t n = write(fd, buf->data(), buf->size());
        if (n == -1) {
            if (errno == EINTR) {
                continue;
            }
            failure(FILESYSTEM,"Could not write to file %s errno=%d\n", file->c_str(), errno);
            close(fd);
            return RC::ERR;
        }
	p += n;
	total -= n;
        if (total <= 0) {
            break;
        }
    }
    close(fd);
    return RC::OK;
}


bool FileSystemImplementationPosix::createFile(Path *file,
                                               FileStat *stat,
                                               std::function<size_t(off_t offset, char *buffer, size_t len)>
                                                       acquire_bytes)
{
    char buf[65536];
    off_t offset = 0;
    size_t remaining = stat->st_size;

    int fd = open(file->c_str(), O_WRONLY | O_CREAT | O_TRUNC, stat->st_mode);
    if (fd == -1) {
        FileStat fs;
        RC rc = FileSystemImplementationPosix::stat(file, &fs);
        if (rc.isOk()) {
            if (!fs.isIWUSR()) {
                // There was a non-write file in place. Let us remove it.
                deleteFile(file);
                fd = open(file->c_str(), O_WRONLY | O_CREAT | O_TRUNC, stat->st_mode);
            }
        }
        if (fd == -1) {
            failure(FILESYSTEM,"Could not create file %s from callback(errno=%d)\n", file->c_str(), errno);
            return false;
        }
    }

    debug(FILESYSTEM,"writing %ju bytes to file %s\n", remaining, file->c_str());

    while (remaining > 0) {
        size_t read = (remaining > sizeof(buf)) ? sizeof(buf) : remaining;
        size_t len = acquire_bytes(offset, buf, read);
        ssize_t n = write(fd, buf, len);
        if (n == -1) {
            if (errno == EINTR) {
                continue;
            }
            failure(FILESYSTEM,"Could not write to file %s errno=%d\n", file->c_str(), errno);
            close(fd);
            return false;
        }
	offset += n;
	remaining -= n;
    }
    close(fd);
    return true;
}

bool FileSystemImplementationPosix::createSymbolicLink(Path *file, FileStat *stat, string target)
{
    int rc = symlink(target.c_str(), file->c_str());
    if (rc) {
        error(FILESYSTEM, "Could not create symlink \"%s\" to %s\n", file->c_str(), target.c_str());
    }
    return true;
}

bool FileSystemImplementationPosix::createHardLink(Path *file, FileStat *stat, Path *target)
{
    int rc = link(target->c_str(), file->c_str());
    if (rc) {
        error(FILESYSTEM, "Could not create hard link \"%s\" to %s\n", file->c_str(), target->c_str());
    }
    return true;
}

bool FileSystemImplementationPosix::createFIFO(Path *file, FileStat *stat)
{
    int rc = mknod(file->c_str(), S_IFIFO|stat->st_mode, 0);
    if (rc) {
        error(FILESYSTEM, "Could not create fifo \"%s\"\n", file->c_str());
    }
    return true;
}

bool FileSystemImplementationPosix::readLink(Path *file, string *target)
{
    char buf[MAX_PATH_LENGTH];
    ssize_t n = readlink(file->c_str(), buf, sizeof(buf));
    if (n == -1) {
        return false;
    }
    *target = string(buf, n);
    return true;
}

bool FileSystemImplementationPosix::deleteFile(Path *file)
{
    int rc = unlink(file->c_str());
    if (rc) {
        error(FILESYSTEM, "Could not delete file \"%s\"\n", file->c_str());
    }
    return true;
}

string ownergroupString(uid_t uid, gid_t gid)
{
    struct passwd pwd;
    struct passwd *result;
    char buf[16000];
    string s;

    int rc = getpwuid_r(uid, &pwd, buf, sizeof(buf), &result);
    if (result == NULL)
    {
        if (rc == 0)
        {
            s = to_string(uid);
        }
        else
        {
            errno = rc;
            error(FILESYSTEM, "Internal error getpwuid_r %d", errno);
        }
    }
    else
    {
        s = pwd.pw_name;
    }
    s += "/";

    struct group grp;
    struct group *gresult;

    rc = getgrgid_r(gid, &grp, buf, sizeof(buf), &gresult);
    if (gresult == NULL)
    {
        if (rc == 0)
        {
            s += to_string(gid);
        }
        else
        {
            errno = rc;
            error(FILESYSTEM, "Internal error getgrgid_r %d", errno);
        }
    }
    else
    {
        s += grp.gr_name;
    }

    return s;
}

Path *Path::realpath()
{
    char tmp[PATH_MAX];
    tmp[0] = 0;
    const char *rc = ::realpath(c_str(), tmp);
    if (!rc) {
        // realpath somtimes return NULL, despite properly
        // finding the full path!
        if (errno == 2 && tmp[0] == 0) {
            return NULL;
        }
        // Odd realpath behaviour, let us use the tmp anyway.
    } else {
        assert(rc == tmp);
    }

    return Path::lookup(tmp);
}

bool makeDirHelper(const char *s)
{
    if (::mkdir(s, 0775) == -1)
    {
        if (errno != EEXIST) { return false; }
    }
    return true;
}

Path *configurationFile()
{
    return configuration_file_;
}

Path *cacheDir()
{
    return cache_dir_;
}

RC FileSystemImplementationPosix::enableWatch()
{
    inotify_fd_ = inotify_init1(IN_NONBLOCK);

    if (inotify_fd_ == -1) {
        error(FILESYSTEM, "Could not enable inotify watch. errno=%d\n", errno);
    }

    return RC::OK;
}

RC FileSystemImplementationPosix::addWatch(Path *p)
{
    int wd = inotify_add_watch(inotify_fd_, p->c_str(),
                               IN_ATTRIB |
                               IN_CREATE |
                               IN_DELETE |
                               IN_DELETE_SELF |
                               IN_MODIFY |
                               IN_MOVE_SELF |
                               IN_MOVED_FROM |
                               IN_MOVED_TO);

    debug(WATCH,"added \"%s\"\n", p->c_str());
    if (wd == -1) {
        warning(FILESYSTEM, "Could not add watch to \"%s\". (errno=%d %s)\n", p->c_str(), errno, strerror(errno));
    }
    return RC::OK;
}

#define LEN_NAME    256
#define EVENT_SIZE  sizeof(struct inotify_event)
#define BUF_LEN     (EVENT_SIZE + LEN_NAME + 1)

int FileSystemImplementationPosix::endWatch()
{
    ssize_t n = 0;
    int rc = ioctl(inotify_fd_, FIONREAD, &n);
    if (rc) {
        error(WATCH, "Could not read from inotify fd.\n");
    }
    if (n == 0) return 0;

    ssize_t i = 0;
    char buffer[BUF_LEN];
    n = read(inotify_fd_, buffer, BUF_LEN);

    if (n == -1 && errno != EAGAIN) {
        perror("read");
        exit(EXIT_FAILURE);
    }
    int count = 0;

    if (n<0) {
        error(WATCH, "Could not read from inotify fd!\n");
    }

    while (i<n)
    {
        struct inotify_event *event = (struct inotify_event*)&buffer[i];
        if (event->len)
        {
            count ++;
            if (event->mask & IN_CREATE)
            {
                debug(WATCH, "created %s\n", event->name);
            }
            if (event->mask & IN_MODIFY)
            {
                debug(WATCH, "modified %s\n", event->name);
            }
            if (event->mask & IN_DELETE ||
                event->mask & IN_DELETE_SELF) {
                debug(WATCH, "deleted %s\n", event->name);
            }
            if (event->mask & IN_ATTRIB) {
                debug(WATCH, "attributes changed %s\n", event->name);
            }
            if (event->mask & IN_MOVE_SELF) {
                debug(WATCH, "move %s\n", event->name);
            }
            if (event->mask & IN_MOVED_FROM) {
                debug(WATCH, "move from %s\n", event->name);
            }
            if (event->mask & IN_MOVED_TO) {
                debug(WATCH, "move to %s\n", event->name);
            }
            i += EVENT_SIZE + event->len;
        }
    }

    // This happens implicitly below, inotify_rm_watch(...);
    close(inotify_fd_);

    return count;
}
