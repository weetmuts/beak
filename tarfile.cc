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

#include "tarfile.h"

#include <openssl/sha.h>
#include <string.h>
#include <algorithm>
#include <cassert>
#include <cstdio>
#include <functional>
#include <iterator>
#include <sstream>

#include "log.h"
#include "tarentry.h"
#include "util.h"

ComponentId TARFILE = registerLogComponent("tarfile");
ComponentId HASHING = registerLogComponent("hashing");

TarFile::TarFile(TarEntry *d, TarContents tc, int n, bool dirs)
{
	in_directory = d;
	tar_contents = tc;
	memset(&mtim_, 0, sizeof(mtim_));
	char c = chartype();
	assert(
			(dirs && tar_contents == DIR_TAR)
					|| (!dirs && tar_contents != DIR_TAR));
	char buffer[256];
	snprintf(buffer, sizeof(buffer), "ta%c%08x.tar", c, n);
        // This is a temporary name! It will be overwritten when the hash is finalized!
	name_ = buffer;
	addVolumeHeader();
}

void TarFile::addEntryLast(TarEntry *entry)
{
	entry->updateMtim(&mtim_);

	entry->registerTarFile(this, current_tar_offset_);
	contents[current_tar_offset_] = entry;
	offsets.push_back(current_tar_offset_);
	debug(TARFILE, "    %s    Added %s at %zu\n", name_.c_str(),
			entry->path()->c_str(), current_tar_offset_);
	current_tar_offset_ += entry->blockedSize();
}

void TarFile::addEntryFirst(TarEntry *entry)
{
	entry->updateMtim(&mtim_);

	entry->registerTarFile(this, 0);
	map<size_t, TarEntry*> newc;
	vector<size_t> newo;
	size_t start = 0;

	if (volume_header_)
	{
		newc[0] = volume_header_;
		newo.push_back(0);
		start = volume_header_->blockedSize();
		newc[start] = entry;
		newo.push_back(start);
		entry->registerTarFile(this, start);
	}
	else
	{
		newc[0] = entry;
		newo.push_back(0);
	}
	for (auto & a : contents)
	{
		if (a.second != volume_header_)
		{
			size_t o = a.first + entry->blockedSize();
			newc[o] = a.second;
			newo.push_back(o);
			a.second->registerTarFile(this, o);
			//fprintf(stderr, "%s Moving %s from %ju to offset %ju\n", name.c_str(), a.second->name.c_str(), a.first, o);
		}
		else
		{
			//fprintf(stderr, "%s Not moving %s from %ju\n", name.c_str(), a.second->name.c_str(), a.first);
		}
	}
	contents = newc;
	offsets = newo;

	debug(TARFILE, "    %s    Added FIRST %s at %zu with blocked size %zu\n",
			name_.c_str(), entry->path()->c_str(), current_tar_offset_,
			entry->blockedSize());
	current_tar_offset_ += entry->blockedSize();

	if (tar_contents == SINGLE_LARGE_FILE_TAR)
	{
		//assert(hash == entry->tarpathHash());
	}
}

void TarFile::addVolumeHeader()
{
	TarEntry *header = TarEntry::newVolumeHeader();
	addEntryLast(header);
	volume_header_ = header;
}

pair<TarEntry*, size_t> TarFile::findTarEntry(size_t offset)
{
	if (offset > size_)
	{
		return pair<TarEntry*, size_t>(NULL, 0);
	}
	debug(TARFILE, "tarfile", "Looking for offset %zu\n", offset);
	size_t o = 0;

	vector<size_t>::iterator i = lower_bound(offsets.begin(), offsets.end(),
			offset, less_equal<size_t>());

	if (i == offsets.end())
	{
		o = *offsets.rbegin();
	}
	else
	{
		i--;
		o = *i;
	}
	TarEntry *te = contents[o];

	debug(TARFILE, "Found it %s\n", te->path()->c_str());
	return pair<TarEntry*, size_t>(te, o);
}

void TarFile::calculateSHA256Hash()
{
	char hash[SHA256_DIGEST_LENGTH];
	SHA256_CTX sha256;
	SHA256_Init(&sha256);

	// SHA256 all headers, including the first Volume label header with
	// empty name and empty checksum.
	for (auto & a : contents)
	{
            size_t len = a.second->headerSize();
            assert(len <= 512 * 5);
            TarEntry *te = a.second;
            debug(HASHING,"   %s %s\n", te->tarpath()->c_str(), te->headerHash().c_str());
            SHA256_Update(&sha256, te->headerHashArray(), SHA256_DIGEST_LENGTH);
	}
	SHA256_Final((unsigned char*) hash, &sha256);
	// Copy the binary hash into the volume header name.
	volume_header_->injectHash(hash, SHA256_DIGEST_LENGTH);

        sha256_hash_ = toHex(hash, SHA256_DIGEST_LENGTH);

        char sizes[32];
        memset(sizes, 0, sizeof(sizes));
        snprintf(sizes, 32, "%zu", size());
        
        char secs_and_nanos[32];
        memset(secs_and_nanos, 0, sizeof(secs_and_nanos));
        snprintf(secs_and_nanos, 32, "%012ju.%09ju", mtim()->tv_sec, mtim()->tv_nsec);
        
	char buffer[256];
	snprintf(buffer, sizeof(buffer), "ta%c_%s_%s_%s.tar", chartype(), sha256_hash_.c_str(), secs_and_nanos, sizes);
	name_ = buffer;

        debug(HASHING,"Tarfile %s\n\n", name_.c_str());
}

string TarFile::line(Path *p)
{
    stringstream ss;

    ss << p->path();
    ss << "/" << name();
    ss << separator << size();

    char secs_and_nanos[32];
    memset(secs_and_nanos, 0, sizeof(secs_and_nanos));
    snprintf(secs_and_nanos, 32, "%012ju.%09ju", mtim()->tv_sec, mtim()->tv_nsec);
    ss << separator << secs_and_nanos;
    
    char datetime[20];
    memset(datetime, 0, sizeof(datetime));
    strftime(datetime, 20, "%Y-%m-%d %H:%M.%S", localtime(&mtim()->tv_sec));
    ss << separator << datetime << endl << separator; 
    
    return ss.str();
}
    

