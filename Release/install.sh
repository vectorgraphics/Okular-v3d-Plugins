#!/bin/bash

FEDORA_QT5="/usr/lib64/qt5/plugins/okular/generators/"
UBUNTU_QT5="/usr/lib/x86_64-linux-gnu/qt5/plugins/okular/generators/"

install okularGenerator_poppler.so $FEDORA_QT5 2> /dev/null || \
install okularGenerator_poppler.so $UBUNTU_QT5 2> /dev/null || \
echo "Could not find installation path for: okularGenerator_poppler.so"

install okularGenerator_v3d.so $FEDORA_QT5 2> /dev/null || \
install okularGenerator_v3d.so $UBUNTU_QT5 2> /dev/null || \
echo "Could not find installation path for: okularGenerator_v3d.so"

install fragment.spv $FEDORA_QT5 2> /dev/null || \
install fragment.spv $UBUNTU_QT5 2> /dev/null || \
echo "Could not find installation path for: fragment.spv"

install vertex.spv $FEDORA_QT5 2> /dev/null || \
install vertex.spv $UBUNTU_QT5 2> /dev/null || \
echo "Could not find installation path for: vertex.spv"
