#!/bin/bash

if [[ ! -d "releases" ]]; then
    echo "No releases to push!"
    echo "Create releases with create-releases.sh"
    exit 1
fi

if [[ $# -ne 1 ]]; then
    echo "Incorrect usage: ./push-releases.sh \"<Insert release notes here>\""
    exit 1
fi

for d in releases/*/ ; do
    version=$(basename "$d")

    gh release delete ${version} --cleanup-tag -y
done

for d in releases/*/ ; do
    version=$(basename "$d")

    gh release create ${version} releases/${version}/release.zip --notes "$1"
done
