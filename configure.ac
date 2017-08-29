# Copyright (C) 2017 Fredrik Öhrström
#
# This program is free software: you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation, either version 3 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program.  If not, see <http://www.gnu.org/licenses/>.
# 

AC_PREREQ([2.69])
AC_INIT(BEAK, beak, oehrstroem@gmail.com,,http://nivelleringslikaren.eu/beak)

AC_CANONICAL_BUILD
AC_CANONICAL_HOST

SRC_ROOT="$(pwd -L)"

DEBUG_LEVEL=debug
CONF_NAME="${host_cpu}-${host_vendor}-${host_os}-${DEBUG_LEVEL}"
OUTPUT_ROOT="$SRC_ROOT/build/${CONF_NAME}"

mkdir -p "$OUTPUT_ROOT"
if test ! -d "$OUTPUT_ROOT"; then
    AC_MSG_ERROR([Could not create build directory $OUTPUT_ROOT])
fi

AC_SUBST(CONF_NAME)
AC_SUBST(SRC_ROOT)
AC_SUBST(OUTPUT_ROOT)

AC_SUBST(CC)

AC_PATH_TOOL([CC], [gcc], [:])
AC_PATH_TOOL([CXX], [g++], [:])
AC_PATH_TOOL([LD], [ld], [:])

AC_SUBST(FUSE_CFLAGS)
AC_SUBST(FUSE_LIBS)
if pkg-config fuse; then
   FUSE_CFLAGS=`pkg-config --cflags fuse`
   FUSE_LIBS=`pkg-config --libs fuse`
else
   AC_MSG_ERROR(Please install libfuse-dev)
fi

CXXFLAGS="-O0 -g  -Wall -fmessage-length=0 -std=c++11 -Wno-unused-function \
	-DTARREDFS_VERSION=0.1 -DFUSE_USE_VERSION=26 \
        -Ilibtar/lib -Ilibtar/listhash -I/usr/include ${FUSE_CFLAGS}"

AC_SUBST(CC)
AC_SUBST(CXX)
AC_SUBST(LD)
AC_SUBST(CCFLAGS)
AC_SUBST(CXXFLAGS)
AC_SUBST(LDFLAGS)

AC_MSG_NOTICE([Output root $OUTPUT_ROOT host=$host build=$build])

AC_CONFIG_FILES([$OUTPUT_ROOT/spec.gmk:$SRC_ROOT/spec.gmk.in])
AC_CONFIG_FILES([$OUTPUT_ROOT/Makefile:$SRC_ROOT/Makefile.in])
# Make sure config.status ends up in the build directory instead of the src root.
CONFIG_STATUS="$OUTPUT_ROOT/config.status"
# Write out spec.gmk and build/Makefile
AC_OUTPUT
