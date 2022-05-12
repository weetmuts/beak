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
AC_CONFIG_AUX_DIR([autoconf])

AC_CANONICAL_BUILD
AC_CANONICAL_HOST

SRC_ROOT="$(pwd -L)"

CONF_NAME="${host_cpu}-${host_vendor}-${host_os}"
BUILD_ROOT="$SRC_ROOT/build"
OUTPUT_ROOT="$SRC_ROOT/build/${CONF_NAME}"

AC_CONFIG_HEADERS(${OUTPUT_ROOT}/config.h:autoconf/config.h.in)

AC_SUBST(PLATFORM)
AC_SUBST(PLATFORM_CFLAGS)
AC_SUBST(PATH_SEP)
AC_SUBST(CONF_MNEMONIC)

if test "${host_vendor}" = "w64"; then
   PLATFORM=WINAPI
   PATH_SEP=';'
   PLATFORM_CFLAGS=""
   FUSE_DEFAULT="no"
   MEDIA_DEFAULT="no"
   ASAN_DEFALUT="no"
   CONF_MNEMONIC="winapi64"
   MNEMONIC="WINAPI64"
else
   PLATFORM=POSIX
   PATH_SEP=':'
   PLATFORM_CFLAGS="-DUSE_SYSLOG"
   FUSE_DEFAULT="yes"
   MEDIA_DEFAULT="yes"
   ASAN_DEFAULT="yes"
   if test "${host_cpu}" = "arm"; then
       CONF_MNEMONIC="arm32"
       MNEMONIC="ARM32"
   fi
   if test "${host_vendor}" = "apple"; then
       CONF_MNEMONIC="osx64"
       MNEMONIC="OSX64"
   fi
   if test "${host_vendor}" = "pc"; then
       CONF_MNEMONIC="linux64"
       MNEMONIC="LINUX64"
   fi
fi

PKG_CONFIG="$(which pkg-config)"
echo PKG_CONFIG=$PKG_CONFIG
if test "$PKG_CONFIG" = ""; then
    PKG_CONFIG=/bin/false
    AC_MSG_ERROR([You need pkg-config to build. Please install.])
fi

mkdir -p "$OUTPUT_ROOT"
if test ! -d "$OUTPUT_ROOT"; then
    AC_MSG_ERROR([Could not create build directory $OUTPUT_ROOT])
fi

AC_SUBST(CONF_NAME)
AC_SUBST(SRC_ROOT)
AC_SUBST(OUTPUT_ROOT)
AC_SUBST(BUILD_ROOT)

AC_HEADER_STDC
AC_HEADER_STAT
AC_HEADER_TIME

if test "x$CC" = "x"; then
    AC_PATH_TOOL([CC], [gcc], [:])
fi
if test "x$CXX" = "x"; then
    AC_PATH_TOOL([CXX], [g++], [:])
fi
if test "x$LD" = "x"; then
    AC_PATH_TOOL([LD], [ld], [:])
fi
if test "x$STRIP" = "x"; then
    AC_PATH_TOOL([STRIP], [strip], [:])
fi

AC_MSG_CHECKING([for address/leak sanitizer and code coverage])

AC_ARG_ENABLE([asan],
  [AS_HELP_STRING([--disable-asan-gcov],
      [disable address sanitizer and code coverage for debug builds @<:@enabled@:>@])],
      [ENABLE_ASAN=${enable_asan}], [ENABLE_ASAN=${ASAN_DEFAULT}])

if test "x$ENABLE_ASAN" = xyes; then
    ASAN_CFLAGS="-O0 -ggdb -fsanitize=address -fno-omit-frame-pointer -fprofile-arcs -ftest-coverage"
    ASAN_LDFLAGS=""
    ASAN_LIBS="-lasan -lgcov --coverage"
    AC_MSG_RESULT([using ${with_asan}])
else
    AC_MSG_RESULT([explicitly disabled])
fi

AC_SUBST(ENABLE_ASAN)
AC_SUBST(ASAN_CFLAGS)
AC_SUBST(ASAN_LDFLAGS)
AC_SUBST(ASAN_LIBS)

AC_MSG_CHECKING([for fuse])

AC_ARG_ENABLE([fuse],
  [AS_HELP_STRING([--disable-fuse],
      [disable libfuse (mount will not work) @<:@enabled@:>@])],
      [ENABLE_FUSE=${enable_fuse}], [ENABLE_FUSE=${FUSE_DEFAULT}])

AC_SUBST(ENABLE_FUSE)
AC_SUBST(FUSE_CFLAGS)
AC_SUBST(FUSE_LIBS)

AC_ARG_WITH(fuse, [AS_HELP_STRING([--with-fuse],
      [specify prefix directory for the libfuse dev package
      (expecting the libraries under PATH and the headers under PATH/include)])])

if test "x$ENABLE_FUSE" = xyes; then
   if test "x${with_fuse}" = "x"; then
       if $PKG_CONFIG fuse; then
           FUSE_CFLAGS="-DFUSE_USE_VERSION=26 $($PKG_CONFIG --cflags fuse)"
           FUSE_LIBS="$($PKG_CONFIG --libs fuse)"
           AC_MSG_RESULT([found])
       else
           FUSE_CFLAGS=""
           FUSE_LIBS=""
           AC_MSG_RESULT([implicitly disabled])
           AC_MSG_RESULT([Please install fuse-dev, for example using: sudo apt install libfuse-dev])
       fi
   else
       with_fuse=$(realpath ${with_fuse})
       if test ! -d "$with_fuse" ; then
           AC_MSG_ERROR([No such directory "$with_fuse"])
       fi
       FUSE_CFLAGS="-DFUSE_USE_VERSION=26 -D_FILE_OFFSET_BITS=64 -I${with_fuse}/include"
       FUSE_LIBS="$(find "${with_fuse}" -name libfuse.a) -ldl"
       AC_MSG_RESULT([using ${with_fuse}])
   fi
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
    OPENSSL_CFLAGS="$($PKG_CONFIG openssl --cflags)"
    OPENSSL_LIBS="$($PKG_CONFIG openssl --libs)"
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
    ZLIB_CFLAGS="$($PKG_CONFIG zlib --cflags)"
    ZLIB_LIBS="$($PKG_CONFIG zlib --libs)"
    AC_MSG_RESULT([found])
else
    with_zlib=$(realpath ${with_zlib})
    ZLIB_CFLAGS="-I${with_zlib}"
    ZLIB_LIBS="${with_zlib}/libz.a"
    AC_MSG_RESULT([using ${with_zlib}])
fi

AC_SUBST(ZLIB_CFLAGS)
AC_SUBST(ZLIB_LIBS)

AC_MSG_CHECKING([for librsync])
AC_ARG_WITH(librsync, [AS_HELP_STRING([--with-librsync],
      [specify prefix directory for the librsync package
      (expecting the libraries under PATH and the headers under PATH/src)])])

if test "x${with_librsync}" = "x"; then
    LIBRSYNC_CFLAGS=""
    LIBRSYNC_LIBS="-lrsync"
    AC_MSG_RESULT([found])
else
    with_librsync=$(realpath ${with_librsync})
    LIBRSYNC_CFLAGS="-I${with_librsync}/src"
    LIBRSYNC_LIBS="${with_librsync}/librsync.dll.a"
    AC_MSG_RESULT([using ${with_librsync}])
fi


AC_SUBST(LIBRSYNC_CFLAGS)
AC_SUBST(LIBRSYNC_LIBS)

AC_MSG_CHECKING([for exiv2])
AC_ARG_WITH(libexiv2, [AS_HELP_STRING([--with-exiv2],
      [specify prefix directory for the exiv2 package
      (expecting the libraries under PATH and the headers under PATH/src)])])

if test "x${with_libexiv2}" = "x"; then
    LIBEXIV2_CFLAGS=""
    LIBEXIV2_LIBS="-lexiv2"
    AC_MSG_RESULT([found])
fi

AC_MSG_CHECKING([for media support])

AC_ARG_ENABLE([media],
  [AS_HELP_STRING([--disable-media],
      [disable importmedia @<:@enabled@:>@])],
      [ENABLE_MEDIA=${enable_media}], [ENABLE_MEDIA=${MEDIA_DEFAULT}])

AC_SUBST(ENABLE_MEDIA)
AC_SUBST(MEDIA_CFLAGS)
AC_SUBST(MEDIA_LIBS)

AC_MSG_CHECKING([for libmagick++-dev])

MEDIA_CFLAGS=
MEDIA_LIBS=$(Magick++-config --libs)

AC_SUBST(LIBEXIV2_CFLAGS)
AC_SUBST(LIBEXIV2_LIBS)

AC_MSG_CHECKING([for libavformat])
AC_ARG_WITH(libavformat, [AS_HELP_STRING([--with-libavformat],
      [specify prefix directory for the libavformat package
      (expecting the libraries under PATH and the headers under PATH/src)])])

if test "x${with_libavformat}" = "x"; then
    LIBAVFORMAT_CFLAGS=""
    LIBAVFORMAT_LIBS="-lavutil -lavformat -lavcodec"
    AC_MSG_RESULT([found])
fi

AC_SUBST(LIBAVFORMAT_CFLAGS)
AC_SUBST(LIBAVFORMAT_LIBS)

AC_MSG_CHECKING(for sizeof time_t)
AC_CHECK_SIZEOF([time_t])
AC_CHECK_SIZEOF([size_t])

AC_MSG_CHECKING(for st_mtim.tv_nsec in struct stat)
AC_COMPILE_IFELSE(
[AC_LANG_PROGRAM([#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>],
[struct stat sb; sb.st_mtim.tv_nsec = 0;])],
have_st_mtim=yes, have_st_mtim=no)
AC_MSG_RESULT($have_st_mtim)

if test "x${have_st_mtim}" = "xyes"; then
    AC_DEFINE(HAS_ST_MTIM,1,Stat structure has st_mtim)
fi

AC_MSG_CHECKING(for st_mtime in struct stat)
AC_COMPILE_IFELSE(
[AC_LANG_PROGRAM([#include <time.h>
#include <sys/time.h>
#include <sys/stat.h>],
[struct stat sb; sb.st_mtime = 0;])],
have_st_mtime=yes, have_st_mtime=no)
AC_MSG_RESULT($have_st_mtime)

if test "x${have_st_mtime}" = "xyes"; then
    AC_DEFINE(HAS_ST_MTIME,1,Stat structure has st_mtime)
fi

AC_LANG_PUSH([C++])
AC_COMPILE_IFELSE(
[AC_LANG_PROGRAM([], [[
#ifndef __clang__
       not clang
#endif
]])],
[CLANG=yes], [CLANG=no])
AC_LANG_POP([C++])

AC_SUBST([CLANG])

GCC_EXTRA_CXXFLAGS=""
if test "x$CLANG" = xyes; then
    AC_MSG_RESULT([checking type of compiler: using clang])
    GCC_EXTRA_CXXFLAGS=""
else
    AC_MSG_RESULT([checking type of compiler: using g++])
    GCC_EXTRA_CXXFLAGS="-Wno-maybe-uninitialized"
fi

CXXFLAGS="-DPLATFORM_${PLATFORM} -D${MNEMONIC} -Wall -Wno-error=deprecated-declarations -fmessage-length=0 -std=c++11 $GCC_EXTRA_CXXFLAGS -Wno-unused-function"

CXXFLAGS_debug="-O0 -g ${ASAN_CFLAGS}
CXXFLAGS_release="-Os

LDFLAGS=""
LDFLAGS_debug="-g ${ASAN_LDFLAGS}"
LDFLAGS_release="-Os"
LDFLAGSEND_debug=""
LDFLAGSBEGIN_debug="${ASAN_LIBS}"

CFLAGS=""

AC_SUBST(CC)
AC_SUBST(CXX)
AC_SUBST(LD)
AC_SUBST(CFLAGS)
AC_SUBST(CXXFLAGS)
AC_SUBST(CXXFLAGS_debug)
AC_SUBST(CXXFLAGS_release)
AC_SUBST(LDFLAGS)
AC_SUBST(LDFLAGS_debug)
AC_SUBST(LDFLAGSBEGIN_debug)
AC_SUBST(LDFLAGSEND_debug)
AC_SUBST(LDFLAGS_release)
AC_SUBST(LDFLAGSBEGIN_release)
AC_SUBST(LDFLAGSEND_release)

AC_CONFIG_FILES([$OUTPUT_ROOT/spec.gmk:$SRC_ROOT/autoconf/spec.gmk.in])
AC_CONFIG_FILES([$OUTPUT_ROOT/Makefile:$SRC_ROOT/make/Makefile.in])
# Make sure config.status ends up in the build directory instead of the src root.
CONFIG_STATUS="$OUTPUT_ROOT/config.status"
# Write out spec.gmk and build/Makefile
AC_OUTPUT
