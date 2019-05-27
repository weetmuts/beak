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
void FileStat::setAsExecutable() { st_mode |= S_IXUSR | S_IRUSR | S_IWUSR; }
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

void FileStat::setIWUSR() { st_mode |= S_IWUSR; }


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
    RC recurse(Path *p, function<RecurseOption(Path*,FileStat*)> cb);
    RC recurse(Path *p, function<RecurseOption(const char *path, const struct stat *sb)> cb);
    RC ctimeTouch(Path *file);
    RC stat(Path *p, FileStat *fs);
    RC chmod(Path *p, FileStat *fs);
    RC utime(Path *p, FileStat *fs);
    Path *mkTempFile(string prefix, string content);
    Path *mkTempDir(string prefix);
    Path *mkDir(Path *p, string name);
    RC rmDir(Path *p);

    RC loadVector(Path *file, size_t blocksize, std::vector<char> *buf);
    RC createFile(Path *file, std::vector<char> *buf);
    bool createFile(Path *file,
                    FileStat *stat,
                    std::function<size_t(off_t offset, char *buffer, size_t len)> cb);
    bool createSymbolicLink(Path *file, FileStat *stat, string target);
    bool createHardLink(Path *file, FileStat *stat, Path *target);
    bool createFIFO(Path *file, FileStat *stat);
    bool readLink(Path *file, string *target);
    bool deleteFile(Path *file);

    RC enableWatch();
    RC addWatch(Path *dir);
    int endWatch();

    FileSystemImplementationWinapi() : FileSystem("FileSystemImplementationWinapi") {}

private:

    Path *root;
    Path *cache;
};

Path *cache_dir_ {};

Path *configuration_file_ {};

Path *initCacheDir_()
{
    const char *homedrive = getenv("HOMEDRIVE");
    const char *homepath = getenv("HOMEPATH");

    if (homedrive == NULL) {
        error(FILESYSTEM, "Could not find home drive!\n");
    }
    if (homepath == NULL) {
        error(FILESYSTEM, "Could not find home directory!\n");
    }

    string homes = string(homedrive)+string(homepath);
    Path *homep = Path::lookup(homes);
    return homep->append(".cache/beak");
}

Path *initConfigurationFile_()
{
    const char *homedrive = getenv("HOMEDRIVE");
    const char *homepath = getenv("HOMEPATH");

    if (homedrive == NULL) {
        error(FILESYSTEM, "Could not find home drive!\n");
    }
    if (homepath == NULL) {
        error(FILESYSTEM, "Could not find home directory!\n");
    }

    string homes = string(homedrive)+string(homepath);
    Path *homep = Path::lookup(homes);
    return homep->append(".config/beak/beak.conf");
}

unique_ptr<FileSystem> newDefaultFileSystem()
{
    if (!cache_dir_) {
        cache_dir_ = initCacheDir_();
    }
    if (!configuration_file_) {
        configuration_file_ = initConfigurationFile_();
    }
    return unique_ptr<FileSystem>(new FileSystemImplementationWinapi());
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
    if (!f) return -1;
    fseek(f, offset, SEEK_SET);
    ssize_t n = fread(buf, 1, count, f);
    fclose(f);
    return n;
}

RC FileSystemImplementationWinapi::recurse(Path *p, function<RecurseOption(Path *,FileStat*)> cb)
{
    assert(0);
    return RC::ERR;
}

RC FileSystemImplementationWinapi::recurse(Path *p, function<RecurseOption(const char *path, const struct stat *sb)> cb)
{
    assert(0);
    return RC::ERR;
}

RC FileSystemImplementationWinapi::ctimeTouch(Path *file)
{
    return RC::ERR;
}

RC FileSystemImplementationWinapi::stat(Path *p, FileStat *fs)
{
    return RC::ERR;
}

RC FileSystemImplementationWinapi::chmod(Path *p, FileStat *fs)
{
    return RC::ERR;
}

RC FileSystemImplementationWinapi::utime(Path *p, FileStat *fs)
{
    return RC::ERR;
}

Path *FileSystemImplementationWinapi::mkTempFile(string prefix, string content)
{
    return NULL;
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

Path *FileSystemImplementationWinapi::mkDir(Path *p, string name, int permissions)
{
    Path *n = p->append(name);
    int rc = CreateDirectory(n->c_str(), NULL);
    if (rc == 0) error(FILESYSTEM, "Could not create directory: \"%s\"\n", n->c_str());
    return n;
}

RC FileSystemImplementationWinapi::rmDir(Path *p)
{
    return RC::ERR;
}

RC FileSystemImplementationWinapi::loadVector(Path *file, size_t blocksize, vector<char> *buf)
{
    assert(file != NULL && buf != NULL && blocksize > 0);
    char block[blocksize+1];

    HANDLE fd = CreateFile(file->c_str(),
                           GENERIC_READ,
                           0,                      // do not share
                           NULL,                   // default security
                           OPEN_EXISTING,
                           FILE_ATTRIBUTE_NORMAL,  // normal file
                           NULL);

    if (fd == INVALID_HANDLE_VALUE) {
        return RC::ERR;
    }

    while (true) {
        DWORD n;
        BOOL rc = ReadFile(fd, block, blocksize, &n, NULL);
        if (!rc) {
            DWORD err = GetLastError();
            error(FILESYSTEM,"Could not read from file %s errno=%d\n", file->c_str(), err);
            CloseHandle(fd);
            return RC::ERR;
        }
        buf->insert(buf->end(), block, block+n);
        if (n < (ssize_t)sizeof(block)) {
            break;
        }
    }
    CloseHandle(fd);

    return RC::OK;
}

RC FileSystemImplementationWinapi::createFile(Path *file, vector<char> *buf)
{
    assert(file != NULL && buf != NULL);

    HANDLE fd = CreateFile(file->c_str(),
                           GENERIC_WRITE,
                           0,                      // do not share
                           NULL,                   // default security
                           CREATE_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL,  // normal file
                           NULL);

    if (fd == INVALID_HANDLE_VALUE) {
        return RC::ERR;
    }

    DWORD n;
    BOOL rc = WriteFile(fd, &(*buf)[0], buf->size(), &n, NULL);
    if (!rc) {
        DWORD err = GetLastError();
        error(FILESYSTEM,"Could not write to file %s errno=%d\n", file->c_str(), err);
        CloseHandle(fd);
        return RC::ERR;
    }
    if (n != buf->size()) {
        error(FILESYSTEM,"Expected %ju bytes to be written to file %s, wrote only %ju\n",
              buf->size(), file->c_str(), n);
    }
    CloseHandle(fd);

    return RC::OK;
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
    return configuration_file_;
}

Path *cacheDir()
{
    return cache_dir_;
}

RC  FileSystemImplementationWinapi::enableWatch()
{
    return RC::ERR;
}

RC  FileSystemImplementationWinapi::addWatch(Path *dir)
{
    return RC::ERR;
}

int  FileSystemImplementationWinapi::endWatch()
{
    return 0;
}
