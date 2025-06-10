#!/bin/bash

for arg in $@
do
    if [[ $arg = "--help" ]]; then
        echo "Usage: ./push-releases.sh <Release Number> <\"Release Notes\"> [OPTIONS]..."
        echo "Pushes already built releases to github using the github CLI"
        echo "Be sure to specify the release number and release notes properly"
        echo "  --help          See this message"

        exit
    fi
done

if [[ ! -d "releases" ]]; then
    echo "No releases to push!"
    echo "Create releases with create-releases.sh"
    exit 1
fi

if [[ $# -ne 2 ]]; then
    echo "Incorrect usage: ./push-releases.sh <Release Number> \"<Insert release notes here>\""
    exit 1
fi

for d in releases/*/ ; do
    version=$(basename "$d")

    gh release create ${version}v${1} releases/${version}/release.zip --notes "$2"
done
