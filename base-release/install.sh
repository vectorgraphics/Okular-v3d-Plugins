#!/bin/bash

FEDORA="/usr/lib64/qt6/plugins/okular_generators/"
UBUNTU="/usr/lib/x86_64-linux-gnu/qt6/plugins/okular_generators/"
ARCH="/usr/lib64/qt6/plugins/okular_generators"

mkdir ${FEDORA}tmp/ 2> /dev/null || \
mkdir ${UBUNTU}tmp/ 2> /dev/null || \
mkdir ${ARCH}tmp/ 2> /dev/null

mv ${FEDORA}okularGenerator_poppler.so ${FEDORA}tmp/okularGenerator_poppler.so 2> /dev/null || \
mv ${UBUNTU}okularGenerator_poppler.so ${UBUNTU}tmp/okularGenerator_poppler.so 2> /dev/null || \
mv ${ARCH}okularGenerator_poppler.so ${ARCH}tmp/okularGenerator_poppler.so 2> /dev/null

install okularGenerator_poppler.so ${FEDORA} 2> /dev/null || \
install okularGenerator_poppler.so ${UBUNTU} 2> /dev/null || \
install okularGenerator_poppler.so ${ARCH} 2> /dev/null || \
echo "Could not find installation path for: okularGenerator_poppler.so"

install okularGenerator_v3d.so ${FEDORA} 2> /dev/null || \
install okularGenerator_v3d.so ${UBUNTU} 2> /dev/null || \
install okularGenerator_v3d.so ${ARCH} 2> /dev/null || \
echo "Could not find installation path for: okularGenerator_v3d.so"

# Install libv3dCommon.so alongside the generator plugins. Both generators
# have RUNPATH=$ORIGIN set at build time, so they resolve this relative to
# their own directory automatically.
install libv3dCommon.so ${FEDORA} 2> /dev/null || \
install libv3dCommon.so ${UBUNTU} 2> /dev/null || \
install libv3dCommon.so ${ARCH} 2> /dev/null || \
echo "Could not find installation path for: libv3dCommon.so"

# okularpart (v3d-enabled PresentationWidget/PagePainter) goes into the
# framework parts directory, not the okular_generators directory.
FEDORA_PARTS="/usr/lib64/qt6/plugins/kf6/parts/"
UBUNTU_PARTS="/usr/lib/x86_64-linux-gnu/qt6/plugins/kf6/parts/"
ARCH_PARTS="/usr/lib64/qt6/plugins/kf6/parts"

install okularpart.so ${FEDORA_PARTS} 2> /dev/null || \
install okularpart.so ${UBUNTU_PARTS} 2> /dev/null || \
install okularpart.so ${ARCH_PARTS} 2> /dev/null || \
echo "Could not find installation path for: okularpart.so"

install *.glsl ${FEDORA} 2> /dev/null || \
install *.glsl ${UBUNTU} 2> /dev/null || \
install *.glsl ${ARCH} 2> /dev/null || \
echo "Could not find installation path for shaders"

install v3d-mime.xml /usr/share/mime/packages 2> /dev/null || \
echo "Could not find installation path for: v3d-mime.xml"

update-mime-database /usr/share/mime

install okularApplication_v3d.desktop /usr/share/applications/ 2> /dev/null || \
echo "Could not find installation path for: okularApplication_v3d.desktop"

