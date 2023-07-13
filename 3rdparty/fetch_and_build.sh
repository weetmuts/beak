#!/bin/sh

sudo apt-get install gcc-mingw-w64
sudo apt-get install gcc-arm-linux-gnueabihf

if [ ! -d openssl-1.0.2-winapi ]; then
    echo
    echo Fetching OpenSSL
    echo
    git clone https://github.com/openssl/openssl.git openssl-1.0.2-winapi
    if [ ! -d openssl-1.0.2-arm ]; then
        git clone openssl-1.0.2-winapi openssl-1.0.2-arm
    fi
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

exit 0

cd openssl-1.0.2-arm
if [ ! -f libssl.a ]; then
    echo
    echo Building OpenSSL arm32
    echo

    git checkout OpenSSL_1_0_2-stable
    ./Configure linux-generic32 shared --cross-compile-prefix=arm-linux-gnueabihf-
    make
fi
cd ..

if [ ! -d zlib-1.2.11-winapi ]; then
    echo
    echo Fetching zlib
    echo
    wget https://zlib.net/zlib-1.2.11.tar.gz && tar xzf zlib-1.2.11.tar.gz
    mv zlib-1.2.11 zlib-1.2.11-winapi
fi

if [ ! -d zlib-1.2.11-arm ]; then
    tar xzf zlib-1.2.11.tar.gz
    mv zlib-1.2.11 zlib-1.2.11-arm
fi

cd zlib-1.2.11-winapi
if [ ! -f zlib1.dll ]; then
    echo
    echo Building zlib w64
    echo
    make -f win32/Makefile.gcc SHARED_MODE=1 PREFIX=x86_64-w64-mingw32-
fi
cd ..

cd zlib-1.2.11-arm
if [ ! -f libz.a ]; then
    echo
    echo Building zlib arm32
    echo
    CROSS=arm-linux-gnueabihf- CC=${CROSS}gcc LD=${CROSS}ld AS=${CROSS}as ./configure
    make
fi
cd ..

if [ ! -d libfuse-arm ]; then
    echo
    echo Fetching libfuse arm
    echo
    wget http://http.us.debian.org/debian/pool/main/f/fuse/libfuse-dev_2.9.0-2+deb7u2_armhf.deb
    mkdir -p libfuse-arm
    cd libfuse-arm
    ar x ../libfuse-dev_2.9.0-2+deb7u2_armhf.deb
    tar xzf data.tar.gz
fi

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
    if [ ! -d librsync-arm ]; then
        git clone librsync-winapi librsync-arm
    fi
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
