#!/bin/bash

clean=0
cleanOnly=0
release=0
debug=0

for arg in $@
do
    if [[ $arg = "--clean" ]]; then
        clean=1
    fi

    if [[ $arg = "--clean-only" ]]; then
        cleanOnly=1
    fi

    if [[ $arg = "--release" ]]; then
        release=1
    fi

    if [[ $arg = "--debug" ]]; then
        debug=1
    fi
done

if [[ $clean -eq 1 ]] || [[ $cleanOnly -eq 1 ]]; then
    rm -r build/
    rm -r usr/
fi

if [[ $cleanOnly -eq 1 ]]; then
    exit 0
fi

if [[ $release -eq 0 ]] && [[ $debug -eq 0 ]]; then
    echo "No build type selected, please specify either --debug, --release or both."
    exit 1
fi

if [[ $debug -eq 1 ]]; then
    cmake -S okular/ -B build/debug/ -DCMAKE_BUILD_TYPE=Debug --install-prefix $PWD/usr/debug/
    cmake --build build/debug/ --target okularGenerator_v3d
    cmake --build build/debug/ --target okularGenerator_poppler
fi

if [[ $release -eq 1 ]]; then
    cmake -S okular/ -B build/release/ -DCMAKE_BUILD_TYPE=Release --install-prefix $PWD/usr/release
    cmake --build build/release/ --target okularGenerator_v3d
    cmake --build build/release/ --target okularGenerator_poppler
fi

