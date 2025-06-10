#!/bin/bash

echo "It is highly recomended to execute this script inside of a fedora40 vm/distrobox with all of the qt6 packages downgraded in order to ensure the highest level of compatibility"

for d in */ ; do
    echo "========== Building version "$(basename ${d})" =========="
    cd $d && sh build.sh $@ && cd ../
done
