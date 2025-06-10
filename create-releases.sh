#!/bin/bash

if [[ $1 = "--help" ]]; then
    echo "Usage: ./create-releases.sh"
    echo "Creates releases for all versions of Okular supported"
    echo "Be sure to buid plugins prior to creating releases"

    exit
fi

rm -rf releases

mkdir releases/

for d in build/*/ ; do
    version=$(basename "$d")

    mkdir releases/${version}/
    mkdir releases/${version}/release/

    releaseDir=releases/${version}/release/

    cp -r base-release/* ./${releaseDir}

    cp $d/build/release/bin/okular_generators/okularGenerator_v3d.so ./${releaseDir}
    cp $d/build/release/bin/okular_generators/okularGenerator_poppler.so ./${releaseDir}

    cp ./3rdParty/v3d-Common/shaders/vertex.spv ./${releaseDir}
    cp ./3rdParty/v3d-Common/shaders/fragment.spv ./${releaseDir}

    cd releases/${version}/

    zip -r release.zip release

    cd ../../

done
