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

#ifndef TARENTRY_H
#define TARENTRY_H

#include <stddef.h>
#include <sys/stat.h>
#include <cstdint>
#include <map>
#include <openssl/sha.h>
#include <string>
#include <vector>

#include "tar.h"
#include "filesystem.h"

struct Atom;
struct Path;
struct TarFile;

struct TarEntry
{

    TarEntry();
    TarEntry(size_t size, TarHeaderStyle ths);
    TarEntry(Path *abspath, Path *path, FileStat *st, TarHeaderStyle ths, bool should_content_split);
    ~TarEntry();

    Path *path()
    {
        return path_;
    }
    Path *abspath()
    {
        return abspath_;
    }
    Path *tarpath()
    {
        return tarpath_;
    }
    uint32_t tarpathHash()
    {
        return tarpath_hash_;
    }
    Path *link()
    {
        return link_;
    }
    bool isRegularFile()
    {
        return fs_.isRegularFile();
    }
    bool isDirectory()
    {
        return fs_.isDirectory();
    }
    bool isSymbolicLink()
    {
        return fs_.isSymbolicLink();
    }
    bool isCharacterDevice()
    {
        return fs_.isCharacterDevice();
    }
    bool isBlockDevice()
    {
        return fs_.isBlockDevice();
    }
    bool isHardLink()
    {
        return is_hard_linked_;
    }
    FileStat *stat()
    {
        return &fs_;
    }

    TarEntry *parent()
    {
        return parent_;
    }
    TarEntry *storageDir()
    {
        return storage_dir_;
    }
    size_t blockedSize()
    {
        return blocked_size_;
    }
    size_t headerSize()
    {
        return header_size_;
    }
    size_t childrenSize()
    {
        return children_size_;
    }
    bool isStorageDir()
    {
        return is_tar_storage_dir_;
    }
    bool isAddedToDir()
    {
        return is_added_to_directory_;
    }

    void calculateTarpath(Path *storage_dir);
    void setContent(std::vector<char> &c);
    size_t copy(char *buf, size_t size, size_t from, FileSystem *fs);
    void updateSizes();
    void rewriteIntoHardLink(TarEntry *target);
    bool calculateHardLink(Path *storage_dir);
    void moveEntryToNewParent(TarEntry *entry, TarEntry *parent);
    void copyEntryToNewParent(TarEntry *entry, TarEntry *parent);
    void updateMtim(struct timespec *mtim);
    void registerTarFile(TarFile *tf, size_t o);
    void registerTazFile();
    void registerGzFile();
    void enableTazFile()
    {
        taz_file_in_use_ = true;
    }
    void enableGzFile()
    {
        gz_file_in_use_ = true;
    }
    bool hasTazFile()
    {
        return taz_file_in_use_;
    }
    bool hasGzFile()
    {
        return gz_file_in_use_;
    }
    TarFile *tarFile()
    {
        return tar_file_;
    }
    TarFile *tazFile()
    {
        return taz_file_;
    }
    TarFile *gzFile()
    {
        return gz_file_;
    }
    size_t tarOffset()
    {
        return tar_offset_;
    }

    std::vector<TarEntry*>& dirs()
    {
        return dirs_;
    }
    std::vector<TarFile*>& files()
    {
        return files_;
    }

    void createSmallTar(int i);
    void createMediumTar(int i);
    void createLargeTar(uint32_t hash);

    std::vector<TarFile*> &tars() { return tars_; }
    TarFile *smallTar(int i)
    {
        return small_tars_[i];
    }
    TarFile *mediumTar(int i)
    {
        return medium_tars_[i];
    }
    TarFile *largeTar(uint32_t hash)
    {
        return large_tars_[hash];
    }
    bool hasLargeTar(uint32_t hash)
    {
        return large_tars_.count(hash) > 0;
    }
    TarFile *smallHashTar(std::vector<char> i)
    {
        return small_hash_tars_[i];
    }
    TarFile *mediumHashTar(std::vector<char> i)
    {
        return medium_hash_tars_[i];
    }
    TarFile *largeHashTar(std::vector<char> i)
    {
        return large_hash_tars_[i];
    }
    TarFile *contentHashTar(std::vector<char> i)
    {
        return content_hash_tars_[i];
    }
    std::map<size_t, TarFile*>& smallTars()
    {
        return small_tars_;
    }
    std::map<size_t, TarFile*>& mediumTars()
    {
        return medium_tars_;
    }
    std::map<size_t, TarFile*>& largeTars()
    {
        return large_tars_;
    }
    std::map<std::vector<char>, TarFile*>& smallHashTars()
    {
        return small_hash_tars_;
    }
    std::map<std::vector<char>, TarFile*>& mediumHashTars()
    {
        return medium_hash_tars_;
    }
    std::map<std::vector<char>, TarFile*>& largeHashTars()
    {
        return large_hash_tars_;
    }
    std::map<std::vector<char>, TarFile*>& contentHashTars()
    {
        return content_hash_tars_;
    }

    void registerParent(TarEntry *p);
    void addChildrenSize(size_t s);

    void secsAndNanos(char *buf, size_t len);
    void setAsStorageDir()
    {
        is_tar_storage_dir_ = true;
    }
    void setAsAddedToDir()
    {
        is_added_to_directory_ = true;
    }
    void addDir(TarEntry *dir);
    void addEntry(TarEntry *te);
    std::vector<TarEntry*>& entries()
    {
        return entries_;
    }
    void sortEntries();

    void appendBeakFile(TarFile *tf)
    {
        files_.push_back(tf);
    }

    void calculateHash();
    std::vector<char> &metaHash();

    private:

    size_t header_size_;
    TarHeaderStyle tar_header_style_;
    // Full path and name, to read the file from the underlying file system.
    Path *abspath_;
    // Just the name of the file.
    Atom *name_;
    // The path below root_dir, starts with a /.
    Path *path_;
    // The path inside the tar, does not start with a /.
    // And can be much shorter than path, because the tar can be located
    // deep in the tree below root_dir.
    Path *tarpath_;
    // The hash of the tarpath is used to spread the files into tars.
    uint32_t tarpath_hash_;
    // The target file for a link.
    Path *link_;

    FileStat fs_;

    bool is_hard_linked_;
    TarFile *tar_file_;
    size_t tar_offset_;
    size_t blocked_size_;

    // If this is a directory, then all children sizes are summed here.
    size_t children_size_;
    TarEntry *parent_;

    // This is where the tar was stored.
    TarEntry *storage_dir_;

    bool is_tar_storage_dir_;
    std::vector<TarEntry*> dirs_; // Directories to be listed inside this TarEntry
    std::vector<TarFile*> files_; // Files to be listed inside this TarEntry (ie the virtual tar files..)
    TarFile *taz_file_;
    bool taz_file_in_use_ = false;
    TarFile *gz_file_;
    bool gz_file_in_use_ = false;
    std::vector<TarFile*> tars_; // All tars including the taz.
    std::map<size_t, TarFile*> small_tars_;  // Small file tars in side this TarEntry
    std::map<size_t, TarFile*> medium_tars_; // Medium file tars in side this TarEntry
    std::map<size_t, TarFile*> large_tars_;  // Large file tars in side this TarEntry
    std::map<std::vector<char>,TarFile*> small_hash_tars_;
    std::map<std::vector<char>,TarFile*> medium_hash_tars_;
    std::map<std::vector<char>,TarFile*> large_hash_tars_;
    std::map<std::vector<char>,TarFile*> content_hash_tars_;
    std::vector<TarEntry*> entries_; // The contents stored in the tar files.

    bool is_added_to_directory_ = false;
    bool virtual_file_ = false;
    std::vector<char> content;

    void calculateSHA256Hash();

    std::vector<char> meta_sha256_hash_;

    bool should_content_split_;

    friend void cookEntry(std::string *listing, TarEntry *entry);
};

void cookEntry(std::string *listing, TarEntry *entry);
std::string cookColumns();

bool eatEntry(int beak_version, std::vector<char> &v, std::vector<char>::iterator &i, Path *dir_to_prepend,
              FileStat *fs, size_t *offset, std::string *tar, Path **path,
              std::string *link, bool *is_sym_link, bool *is_hard_link,
              uint *num_parts, size_t *part_offset, size_t *part_size, size_t *last_part_size,
              size_t *disk_size, size_t *last_disk_size,
              bool *eof, bool *err);


#endif
