#!/bin/bash

noBuild=0

for arg in "$@"
do
    if [[ $arg = "--help" ]]; then
        echo "Usage: ./create-releases.sh [OPTIONS]..."
        echo "Creates releases for all versions of Okular supported"
        echo "  --help          See this message"
        echo "  --no-build      Skip the build step; only create release archives from existing builds"
        exit
    fi

    if [[ $arg = "--no-build" ]]; then
        noBuild=1
    fi
done

if [[ $noBuild -eq 0 ]]; then
    okularVersion=$(okular --version 2>/dev/null | grep -oP '\d+\.\d+')

    if [[ -z "$okularVersion" ]]; then
        echo "Warning: Could not detect Okular version. Skipping build step."
        exit 1
    fi

    echo "Detected Okular version: $okularVersion"

    buildScript="build/${okularVersion}/build.sh"

    if [[ ! -f "$buildScript" ]]; then
        echo "Error: No build script found at $buildScript"
        exit 1
    fi

    echo "Building plugins for Okular $okularVersion using $buildScript..."
    (cd "$(dirname "$buildScript")" && bash "./build.sh" --release)

    if [[ $? -ne 0 ]]; then
        echo "Error: Build failed."
        exit 1
    fi

    echo "Build complete."
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
    cp $d/build/release/bin/libv3dCommon.so ./${releaseDir}

    cp ./asymptote/base/shaders/*.glsl ./${releaseDir}

    cd releases/${version}/

    zip -r release.zip release

    cd ../../

done
