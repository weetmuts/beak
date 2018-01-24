#!/bin/sh

echo
echo Fetching and building OpenSSL for x86_64-w64-mingw32-
echo

git clone https://github.com/openssl/openssl.git openssl-1.0.2

pushd openssl-1.0.2
    git checkout OpenSSL_1_0_2-stable
    ./Configure mingw64 shared --cross-compile-prefix=x86_64-w64-mingw32-
popd


echo
echo Fetching and building zlib for x86_64-w64-mingw32-
echo

if [ ! -d zlib-1.2.11 ]; then
    wget https://zlib.net/zlib-1.2.11.tar.gz && tar xzf zlib-1.2.11.tar.gz
fi

pushd zlib-1.2.11
    cd zlib-1.2.11
    make -f win32/Makefile.gcc SHARED_MODE=1 PREFIX=x86_64-w64-mingw32-
popd
