#!/bin/bash

if [[ $1 = "--clean" ]] || [[ $1 = "--clean-only" ]]; then
    rm -r build/
fi

if [[ $1 = "--clean-only" ]]; then
    exit 0
fi

cmake -S okular/ -B build/ --install-prefix $PWD/usr
cmake --build build/ --target okularGenerator_v3d
cmake --build build/ --target okularGenerator_poppler
