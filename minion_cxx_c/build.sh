#!/bin/sh

rm -rf build/release

mkdir -p build/release

if [ ! -d build/data ]; then
    ln -s ../../data build/data
fi

cd build/release

cmake ../..

make
