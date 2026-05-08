#!/bin/sh -e

HWLOC_VERSION="1.11.13"

mkdir -p deps
mkdir -p deps/include
mkdir -p deps/lib

mkdir -p build && cd build

wget https://download.open-mpi.org/release/hwloc/v1.11/hwloc-${HWLOC_VERSION}.tar.gz -O hwloc-${HWLOC_VERSION}.tar.gz
tar -xzf hwloc-${HWLOC_VERSION}.tar.gz

cd hwloc-${HWLOC_VERSION}
./configure --disable-shared --enable-static --disable-libudev --disable-libxml2 --disable-cairo --disable-pci --disable-opencl --disable-cuda --disable-nvml --disable-libnuma
make -C src -j$(nproc || sysctl -n hw.ncpu || sysctl -n hw.logicalcpu)
cp -fr include ../../deps
cp src/.libs/libhwloc.a ../../deps/lib
cd ..
