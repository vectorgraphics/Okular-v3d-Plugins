#!/bin/bash

okularVersion="25.04"
distro="fedora42"
distroboxImage="fedora-toolbox:42"

distroboxName=${distro}"-testingbox"
distroboxHomeDir=${PWD}"/home/"

if [[ $1 = "--clean" ]]; then
    distrobox rm -f ${distroboxName}
    sudo rm -rf home
fi

if [ ! -d "home" ]; then
    mkdir home
    distrobox create -n -Y ${distroboxName} -i ${distroboxImage} --home ${distroboxHomeDir}

    distrobox enter ${distroboxName} -nw -T -e sudo dnf install -y okular
fi

rm -f ~/Okular-v3d-plugins/testing/${distro}/home/okularGenerator_v3d.so
rm -f ~/Okular-v3d-plugins/testing/${distro}/home/okularGenerator_poppler.so

rm -f ~/Okular-v3d-plugins/testing/${distro}/home/vertex.spv
rm -f ~/Okular-v3d-plugins/testing/${distro}/home/fragment.spv

sudo cp ~/Okular-v3d-plugins/releases/${okularVersion}/build/bin/okular_generators/okularGenerator_v3d.so ~/Okular-v3d-plugins/testing/${distro}/home/

sudo cp ~/Okular-v3d-plugins/releases/${okularVersion}/build/bin/okular_generators/okularGenerator_poppler.so ~/Okular-v3d-plugins/testing/${distro}/home/

cp ~/Okular-v3d-plugins/3rdParty/v3d-Common/shaders/vertex.spv ~/Okular-v3d-plugins/testing/${distro}/home/

cp ~/Okular-v3d-plugins/3rdParty/v3d-Common/shaders/fragment.spv ~/Okular-v3d-plugins/testing/${distro}/home/

cp ~/Okular-v3d-plugins/testing/default-home/* ~/Okular-v3d-plugins/testing/${distro}/home/

distrobox enter ${distroboxName} --no-workdir -T -e sudo ./install.sh

echo "Testing the v3d plugin on Okular version "${okularVersion}" on "${distro}" using teapot.v3d"
distrobox enter ${distroboxName} --no-workdir -T -e okular teapot.v3d
echo "Testing the pdf plugin on Okular version "${okularVersion}" on "${distro}" using modelGrid.pdf"
distrobox enter ${distroboxName} --no-workdir -T -e okular modelGrid.pdf
