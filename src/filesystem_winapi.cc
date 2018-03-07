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

#define WINVER 0x0601
#define _WIN32_WINNT 0x0601

#include <windows.h>

#include "filesystem.h"

#include "log.h"
#include "util.h"

using namespace std;

static ComponentId FILESYSTEM = registerLogComponent("filesystem");

bool FileStat::isRegularFile() { return S_ISREG(st_mode); }
bool FileStat::isDirectory() { return S_ISDIR(st_mode); }
void FileStat::setAsRegularFile() { st_mode |= S_IFREG; }
void FileStat::setAsDirectory() { st_mode |= S_IFDIR; }
bool FileStat::isSymbolicLink() { return false; }
bool FileStat::isCharacterDevice() { return false; }
bool FileStat::isBlockDevice() { return false; }
bool FileStat::isFIFO()  { return false; }
bool FileStat::isSOCK()  { return false; }
bool FileStat::isISUID() { return false; } // set uid
bool FileStat::isISGID() { return false; } // set gid
bool FileStat::isISVTX() { return false; } // sticky

bool FileStat::isIRUSR() { return false; }
bool FileStat::isIWUSR() { return false; }
bool FileStat::isIXUSR() { return false; }

bool FileStat::isIRGRP() { return false; }
bool FileStat::isIWGRP() { return false; }
bool FileStat::isIXGRP() { return false; }

bool FileStat::isIROTH() { return false; }
bool FileStat::isIWOTH() { return false; }
bool FileStat::isIXOTH() { return false; }


string FileStat::uidName()
{
    return "Woot!";

}

string FileStat::gidName()
{
    return "Woot!";
}

dev_t MakeDev(int maj, int min)
{
    return 0;
}

int MajorDev(dev_t d)
{
    return 0;
}

int MinorDev(dev_t d)
{
    return 0;
}

string ownergroupString(uid_t uid, gid_t gid)
{
    return "";
}

struct FileSystemImplementationWinapi : FileSystem
{
    bool readdir(Path *p, vector<Path*> *vec);
    ssize_t pread(Path *p, char *buf, size_t count, off_t offset);
    void recurse(function<void(Path*,FileStat*)> cb);
    RCC stat(Path *p, FileStat *fs);
    RCC chmod(Path *p, FileStat *fs);
    RCC utime(Path *p, FileStat *fs);
    Path *mkTempDir(string prefix);
    Path *mkDir(Path *p, string name);

    RCC loadVector(Path *file, size_t blocksize, std::vector<char> *buf);
    RCC createFile(Path *file, std::vector<char> *buf);
    bool createFile(Path *file,
                    FileStat *stat,
                    std::function<size_t(off_t offset, char *buffer, size_t len)> cb);
    bool createSymbolicLink(Path *file, FileStat *stat, string target);
    bool createHardLink(Path *file, FileStat *stat, Path *target);
    bool createFIFO(Path *file, FileStat *stat);
    bool readLink(Path *file, string *target);
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
    default_file_system_ = new FileSystemImplementationWinapi();
    return unique_ptr<FileSystem>(default_file_system_);
}


bool FileSystemImplementationWinapi::readdir(Path *p, vector<Path*> *vec)
{
    HANDLE find;
    WIN32_FIND_DATA find_data;
    char buf[MAX_PATH+2];

    strcpy(buf, p->c_str());
    strcat(buf, "/*");
    find = FindFirstFile(buf, &find_data);
    if (find == INVALID_HANDLE_VALUE) return false;

    do {
        vec->push_back(Path::lookup(find_data.cFileName));
    }
    while(FindNextFile(find, &find_data));

    FindClose(find);
    return true;
}

ssize_t FileSystemImplementationWinapi::pread(Path *p, char *buf, size_t count, off_t offset)
{
    FILE * f = fopen(p->c_str(), "rb");
    if (!f) return ERR;
    fseek(f, offset, SEEK_SET);
    ssize_t n = fread(buf, 1, count, f);
    fclose(f);
    return n;
}

void FileSystemImplementationWinapi::recurse(function<void(Path *,FileStat*)> cb)
{
}

RCC FileSystemImplementationWinapi::stat(Path *p, FileStat *fs)
{
    return RCC::ERRR;
}

RCC FileSystemImplementationWinapi::chmod(Path *p, FileStat *fs)
{
    return RCC::ERRR;
}

RCC FileSystemImplementationWinapi::utime(Path *p, FileStat *fs)
{
    return RCC::ERRR;
}

Path *FileSystemImplementationWinapi::mkTempDir(string prefix)
{
    int attempts = 0;
    string buf;
    buf.resize(MAX_PATH);
    size_t l = GetTempPath(MAX_PATH, &buf[0]);
    if (l+1 > MAX_PATH) {
        error(FILESYSTEM,"Cannot find the temp dir path!\n");
    }
    buf.resize(l);
    Path *tmp_path = Path::lookup(buf);
    Path *tmp_dir;

    for (;;) {
        string suffix = randomUpperCaseCharacterString(6);
        string dirname = prefix+suffix;
        tmp_dir = tmp_path->append(dirname);

        int rc = CreateDirectory(tmp_dir->c_str(), NULL);
        if (rc != 0) break;
        attempts++;
        if (attempts > 100) {
            error(FILESYSTEM, "Cannot create temporary directory. Too many fails.\n");
        }
    }
    return tmp_dir;
}

Path *FileSystemImplementationWinapi::mkDir(Path *p, string name)
{
    Path *n = p->append(name);
    int rc = CreateDirectory(n->c_str(), NULL);
    if (rc == 0) error(FILESYSTEM, "Could not create directory: \"%s\"\n", n->c_str());
    return n;
}

RCC FileSystemImplementationWinapi::loadVector(Path *file, size_t blocksize, vector<char> *buf)
{
    /*
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
    */
    return RCC::ERRR;
}

RCC FileSystemImplementationWinapi::createFile(Path *file, vector<char> *buf)
{
    /*
    int fd = open(file->c_str(), O_WRONLY | O_CREAT, 0666);
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
    close(fd);*/
    return RCC::ERRR;
}


bool FileSystemImplementationWinapi::createFile(Path *file,
                                                FileStat *stat,
                                                std::function<size_t(off_t offset, char *buffer, size_t len)> cb)
{
    return false;
}

bool FileSystemImplementationWinapi::createSymbolicLink(Path *file, FileStat *stat, string target)
{
    return false;
}

bool FileSystemImplementationWinapi::createHardLink(Path *file, FileStat *stat, Path *target)
{
    return false;
}

bool FileSystemImplementationWinapi::createFIFO(Path *file, FileStat *stat)
{
    return false;
}

bool FileSystemImplementationWinapi::readLink(Path *file, string *target)
{
    return false;
}

bool FileSystemImplementationWinapi::deleteFile(Path *file)
{
    return false;
}

uid_t geteuid()
{
    return 0;
}

gid_t getegid()
{
    return 0;
}

char *mkdtemp(char *pattern)
{
    return NULL;
}

Path *Path::realpath()
{
    char tmp[PATH_MAX];
    size_t n = GetFullPathName(c_str(), PATH_MAX, tmp, NULL);
    if (n == 0 || n >= PATH_MAX)
    {
        error(FILESYSTEM, "Could not find real path for %s\n", c_str());
    }
    unsigned int attr = GetFileAttributes(tmp);
    if (attr == INVALID_FILE_ATTRIBUTES) return NULL;
    return Path::lookup(tmp);
}

bool makeDirHelper(const char *s)
{
    int rc = CreateDirectory(s, NULL);
    if (rc == 0) {
        int err = GetLastError();
        if (err != ERROR_ALREADY_EXISTS) { return false; }
    }
    return true;
}

Path *configurationFile()
{
    Path *home = Path::lookup(getenv("HOME"));
    return home->append(".config/beak/beak.conf");
}
