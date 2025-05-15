#!/bin/bash

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
