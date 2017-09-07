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

CONF_NAME="${host_cpu}-${host_vendor}-${host_os}"
OUTPUT_ROOT="$SRC_ROOT/build/${CONF_NAME}"

AC_CONFIG_HEADERS(${OUTPUT_ROOT}/config.h:config.h.in)

AC_SUBST(PLATFORM)
AC_SUBST(PLATFORM_CFLAGS)
AC_SUBST(PATH_SEP)
if test "${host_vendor}" = "w64"; then
   PLATFORM=WINAPI
   PATH_SEP=';'
   PLATFORM_CFLAGS=""
   FUSE_DEFAULT="no"
else
   PLATFORM=POSIX
   PATH_SEP=':'
   PLATFORM_CFLAGS="-DUSE_SYSLOG"
   FUSE_DEFAULT="yes"
fi

mkdir -p "$OUTPUT_ROOT"
if test ! -d "$OUTPUT_ROOT"; then
    AC_MSG_ERROR([Could not create build directory $OUTPUT_ROOT])
fi

AC_SUBST(CONF_NAME)
AC_SUBST(SRC_ROOT)
AC_SUBST(OUTPUT_ROOT)

AC_HEADER_STDC
AC_HEADER_STAT
AC_HEADER_TIME

AC_SUBST(CC)

AC_PATH_TOOL([CC], [gcc], [:])
AC_PATH_TOOL([CXX], [g++], [:])
AC_PATH_TOOL([LD], [ld], [:])

AC_MSG_CHECKING([for fuse])

AC_ARG_ENABLE([fuse],
  [AS_HELP_STRING([--disable-fuse],
      [disable libfuse (mount will not work) @<:@enabled@:>@])],
      [ENABLE_FUSE=${enable_fuse}], [ENABLE_FUSE=${FUSE_DEFAULT}])

AC_SUBST(ENABLE_FUSE)
AC_SUBST(FUSE_CFLAGS)
AC_SUBST(FUSE_LIBS)

if test "x$ENABLE_FUSE" = xyes; then
    if pkg-config fuse; then
       FUSE_CFLAGS="-DFUSE_USE_VERSION=26 $(pkg-config --cflags fuse)"
       FUSE_LIBS="$(pkg-config --libs fuse)"
    else
       AC_MSG_ERROR(Please install libfuse-dev)
    fi
    AC_MSG_RESULT([found])    
else
    FUSE_CFLAGS=""
    FUSE_LIBS=""
    AC_MSG_RESULT([explicitly disabled])
fi

AC_MSG_CHECKING([for openssl])
AC_ARG_WITH(openssl, [AS_HELP_STRING([--with-openssl],
      [specify prefix directory for the openssl package
      (expecting the libraries under PATH and the headers under PATH/include)])])

#x${with_openssl}
#x86_64-w64-mingw32-g++ -I../openssl-1.0.2l/include/ test.cc ../openssl-1.0.2l/libcrypto.a

if test "x${with_openssl}" = "x"; then
    OPENSSL_CFLAGS="$(pkg-config openssl --cflags)"
    OPENSSL_LIBS="$(pkg-config openssl --libs)"
    AC_MSG_RESULT([found])
else
    with_openssl=$(realpath ${with_openssl})
    OPENSSL_CFLAGS="-I${with_openssl}/include"
    OPENSSL_LIBS="${with_openssl}/libcrypto.a"
    AC_MSG_RESULT([using ${with_openssl}])
fi

AC_SUBST(OPENSSL_CFLAGS)
AC_SUBST(OPENSSL_LIBS)

AC_MSG_CHECKING([for zlib])
AC_ARG_WITH(zlib, [AS_HELP_STRING([--with-zlib],
      [specify prefix directory for the zlib package
      (expecting the libraries under PATH and the headers under PATH)])])

if test "x${with_zlib}" = "x"; then
    ZLIB_CFLAGS="$(pkg-config zlib --cflags)"
    ZLIB_LIBS="$(pkg-config zlib --libs)"
    AC_MSG_RESULT([found])    
else
    with_zlib=$(realpath ${with_zlib})
    ZLIB_CFLAGS="-I${with_zlib}"
    ZLIB_LIBS="${with_zlib}/libz.a"
    AC_MSG_RESULT([using ${with_zlib}])
fi

AC_SUBST(ZLIB_CFLAGS)
AC_SUBST(ZLIB_LIBS)

AC_MSG_CHECKING(for st_mtim.tv_nsec in struct stat)
AC_COMPILE_IFELSE(
[AC_LANG_PROGRAM([#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>],
[struct stat sb; sb.st_mtim.tv_nsec = 0;])],
have_st_mtim=yes, have_st_mtim=no)
AC_MSG_RESULT($have_st_mtim)

AC_DEFINE_UNQUOTED(ST_MTIM,$have_st_mtim,Stat structure has st_mtim)

AC_MSG_CHECKING(for st_mtime in struct stat)
AC_COMPILE_IFELSE(
[AC_LANG_PROGRAM([#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>],
[struct stat sb; sb.st_mtime = 0;])],
have_st_mtime=yes, have_st_mtime=no)
AC_MSG_RESULT($have_st_mtime)

AC_DEFINE_UNQUOTED(ST_MTIME,$have_st_mtime,Stat structure has st_mtime)

CXXFLAGS="-D${PLATFORM} -Wall -fmessage-length=0 -std=c++11 -Wno-unused-function -DTARREDFS_VERSION=0.1" 

CXXFLAGS_DEBUG="-O0 -g" 
CXXFLAGS_RELEASE="-Os"

LDFLAGS=""
LDFLAGS_DEBUG="-g"
LDFLAGS_RELEASE="-Os"

CFLAGS=""

AC_SUBST(CC)
AC_SUBST(CXX)
AC_SUBST(LD)
AC_SUBST(CFLAGS)
AC_SUBST(CXXFLAGS)
AC_SUBST(CXXFLAGS_DEBUG)
AC_SUBST(CXXFLAGS_RELEASE)
AC_SUBST(LDFLAGS)
AC_SUBST(LDFLAGS_DEBUG)
AC_SUBST(LDFLAGS_RELEASE)

AC_CONFIG_FILES([$OUTPUT_ROOT/spec.gmk:$SRC_ROOT/spec.gmk.in])
AC_CONFIG_FILES([$OUTPUT_ROOT/Makefile:$SRC_ROOT/Makefile.in])
# Make sure config.status ends up in the build directory instead of the src root.
CONFIG_STATUS="$OUTPUT_ROOT/config.status"
# Write out spec.gmk and build/Makefile
AC_OUTPUT
