#!/bin/bash

FEDORA="/usr/lib64/qt6/plugins/okular_generators/"
UBUNTU="/usr/lib/x86_64-linux-gnu/qt6/plugins/okular_generators/"
ARCH="/usr/lib64/qt6/plugins/okular_generators"

rm ${FEDORA}okularGenerator_poppler.so  2> /dev/null || \
rm ${UBUNTU}okularGenerator_poppler.so 2> /dev/null || \
rm ${ARCH}okularGenerator_poppler.so 2> /dev/null || \
echo "Could not find okularGenerator_poppler.so, was the plugin ever installed?"

rm ${FEDORA}okularGenerator_v3d.so 2> /dev/null || \
rm ${UBUNTU}okularGenerator_v3d.so 2> /dev/null || \
rm ${ARCH}okularGenerator_v3d.so 2> /dev/null || \
echo "Could not find okularGenerator_v3d.so, was the plugin ever installed?"

rm ${FEDORA}fragment.spv 2> /dev/null || \
rm ${UBUNTU}fragment.spv 2> /dev/null || \
rm ${ARCH}fragment.spv 2> /dev/null || \
echo "Could not find fragment.spv, was the plugin ever installed?"

rm ${FEDORA}vertex.spv 2> /dev/null || \
rm ${UBUNTU}vertex.spv 2> /dev/null || \
rm ${ARCH}vertex.spv 2> /dev/null || \
echo "Could not find vertex.spv, was the plugin ever installed?"

rm /usr/share/mime/packages/v3d-mime.xml 2> /dev/null || \
echo "Could not find v3d-mime.xml, was the plugin ever installed?"

update-mime-database /usr/share/mime

rm /usr/share/applications/okularApplication_v3d.desktop  2> /dev/null || \
echo "Could not find okularApplication_v3d.desktop, was the plugin ever installed?"

mv ${FEDORA}tmp/okularGenerator_poppler.so ${FEDORA}okularGenerator_poppler.so 2> /dev/null || \
mv ${UBUNTU}tmp/okularGenerator_poppler.so ${UBUNTU}okularGenerator_poppler.so 2> /dev/null || \
mv ${ARCH}tmp/okularGenerator_poppler.so ${ARCH}okularGenerator_poppler.so 2> /dev/null || \
echo "Could not find existing okularGenerator_poppler.so to save"

rm -r ${FEDORA}tmp/ 2> /dev/null || \
rm -r ${UBUNTU}tmp/ 2> /dev/null || \
rm -r ${ARCH}tmp/ 2> /dev/null || \
echo "Could not find existing okularGenerator_poppler.so to save"
