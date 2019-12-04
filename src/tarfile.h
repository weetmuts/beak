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

#ifndef TARFILE_H
#define TARFILE_H

#include "always.h"
#include "filesystem.h"
#include "tar.h"
#include "tarentry.h"
#include "util.h"

#include <stddef.h>
#include <cstdint>
#include <ctime>
#include <map>
#include <string>
#include <utility>
#include <vector>
#include <openssl/sha.h>


struct TarEntry;

enum class TarContents
{
    INDEX_FILE,
    DIR_TAR,
    SMALL_FILES_TAR,
    MEDIUM_FILES_TAR,
    SINGLE_LARGE_FILE_TAR,
    SPLIT_LARGE_FILE_TAR,
    CONTENT_SPLIT_LARGE_FILE_TAR
};

enum class TarFilePaddingStyle : short
{
    None,     // Tar files are not padded at all.
    Relative, // Padded relative the size, small size -> small padding, large size -> large padding.
    Absolute  // Always pad to the target size -ta/--targetsize. Which by default is 10M.
};

#define INDEX_FILE_CHAR 'z'
#define DIR_TAR_CHAR 'y'
#define SMALL_FILES_TAR_CHAR 's'
#define MEDIUM_FILES_TAR_CHAR 'm'
#define SINGLE_LARGE_FILE_TAR_CHAR 'l'
#define SPLIT_LARGE_FILE_TAR_CHAR 'i'
#define CONTENT_SPLIT_LARGE_FILE_TAR_CHAR 'c'

struct TarFile;

struct TarFileName
{
    TarContents type {};
    int version {};
    time_t sec {};
    long nsec {};
    size_t size {};
    size_t ondisk_size {};
    size_t backup_size {};
    std::string header_hash {};
    uint part_nr {};
    uint num_parts {};

    TarFileName() : version(2) {};
    TarFileName(const TarFileName&tfn) : type(tfn.type),
        version(tfn.version),
        sec(tfn.sec), nsec(tfn.nsec),
        size(tfn.size),
        ondisk_size(tfn.ondisk_size),
        backup_size(tfn.backup_size),
        header_hash(tfn.header_hash),
        part_nr(tfn.part_nr),
        num_parts(tfn.num_parts) {};
    TarFileName(TarFile *tf, uint partnr);

    bool equals(TarFileName *tfn) {
        return tfn->type == type &&
            tfn->version == version &&
            tfn->sec == sec &&
            tfn->nsec == nsec &&
            tfn->size == size &&
            tfn->header_hash == header_hash &&
            tfn->part_nr == part_nr;
    }

    bool isIndexFile() {
        return type == TarContents::INDEX_FILE;
    }

    static bool isIndexFile(Path *);

    bool parseFileName(std::string &name, std::string *dir = NULL);
    void writeTarFileNameIntoBuffer(char *buf, size_t buf_len, Path *dir);
    std::string asStringWithDir(Path *dir);
    Path *asPathWithDir(Path *dir);

    static char chartype(TarContents type) {
        switch (type) {
        case TarContents::INDEX_FILE: return INDEX_FILE_CHAR;
        case TarContents::DIR_TAR: return DIR_TAR_CHAR;
        case TarContents::SMALL_FILES_TAR: return SMALL_FILES_TAR_CHAR;
        case TarContents::MEDIUM_FILES_TAR: return MEDIUM_FILES_TAR_CHAR;
        case TarContents::SINGLE_LARGE_FILE_TAR: return SINGLE_LARGE_FILE_TAR_CHAR;
        case TarContents::SPLIT_LARGE_FILE_TAR: return SPLIT_LARGE_FILE_TAR_CHAR;
        case TarContents::CONTENT_SPLIT_LARGE_FILE_TAR: return CONTENT_SPLIT_LARGE_FILE_TAR_CHAR;
        }
        return 0;
    }

    static bool typeFromChar(char c, TarContents *tc) {
        switch (c) {
        case INDEX_FILE_CHAR: *tc = TarContents::INDEX_FILE; return true;
        case DIR_TAR_CHAR: *tc = TarContents::DIR_TAR; return true;
        case SMALL_FILES_TAR_CHAR: *tc = TarContents::SMALL_FILES_TAR; return true;
        case MEDIUM_FILES_TAR_CHAR: *tc = TarContents::MEDIUM_FILES_TAR; return true;
        case SINGLE_LARGE_FILE_TAR_CHAR: *tc = TarContents::SINGLE_LARGE_FILE_TAR; return true;
        case SPLIT_LARGE_FILE_TAR_CHAR: *tc = TarContents::SPLIT_LARGE_FILE_TAR; return true;
        case CONTENT_SPLIT_LARGE_FILE_TAR_CHAR: *tc = TarContents::CONTENT_SPLIT_LARGE_FILE_TAR; return true;
        }
        return false;
    }

    static const char *suffixtype(TarContents type) {
        switch (type) {
        case TarContents::INDEX_FILE: return "gz";
        case TarContents::DIR_TAR:
        case TarContents::SMALL_FILES_TAR:
        case TarContents::MEDIUM_FILES_TAR:
        case TarContents::SINGLE_LARGE_FILE_TAR:
        case TarContents::SPLIT_LARGE_FILE_TAR: return "tar";
        case TarContents::CONTENT_SPLIT_LARGE_FILE_TAR: return "bin";
        }
        assert(0);
        return "";
    }

private:

    bool parseFileNameVersion_(std::string &name, size_t p1);
    void writeTarFileNameIntoBufferVersion_(char *buf, size_t buf_len, Path *dir);
};

struct TarFile
{
    TarFile() : num_parts_(1), part_size_(0) { }
    TarFile(TarContents tc);
    ~TarFile();

    TarContents type() { return tar_contents_; }
    size_t contentSize() { return content_size_; }
    size_t partContentSize(uint partnr);
    size_t diskSize(uint partnr);
    size_t partHeaderSize() { return part_header_size_; }
    // Given an offset into a multivol part, find the offset into
    // the original tarfile that includes a header.
    size_t calculateOriginTarOffset(uint partnr, size_t offset);

    uint numParts()
    {
        return num_parts_;
    }
    size_t onDiskSize_(size_t from, TarContents type, TarFilePaddingStyle padding, size_t target_size);
    void fixSize(size_t split_size, TarHeaderStyle ths, TarFilePaddingStyle padding, size_t target_size);
    void addEntryLast(TarEntry *entry);
    void addEntryFirst(TarEntry *entry);

    void finishHash();
    std::pair<TarEntry*, size_t> findTarEntry(size_t offset);
    void calculateHash();
    void calculateHash(std::vector<std::pair<TarFile*,TarEntry*>> &tars, std::string &contents);
    void calculateHashFromString(std::string &contents);
    std::vector<char> &hash();

    size_t currentTarOffset()
    {
        return current_tar_offset_;
    }
    struct timespec *mtim()
    {
        return &mtim_;
    }
    void updateMtim(struct timespec *mtim);

    // readVirtualTar is used to present the backup filesystem
    // Write size bytes of the contents of the tar file into buf,
    // start reading at offest in the tar file.
    size_t readVirtualTar(char *buf, size_t size, off_t offset, FileSystem *fs, uint partnr);

    // file: Write the tarfile contents into this file.
    // stat: With this size and permissions.
    // src_fs: Fetch the tarfile contents from this filesystem
    // dst_fs: Store into this filesystem
    // off: Start storing from this offset in the tar file.
    bool createFilee(Path *file, FileStat *stat, uint partnr,
                     FileSystem *src_fs, FileSystem *dst_fs, size_t off,
                     std::function<void(size_t)> update_progress);

    TarEntry *singleContent() {
        return contents_.begin()->second;
    }

private:

    // A virtual tar can contain small files, medium files or a single large file.
    TarContents tar_contents_ = TarContents::SMALL_FILES_TAR;
    uint32_t hash_;
    bool hash_initialized = false;
    // The size of all the tar entries, content and tar headers.
    // The tar file can be exactly this file if TarFilePaddingStyle is None.
    // But the default is to round the disk file size to nice boundaries.
    size_t content_size_;
    std::map<size_t, TarEntry*> contents_;
    std::vector<size_t> offsets;
    size_t current_tar_offset_ = 0;
    // The mtim_->tv_nsec is always moved up to nearest microsecond boundary in the future.
    struct timespec mtim_;
    UpdateDisk disk_update;

    void calculateSHA256Hash();

    std::vector<char> sha256_hash_;
    // Number of parts.
    uint num_parts_ {};
    // The size of each part, except the last one, which could be smaller.
    size_t part_size_ {};
    // The part size on disk can be larger, which includes padding.
    size_t ondisk_part_size_ {};

    // The last part can be smaller.
    size_t last_part_size_ {};
    // The last part size on disk can be larger, which includes padding.
    size_t ondisk_last_part_size_ {};

    // A tar parts file by itself can have a tar continutation header.
    size_t part_header_size_ {};
    // How many extra 512 byte blocks are need if the file name exceeds 100 chars?
    size_t num_long_path_blocks_ {};
    // Set to true when the hash is valid.
    bool sha256_calculated_ {};
};

#endif
