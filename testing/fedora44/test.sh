#!/bin/bash

okularVersion="26.04"
distro="fedora44"
distroboxImage="fedora:latest"

distroboxName=${distro}"-testingbox"
distroboxHomeDir=${PWD}"/home/"

. ../test-base.sh
testFunc "sudo dnf install -y okular" $@
