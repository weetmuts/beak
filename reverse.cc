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

#include "reverse.h"

#include <asm-generic/errno-base.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <cassert>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <regex.h>
#include<dirent.h>
#include <sys/stat.h>
#include <sys/types.h>

#include "log.h"
#include "tarentry.h"

using namespace std;

ComponentId REVERSE = registerLogComponent("reverse");

ReverseTarredFS::ReverseTarredFS()
{
	mode_t m = S_IFDIR | S_IRUSR | S_IXUSR;
	entries[Path::lookup("")] = Entry(m, 0, 0, Path::lookup(""));
}

int ReverseTarredFS::parseTarredfsContent(vector<char> &v, string tar_path)
{
	auto i = v.begin();
        auto ii = i;

        bool eof, err;
	string header = eatTo(v, i, separator, 30 * 1024 * 1024, &eof, &err);

	std::vector<char> data(header.begin(), header.end());
	auto j = data.begin();

	string type = eatTo(data, j, '\n', 64, &eof, &err);
	string uid = eatTo(data, j, '\n', 10 * 1024 * 1024, &eof, &err); // Accept up to a ~million uniq uids
	string gid = eatTo(data, j, '\n', 10 * 1024 * 1024, &eof, &err); // Accept up to a ~million uniq gids

	if (type != "#tarredfs 0.1")
	{
            failure(REVERSE,
                    "Type was not \"#tarredfs 0.1\" as expected! It was \"%s\"\n",
                    type.c_str());
		return ERR;
	}

	vector<Entry*> es;

        eof = false;        
	while (i != v.end() && !eof)
	{
            mode_t mode;
            size_t size;
            size_t offset;
            string tar;            
            Path *path;
            string link;
            bool is_sym_link;
            time_t secs;
            time_t nanos;

            ii = i;
            bool got_entry = eatEntry(v, i, tar_path, &mode, &size, &offset,
                                      &tar, &path, &link, &is_sym_link, &secs, &nanos, &eof, &err);
            if (err) {
                failure(REVERSE, "Could not parse tarredfs-contents file in %s\n>%s<\n", tar_path.c_str(), ii);
                break;
            }            
            if (!got_entry) break;
            entries[path] = Entry(mode, size, offset, path);
            Entry *e = &entries[path];
            e->link = link;
            e->is_sym_link = is_sym_link;
            e->secs = secs;
            e->nanos = nanos;
            e->tar = tar;
            es.push_back(e);
	}

	for (auto i : es)
	{
		debug(REVERSE, "FILE %s in dir %s\n",
				i->path->name()->literal().c_str(), i->path->parent()->c_str());
		Path *dir = i->path->parent();
		if (entries.count(dir) == 0)
		{
                    loadCache(dir, 0);
		}
		Entry *d = &entries[dir];
		d->dir.push_back(i);
		d->loaded = true;
	}

	return OK;
}

void ReverseTarredFS::loadTaz(string taz_path, string path)
{
	TAR *t;

	if (tazs.count(taz_path) == 1)
	{
		return;
	}

	if (path.back() != '/')
	{
		path = path + "/";
	}
	int rc = tar_open(&t, taz_path.c_str(), NULL, O_RDONLY, 0, TAR_GNU);
	if (rc)
	{
		debug(REVERSE, "reverse", "Could not open taz %s\n", taz_path.c_str());
		return;
	}

	rc = th_read(t);
	if (rc)
	{
		debug(REVERSE, "Could not read volume header in %s\n",
				taz_path.c_str());
		tar_close(t);
		return;
	}

	if (!TH_ISVOLHDR(t))
	{
		debug(REVERSE, "reverse", "First entry is not a volume header %s\n",
				taz_path.c_str());
		tar_close(t);
		return;
	}

	string n = th_get_pathname(t);
	if (n != "tarredfs")
	{
		debug(REVERSE, "reverse", "Volume header is not tarredfs %s",
				taz_path.c_str());
		tar_close(t);
		return;
	}

	rc = th_read(t);
	if (rc)
	{
		debug(REVERSE, "reverse", "Could not read tarredfs-contents in %s\n",
				taz_path.c_str());
		tar_close(t);
		return;
	}

	if (!TH_ISREG(t))
	{
		debug(REVERSE, "Second entry is not a regular file in %s\n",
				taz_path.c_str());
		tar_close(t);
		return;
	}

	n = th_get_pathname(t);
	if (n != "tarredfs-contents")
	{
		debug(REVERSE, "reverse",
				"Second entry in %s is not tarredfs-contents but %s",
				taz_path.c_str(), n.c_str());
		tar_close(t);
		return;
	}

	size_t size = th_get_size(t);
	vector<char> buf;

	debug(REVERSE, "reverse", "SIZE %ju \n", size);

	char block[T_BLOCKSIZE + 1];
	for (size_t i = 0; i < size; i += T_BLOCKSIZE)
	{
		memset(block, 0, T_BLOCKSIZE + 1);
		ssize_t k = tar_block_read(t, block);

		if (k != T_BLOCKSIZE)
		{
			if (k != -1)
			{
				errno = EINVAL;
			}
			tar_close(t);
			debug(REVERSE, "reverse",
					"Internal error reading block from taz file.");
			return;
		}
		buf.insert(buf.end(), block, block + k);
	}
        buf.resize(size);
	rc = parseTarredfsContent(buf, path);
	if (rc)
	{
		debug(REVERSE, "reverse",
				"Could not parse the tarredfs-contents file in %s\n",
				taz_path.c_str());
		tar_close(t);
		return;
	}

	debug(REVERSE, "reverse", "Found proper taz file! %s\n", taz_path.c_str());
	//char taz_hash[SHA256_DIGEST_LENGTH];
	//char hash[SHA256_DIGEST_LENGTH];

	tazs[taz_path] = Taz(t);

	for (;;)
	{
		rc = th_read(t);
		if (rc)
			break;

		debug(REVERSE, "reverse", "Found %s\n", th_get_pathname(t));
	}

	tar_close(t);
	return;
}

void ReverseTarredFS::loadCache(Path *path, Path *taz)
{
	struct stat sb;
	Path *opath = path;

	debug(REVERSE, "Load cache for >%s<\n", path->c_str());
	// Walk up in the directory structure until a taz file is found.
	for (;;)
	{
		string c;
		debug(REVERSE, "Looking for cache %s\n", taz->c_str());
		int rc = stat(taz->c_str(), &sb);
		if (!rc && S_ISREG(sb.st_mode))
		{
			// Found a taz file!
                        loadTaz(taz->path(), path->path());
			if (entries.count(path) == 1)
			{
				// Success
				debug(REVERSE, "Found %s in taz %s\n", path->c_str(),
						taz->c_str());
				return;
			}
			if (path != opath)
			{
				// The file, if it exists should have been found here. Therefore we
				// conclude that the file does not exist.
				debug(REVERSE, "NOT found %s in taz %s\n", path->c_str(),
						taz->c_str());
				return;
			}
		}
		if (path->isRoot())
		{
			// No taz file found anywhere! This filesystem should not have been mounted!
			debug(REVERSE, "reverse", "No taz found anywhere!\n");
			return;
		}
		// Move up in the directory tree.
		path = path->parent();
	}
	assert(0);
}

int ReverseTarredFS::getattrCB(const char *path_char_string, struct stat *stbuf)
{
	pthread_mutex_lock(&global);

	memset(stbuf, 0, sizeof(struct stat));
	if (path_char_string[0] == '/')
	{
		string path_string = path_char_string;
		Path *path = Path::lookup(path_string);

		if (entries.count(path) == 0)
		{
                    loadCache(path, 0);
		}
		if (entries.count(path) == 0)
		{
			debug(REVERSE, "Could not find %s in any taz file!\n",
					path->c_str());
			goto err;
		}

		Entry &e = entries[path];
		if (e.isDir())
		{
			stbuf->st_mode = e.mode_bits;
			stbuf->st_nlink = 2;
			stbuf->st_size = e.size;
			stbuf->st_uid = geteuid();
			stbuf->st_gid = getegid();
			stbuf->st_mtim.tv_sec = e.secs;
			stbuf->st_mtim.tv_nsec = e.nanos;
			goto ok;
		}

		stbuf->st_mode = e.mode_bits;
		stbuf->st_nlink = 1;
		stbuf->st_size = e.size;
		stbuf->st_uid = geteuid();
		stbuf->st_gid = getegid();
		stbuf->st_mtim.tv_sec = e.secs;
		stbuf->st_mtim.tv_nsec = e.nanos;
		debug(REVERSE, "reverse", "OK\n");
		goto ok;
	}

	err: pthread_mutex_unlock(&global);
	return -ENOENT;

	ok: pthread_mutex_unlock(&global);
	return 0;
}

int ReverseTarredFS::readdirCB(const char *path_char_string, void *buf,
		fuse_fill_dir_t filler, off_t offset, struct fuse_file_info *fi)
{
	pthread_mutex_lock(&global);
	int rc = OK;

	if (path_char_string[0] == '/')
	{
		string path_string = path_char_string;
		Path *path = Path::lookup(path_string);

		if (entries.count(path) == 0)
		{
                    loadCache(path, 0);
		}
		if (entries.count(path) == 0)
		{
			debug(REVERSE, "Could not find %s in any taz file!\n",
					path->c_str());
			goto err;
		}

		Entry &e = entries[path];
		if (!e.loaded)
		{
                    loadCache(path, 0);
		}
		if (e.isDir())
		{
			filler(buf, ".", NULL, 0);
			filler(buf, "..", NULL, 0);
			for (auto i : e.dir)
			{
				char filename[256];
				memset(filename, 0, 256);
				snprintf(filename, 255, "%s", i->path->name()->c_str());
				filler(buf, filename, NULL, 0);
				debug(REVERSE, "Reading dir line >%s<\n",
						i->path->name()->c_str());
			}
		}
	}
	else
	{
		rc = -ENOENT;
	}

	err: pthread_mutex_unlock(&global);
	return rc;
}

int ReverseTarredFS::readlinkCB(const char *path_char_string, char *buf,
		size_t s)
{
	pthread_mutex_lock(&global);

	debug(REVERSE, "readlinkCB >%s< bufsiz=%ju\n", path_char_string, s);
	if (path_char_string[0] == '/')
	{
		string path_string = path_char_string;
		Path *path = Path::lookup(path_string);

		if (entries.count(path) == 0)
		{
                    loadCache(path, 0);
		}
		if (entries.count(path) == 0)
		{
			debug(REVERSE, "Could not find %s in any taz file!\n",
					path->c_str());
			goto err;
		}

		Entry &e = entries[path];
		size_t c = e.link.length();

		if (c > s)
		{
			c = s;
		}
		memcpy(buf, e.link.c_str(), c);
		buf[c] = 0;
		debug(REVERSE, "readlinkCB >%s< bufsiz=%ju returns buf=>%s<\n", path, s,
				buf);

		goto ok;
	}

	err: pthread_mutex_unlock(&global);
	return -ENOENT;

	ok: pthread_mutex_unlock(&global);
	return 0;
}

int ReverseTarredFS::readCB(const char *path_char_string, char *buf,
		size_t size, off_t offset_, struct fuse_file_info *fi)
{
	assert(path_char_string[0] == '/');
	assert(offset_ >= 0);

	try
	{
		pthread_mutex_lock(&global);

		size_t offset = (size_t) offset_;
		int rc = 0;
		string path_string = path_char_string;
		Path *path = Path::lookup(path_string);
		int fd;
		ssize_t l;

		if (entries.count(path) == 0)
		{
                    loadCache(path, 0);
		}
		if (entries.count(path) == 0)
		{
			debug(REVERSE, "Could not find %s in any taz file!\n",
					path->c_str());
			throw;
		}

		Entry &e = entries[path];

		string tar = root_dir + e.tar;

		if (offset > e.size)
		{
			// Read outside of file size
			rc = 0;
			goto ok;
		}

		if (offset + size > e.size)
		{
			// Shrink actual read to fit file.
			size = e.size - offset;
		}

		// Offset into tar file.
		offset += e.offset;

		fd = open(tar.c_str(), O_RDONLY);
		if (fd == -1)
		{
			failure(REVERSE,
					"Could not open file >%s< in underlying filesystem err %d",
					tar.c_str(), errno);
			throw;
		}
		debug(REVERSE, "reverse",
				"Reading %ju bytes from offset %ju in file %s\n", size, offset,
				tar.c_str());
		l = pread(fd, buf, size, offset);
		close(fd);
		if (l == -1)
		{
			failure(REVERSE,
					"Could not read from file >%s< in underlying filesystem err %d",
					tar.c_str(), errno);
			throw;
		}
		rc = l;
		ok: pthread_mutex_unlock(&global);
		return rc;
	} catch (...)
	{
		pthread_mutex_unlock(&global);
		return -ENOENT;
	}
}


void ReverseTarredFS::checkVersions(Path *path, vector<string> *versions)
{
    regex_t re;
    int rc = regcomp(&re, "taz_[0-9a-z]+_[0-9]+\\.[0-9]+_[0-9]+\\.tar", REG_EXTENDED | REG_NOSUB);
    assert(!rc);
    
    DIR *dp = NULL;
    struct dirent *dptr = NULL;

    if (NULL == (dp = opendir(path->c_str())))
    {
        return;
    }
    while(NULL != (dptr = readdir(dp)) )
    {
        int miss = regexec(&re, dptr->d_name, (size_t) 0, NULL, 0);
        if (!miss) {
            versions->push_back(dptr->d_name);
        }
    }
    closedir(dp);
}

void ReverseTarredFS::setGeneration(string g) {
    generation_ = g;
}




