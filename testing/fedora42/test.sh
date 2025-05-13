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
fi

distrobox enter ${distroboxName} -nw -T -e sudo dnf install -y okular

rm -f ~/Okular-v3d-plugins/testing/${distro}/home/okularGenerator_v3d.so

sudo cp ~/Okular-v3d-plugins/releases/${okularVersion}/build/bin/okular_generators/okularGenerator_v3d.so ~/Okular-v3d-plugins/testing/${distro}/home/

cp ~/Okular-v3d-plugins/testing/default-home/* ~/Okular-v3d-plugins/testing/${distro}/home/

distrobox enter ${distroboxName} --no-workdir -T -e pwd
distrobox enter ${distroboxName} --no-workdir -T -e sudo ./install.sh
distrobox enter ${distroboxName} --no-workdir -T -e pwd

echo "Testing okular version "${okularVersion}" on "${distro}" using teapot.v3d"
distrobox enter ${distroboxName} --no-workdir -T -e okular teapot.v3d
