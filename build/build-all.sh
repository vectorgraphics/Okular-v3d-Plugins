#!/bin/bash

for d in */ ; do
    echo "========== Building version "$(basename ${d})" =========="
    cd $d && sh build.sh $@ && cd ../
done
