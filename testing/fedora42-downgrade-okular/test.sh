#!/bin/bash

okularVersion="24.12"
distro="fedora42-downgrade-okular"
distroboxImage="fedora-toolbox:42"

distroboxName=${distro}"-testingbox"
distroboxHomeDir=${PWD}"/home/"

. ../test-base.sh
testFunc "sudo dnf install -y okular-24.12.3" $@
