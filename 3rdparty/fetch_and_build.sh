#!/bin/sh

sudo apt-get install gcc-mingw-w64

if [ ! -d openssl-1.0.2-winapi ]; then
    echo
    echo Fetching OpenSSL
    echo
    git clone https://github.com/openssl/openssl.git openssl-1.0.2-winapi
fi

cd openssl-1.0.2-winapi
if [ ! -f ssleay32.dll ]; then
    echo
    echo Building OpenSSL w64
    echo

    git checkout OpenSSL_1_0_2-stable
    ./Configure mingw64 shared --cross-compile-prefix=x86_64-w64-mingw32-
    make
fi
cd ..

if [ ! -d zlib-1.3-winapi ]; then
    echo
    echo Fetching zlib
    echo
    wget https://github.com/madler/zlib/releases/download/v1.3/zlib-1.3.tar.gz && tar xzf zlib-1.3.tar.gz
    mv zlib-1.3 zlib-1.3-winapi
fi

cd zlib-1.3-winapi
if [ ! -f zlib1.dll ]; then
    echo
    echo Building zlib w64
    echo
    make -f win32/Makefile.gcc SHARED_MODE=1 PREFIX=x86_64-w64-mingw32-
fi
cd ..

if [ ! -d winfsp ]; then
    echo
    echo Fetching Winfsp
    echo
    git clone https://github.com/billziss-gh/winfsp
fi

if [ ! -d librsync-winapi ]; then
    echo
    echo Fetching RSync
    echo
    git clone https://github.com/librsync/librsync.git librsync-winapi
fi

cd librsync-winapi
if [ ! -f librsync.dll ]; then
    echo
    echo Building librsync w64
    echo

    MINGW_ARCH=64 cmake -DBUILD_RDIFF=OFF -DCMAKE_INSTALL_PREFIX=/opt/myoutput -DCMAKE_PREFIX_PATH=/usr -DMINGW_PREFIX="/usr" -DCMAKE_INSTALL_LIBDIR=/opt/mingw-w64-x86_64/lib -DENABLE_COMPRESSION=OFF -DCMAKE_BUILD_TYPE=Release -DCMAKE_DISABLE_FIND_PACKAGE_POPT=TRUE -DCMAKE_DISABLE_FIND_PACKAGE_libb2=TRUE -DCMAKE_DISABLE_FIND_PACKAGE_ZLIB=TRUE -DCMAKE_DISABLE_FIND_PACKAGE_BZip2=TRUE -DCMAKE_DISABLE_FIND_PACKAGE_Doxygen=TRUE -DCMAKE_TOOLCHAIN_FILE=../librsync.toolchain.cmake

    make
fi
cd ..
