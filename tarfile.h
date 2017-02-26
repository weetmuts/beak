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

#include <stddef.h>
#include <cstdint>
#include <ctime>
#include <map>
#include <string>
#include <utility>
#include <vector>

struct TarEntry;

using namespace std;

enum TarContents
{
	DIR_TAR, SMALL_FILES_TAR, MEDIUM_FILES_TAR, SINGLE_LARGE_FILE_TAR
};

struct TarFile
{
	TarFile()
	{
	}
	TarFile(TarEntry *d, TarContents tc, int n, bool dirs);
	string name()
	{
		return name_;
	}
	size_t size()
	{
		return size_;
	}
	void fixSize()
	{
		size_ = current_tar_offset_;
	}
	void addEntryLast(TarEntry *entry);
	void addEntryFirst(TarEntry *entry);
	void addVolumeHeader();
	TarEntry *volumeHeader()
	{
		return volume_header_;
	}
	void finishHash();
	pair<TarEntry*, size_t> findTarEntry(size_t offset);

	void calculateSHA256Hash();
	size_t currentTarOffset()
	{
		return current_tar_offset_;
	}
	struct timespec *mtim()
	{
		return &mtim_;
	}

private:

	TarEntry *in_directory;
	// A virtual tar can contain small files, medium files or a single large file.
	TarContents tar_contents = SMALL_FILES_TAR;

	// Name of the tar, tar00000000.tar taz00000000.tar tal00000000.tar tam00000000.tar
	string name_;
	uint32_t hash;
	bool hash_initialized = false;
	size_t size_;
	map<size_t, TarEntry*> contents;
	vector<size_t> offsets;
	size_t current_tar_offset_ = 0;
	struct timespec mtim_;
	TarEntry *volume_header_;
	TarEntry *volume_contents;
	TarEntry *volume_footer;

};

#endif
