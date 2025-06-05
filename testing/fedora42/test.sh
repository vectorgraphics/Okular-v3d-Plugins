#!/bin/bash

okularVersion="25.04"
distro="fedora42"
distroboxImage="fedora-toolbox:42"

distroboxName=${distro}"-testingbox"
distroboxHomeDir=${PWD}"/home/"

. ../test-base.sh
testFunc "sudo dnf install -y okular" $@
