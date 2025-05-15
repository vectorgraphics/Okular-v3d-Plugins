#!/bin/bash

FEDORA_QT6="/usr/lib64/qt6/plugins/okular_generators/"
UBUNTU_QT6="/usr/lib/x86_64-linux-gnu/qt6/plugins/okular_generators/"

install okularGenerator_poppler.so $FEDORA_QT6 2> /dev/null || \
install okularGenerator_poppler.so $UBUNTU_QT6 2> /dev/null || \
echo "Could not find installation path for: okularGenerator_poppler.so"

install okularGenerator_v3d.so $FEDORA_QT6 2> /dev/null || \
install okularGenerator_v3d.so $UBUNTU_QT6 2> /dev/null || \
echo "Could not find installation path for: okularGenerator_v3d.so"

install fragment.spv $FEDORA_QT6 2> /dev/null || \
install fragment.spv $UBUNTU_QT6 2> /dev/null || \
echo "Could not find installation path for: fragment.spv"

install vertex.spv $FEDORA_QT6 2> /dev/null || \
install vertex.spv $UBUNTU_QT6 2> /dev/null || \
echo "Could not find installation path for: vertex.spv"

install v3d-mime.xml /usr/share/mime/packages 2> /dev/null || \
echo "Could not find installation path for: v3d-mime.xml"

update-mime-database /usr/share/mime

install okularApplication_v3d.desktop /usr/share/applications/ 2> /dev/null || \
echo "Could not find installation path for: okularApplication_v3d.desktop"
