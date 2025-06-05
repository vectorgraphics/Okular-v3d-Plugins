#!/bin/bash

okularVersion="24.05"
distro="ubuntu24.10"
distroboxImage="ubuntu:24.10"

distroboxName=${distro}"-testingbox"
distroboxHomeDir=${PWD}"/home/"

. ../test-base.sh
testFunc "sudo apt install -y okular" $@
