#!/bin/bash

rm -f release.zip

cp 3rdParty/v3d-Common/shaders/vertex.spv Release/
cp 3rdParty/v3d-Common/shaders/fragment.spv Release/
cp ../../../../build/okular/bin/okular_generators/okularGenerator_poppler.so Release/
cp ../../../../build/okular/bin/okular_generators/okularGenerator_v3d.so Release/

zip -r release.zip                          \
    Release/vertex.spv                      \
    Release/fragment.spv                    \
    Release/okularGenerator_poppler.so      \
    Release/okularGenerator_v3d.so          \
    Release/install.sh                      \
    Release/examples
