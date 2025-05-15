#!/bin/bash

if [[ $1 = "--clean" ]]; then
    rm -r build/
fi

cmake -S okular/ -B build/ --install-prefix $PWD/usr
cmake --build build/ --target okularGenerator_v3d
cmake --build build/ --target okularGenerator_poppler
