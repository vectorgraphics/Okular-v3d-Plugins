#!/bin/bash

okularVersion="25.04"
distro="arch2025.05"
distroboxImage="archlinux:latest"

distroboxName=${distro}"-testingbox"
distroboxHomeDir=${PWD}"/home/"

. ../test-base.sh
testFunc "sudo pacman -S --noconfirm okular" $@
