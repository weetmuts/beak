/*
 Copyright (C) 2017 Fredrik Öhrström

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
#include <sys/types.h>
#include <unistd.h>

using namespace std;

static ComponentId FILESYSTEM = registerLogComponent("filesystem");

bool FileStat::isRegularFile() { return S_ISREG(st_mode); }
bool FileStat::isDirectory() { return S_ISDIR(st_mode); }
void FileStat::setAsRegularFile() { st_mode |= S_IFREG; }
void FileStat::setAsDirectory() { st_mode |= S_IFDIR; }

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
    void recurse(function<void(Path *path, FileStat *stat)> cb);
    RC stat(Path *p, FileStat *fs);
    RC chmod(Path *p, FileStat *stat);
    RC utime(Path *p, FileStat *stat);
    Path *mkTempDir(string prefix);
    Path *mkDirp(Path *p);
    Path *mkDir(Path *p, string name);
    int loadVector(Path *file, size_t blocksize, std::vector<char> *buf);
    int createFile(Path *file, std::vector<char> *buf);
    bool createFile(Path *path, FileStat *stat,
                     std::function<size_t(off_t offset, char *buffer, size_t len)> cb);
    bool createSymbolicLink(Path *path, FileStat *stat, string target);
    bool createHardLink(Path *path, FileStat *stat, Path *target);
    bool createFIFO(Path *path, FileStat *stat);
    bool readLink(Path *path, string *target);
    bool deleteFile(Path *file);

private:

    Path *root;
    Path *cache;
};

FileSystem *default_file_system_;

FileSystem *defaultFileSystem()
{
    return default_file_system_;
}

unique_ptr<FileSystem> newDefaultFileSystem()
{
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
    if (fd == -1) return ERR;
    int rc = ::pread(fd, buf, size, offset);
    close(fd);
    return rc;
}

void FileSystemImplementationPosix::recurse(function<void(Path *path, FileStat *stat)> cb)
{
    //int rc = nftw(root_dir.c_str(), addEntry, 256, FTW_PHYS|FTW_ACTIONRETVAL);
}


RC FileSystemImplementationPosix::stat(Path *p, FileStat *fs)
{
    struct stat sb;
    int rc = ::lstat(p->c_str(), &sb);
    if (rc) return ERR;
    fs->loadFrom(&sb);
    return OK;
}

RC FileSystemImplementationPosix::chmod(Path *p, FileStat *fs)
{
    int rc = ::chmod(p->c_str(), fs->st_mode);
    if (rc) return ERR;
    return OK;
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
    int rc = utimensat(0, p->c_str(), times, AT_SYMLINK_NOFOLLOW);
    if (rc) return ERR;
    return OK;
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

Path *FileSystemImplementationPosix::mkDirp(Path *p)
{

    return p;
}

Path *FileSystemImplementationPosix::mkDir(Path *p, string name)
{
    Path *n = p->append(name);
    RC rc = mkdir(n->c_str(), 0775);
    if (rc != OK) error(FILESYSTEM, "Could not create directory: \"%s\"\n", n->c_str());
    return n;
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

int FileSystemImplementationPosix::loadVector(Path *file, size_t blocksize, vector<char> *buf)
{
    char block[blocksize+1];

    int fd = open(file->c_str(), O_RDONLY);
    if (fd == -1) {
        return -1;
    }
    while (true) {
        ssize_t n = read(fd, block, sizeof(block));
        if (n == -1) {
            if (errno == EINTR) {
                continue;
            }
            failure(FILESYSTEM,"Could not read from gzfile %s errno=%d\n", file->c_str(), errno);
            close(fd);
            return -1;
        }
        buf->insert(buf->end(), block, block+n);
        if (n < (ssize_t)sizeof(block)) {
            break;
        }
    }
    close(fd);
    return 0;
}

int FileSystemImplementationPosix::createFile(Path *file, vector<char> *buf)
{
    int fd = open(file->c_str(), O_WRONLY | O_CREAT | O_TRUNC, 0600);
    if (fd == -1) {
	failure(FILESYSTEM,"Could not open file %s errno=%d\n", file->c_str(), errno);
        return -1;
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
            return -1;
        }
	p += n;
	total -= n;
        if (total <= 0) {
            break;
        }
    }
    close(fd);
    return 0;
}


bool FileSystemImplementationPosix::createFile(Path *file,
                                               FileStat *stat,
                                               std::function<size_t(off_t offset, char *buffer, size_t len)> cb)
{
    char buf[65536];
    off_t offset = 0;
    size_t remaining = stat->st_size;

    int fd = open(file->c_str(), O_WRONLY | O_CREAT, stat->st_mode);
    if (fd == -1) {
	failure(FILESYSTEM,"Could not open file %s errno=%d\n", file->c_str(), errno);
        return false;
    }

    debug(FILESYSTEM,"Writing %ju bytes to file %s\n", remaining, file->c_str());

    while (remaining > 0) {
        size_t read = (remaining > sizeof(buf)) ? sizeof(buf) : remaining;
        size_t len = cb(offset, buf, read);
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
    Path *home = Path::lookup(getenv("HOME"));
    return home->append(".config/beak/beak.conf");
}
