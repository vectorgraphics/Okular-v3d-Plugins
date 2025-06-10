#!/bin/bash

FEDORA="/usr/lib64/qt6/plugins/okular_generators/"
UBUNTU="/usr/lib/x86_64-linux-gnu/qt6/plugins/okular_generators/"
ARCH="/usr/lib64/qt6/plugins/okular_generators"

mkdir ${FEDORA}tmp/ 2> /dev/null || \
mkdir ${UBUNTU}tmp/ 2> /dev/null || \
mkdir ${ARCH}tmp/ 2> /dev/null

mv ${FEDORA}okularGenerator_poppler.so ${FEDORA}tmp/okularGenerator_poppler.so 2> /dev/null || \
mv ${UBUNTU}okularGenerator_poppler.so ${UBUNTU}tmp/okularGenerator_poppler.so 2> /dev/null || \
mv ${ARCH}okularGenerator_poppler.so ${ARCH}tmp/okularGenerator_poppler.so 2> /dev/null || \
echo "Could not find existing okularGenerator_poppler.so to save"

install okularGenerator_poppler.so ${FEDORA} 2> /dev/null || \
install okularGenerator_poppler.so ${UBUNTU} 2> /dev/null || \
install okularGenerator_poppler.so ${ARCH} 2> /dev/null || \
echo "Could not find installation path for: okularGenerator_poppler.so"

install okularGenerator_v3d.so ${FEDORA} 2> /dev/null || \
install okularGenerator_v3d.so ${UBUNTU} 2> /dev/null || \
install okularGenerator_v3d.so ${ARCH} 2> /dev/null || \
echo "Could not find installation path for: okularGenerator_v3d.so"

install fragment.spv ${FEDORA} 2> /dev/null || \
install fragment.spv ${UBUNTU} 2> /dev/null || \
install fragment.spv ${ARCH} 2> /dev/null || \
echo "Could not find installation path for: fragment.spv"

install vertex.spv ${FEDORA} 2> /dev/null || \
install vertex.spv ${UBUNTU} 2> /dev/null || \
install vertex.spv ${ARCH} 2> /dev/null || \
echo "Could not find installation path for: vertex.spv"

install v3d-mime.xml /usr/share/mime/packages 2> /dev/null || \
echo "Could not find installation path for: v3d-mime.xml"

update-mime-database /usr/share/mime

install okularApplication_v3d.desktop /usr/share/applications/ 2> /dev/null || \
echo "Could not find installation path for: okularApplication_v3d.desktop"
