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

#ifndef DEFS_H
#define DEFS_H

#define TARREDFS_VERSION "0.1"

#define FUSE_USE_VERSION 26

#define OK 0
#define ERR 1

#define DEFAULT_TARGET_TAR_SIZE 10ull*1024*1024;
#define DEFAULT_TAR_TRIGGER_SIZE 20ull*1024*1024;
#define DEFAULT_SPLIT_TAR_SIZE 100ull*1024*1024;

#endif
