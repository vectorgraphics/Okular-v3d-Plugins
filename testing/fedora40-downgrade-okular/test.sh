#!/bin/bash

okularVersion="24.02"
distro="fedora40-downgrade-okular"
distroboxImage="fedora-toolbox:40"

distroboxName=${distro}"-testingbox"
distroboxHomeDir=${PWD}"/home/"

. ../test-base.sh
testFunc "sudo dnf install -y okular-24.02.1" $@
