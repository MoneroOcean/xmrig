#!/bin/sh -e

OPENSSL_VERSION="1.1.1u"
OPENSSL_TAG="OpenSSL_1_1_1u"

mkdir -p deps
mkdir -p deps/include
mkdir -p deps/lib

mkdir -p build && cd build

wget https://github.com/openssl/openssl/releases/download/${OPENSSL_TAG}/openssl-${OPENSSL_VERSION}.tar.gz -O openssl-${OPENSSL_VERSION}.tar.gz
tar -xzf openssl-${OPENSSL_VERSION}.tar.gz

cd openssl-${OPENSSL_VERSION}
./config -no-shared -no-asm -no-zlib -no-comp -no-dgram -no-filenames -no-cms no-tests
make build_libs -j$(nproc || sysctl -n hw.ncpu || sysctl -n hw.logicalcpu)
cp -fr include ../../deps
cp libcrypto.a ../../deps/lib
cp libssl.a ../../deps/lib
cd ..
