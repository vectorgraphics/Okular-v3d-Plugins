#!/bin/bash

okularVersion="24.08"
distro="fedora41-downgrade-okular"
distroboxImage="fedora-toolbox:41"

distroboxName=${distro}"-testingbox"
distroboxHomeDir=${PWD}"/home/"

. ../test-base.sh
testFunc "sudo dnf install -y okular-24.08.2" $@
