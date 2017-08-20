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

#include "libtar/lib/libtar.h"

struct Atom;
struct Path;
struct TarFile;

using namespace std;

struct TarEntry
{
    
    TarEntry(size_t size);
    TarEntry(Path *abspath, Path *path, const struct stat *b,
             bool header = false);
    
    Path *path() {
        return path_;
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
    bool isSymLink()
	{
            return TH_ISSYM(tar_);
	}
    bool isHardLink()
	{
            return TH_ISLNK(tar_);
	}
    
    TarEntry *parent()
	{
            return parent_;
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
    struct stat *stat()
	{
            return &sb_;
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
    void setContent(vector<unsigned char> &c);
    size_t copy(char *buf, size_t size, size_t from);
    const bool isDir();
    const bool isHardlink();
    void updateSizes();
    void rewriteIntoHardLink(TarEntry *target);
    bool fixHardLink(Path *storage_dir);
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
    
    vector<TarEntry*>& dirs()
	{
            return dirs_;
	}
    vector<string>& files()
	{
            return files_;
	}
    
    void createSmallTar(int i);
    void createMediumTar(int i);
    void createLargeTar(uint32_t hash);
    
    vector<TarFile*> &tars() { return tars_; }
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
    TarFile *smallHashTar(vector<char> i)
	{
            return small_hash_tars_[i];
	}
    TarFile *mediumHashTar(vector<char> i)
	{
            return medium_hash_tars_[i];
	}
    TarFile *largeHashTar(vector<char> i)
	{
            return large_hash_tars_[i];
	}
    map<size_t, TarFile*>& smallTars()
	{
            return small_tars_;
	}
    map<size_t, TarFile*>& mediumTars()
	{
            return medium_tars_;
	}
    map<size_t, TarFile*>& largeTars()
	{
            return large_tars_;
	}
    map<vector<char>, TarFile*>& smallHashTars()
	{
            return small_hash_tars_;
	}
    map<vector<char>, TarFile*>& mediumHashTars()
	{
            return medium_hash_tars_;
	}
    map<vector<char>, TarFile*>& largeHashTars()
	{
            return large_hash_tars_;
	}
    
    void registerParent(TarEntry *p);
    void addChildrenSize(size_t s);
    
    void secsAndNanos(char *buf, size_t len);
    void injectHash(const char *buf, size_t len);
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
    vector<TarEntry*>& entries()
	{
            return entries_;
	}
    void sortEntries();
    
    void appendFileName(string name)
	{
            files_.push_back(name);
	}
    
    static TarEntry *newVolumeHeader();
    

    void calculateHash();
    vector<char> &hash();
    
    // This is a re-construction of how the entry would be listed by "tar tv"
    // tv_line_left is accessbits, ownership
    // tv_line_size is the size, to be left padded with space
    // tv_line_right is the last modification time
    string tv_line_left, tv_line_size, tv_line_right;
    
    private:
    
    int num_long_path_blocks;
    int num_long_link_blocks;
    int num_header_blocks;
    size_t header_size_;
    
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
    
    struct stat sb_;
    TarFile *tar_file_;
    size_t tar_offset_;
    size_t blocked_size_;
    size_t disk_size;
    
    // If this is a directory, then all children sizes are summed here.
    size_t children_size_;
    TarEntry *parent_;
    TAR *tar_;
    bool is_tar_storage_dir_;
    vector<TarEntry*> dirs_; // Directories to be listed inside this TarEntry
    vector<string> files_; // Files to be listed inside this TarEntry (ie the virtual tar files..)
    TarFile *taz_file_;
    bool taz_file_in_use_ = false;
    TarFile *gz_file_;
    bool gz_file_in_use_ = false;
    vector<TarFile*> tars_; // All tars including the taz.
    map<size_t, TarFile*> small_tars_;  // Small file tars in side this TarEntry
    map<size_t, TarFile*> medium_tars_; // Medium file tars in side this TarEntry
    map<size_t, TarFile*> large_tars_;  // Large file tars in side this TarEntry
    map<vector<char>,TarFile*> small_hash_tars_;
    map<vector<char>,TarFile*> medium_hash_tars_;
    map<vector<char>,TarFile*> large_hash_tars_;
    vector<TarEntry*> entries_; // The contents stored in the tar files.
    
    TarEntry *tar_collection_dir = NULL; // This entry is stored in this tar collection dir.
    bool is_added_to_directory_ = false;
    bool virtual_file_ = false;
    vector<unsigned char> content;

    void calculateSHA256Hash();
    
    vector<char> sha256_hash_;
};

void cookEntry(string *listing, TarEntry *entry);

bool eatEntry(vector<char> &v, vector<char>::iterator &i, Path *dir_to_prepend,
              mode_t *mode, size_t *size, size_t *offset, string *tar, Path **path,
              string *link, bool *is_sym_link,
              time_t *msecs, time_t *mnanos,
              time_t *asecs, time_t *ananos,
              time_t *csecs, time_t *cnanos,
              bool *eof, bool *err);


#endif
