#!/bin/bash

okularVersion="24.02"
distro="fedora40-downgrade-okular"
distroboxImage="fedora-toolbox:40"

distroboxName=${distro}"-testingbox"
distroboxHomeDir=${PWD}"/home/"

clean=0
cleanOnly=0
release=0
debug=0

for arg in $@
do
    if [[ $arg = "--clean" ]]; then
        clean=1
    fi

    if [[ $arg = "--clean-only" ]]; then
        cleanOnly=1
    fi

    if [[ $arg = "--release" ]]; then
        release=1
    fi

    if [[ $arg = "--debug" ]]; then
        debug=1
    fi
done

if [[ $clean -eq 1 ]] || [[ $cleanOnly -eq 1 ]]; then
    distrobox rm -f ${distroboxName}
    sudo rm -rf home
fi

if [[ $cleanOnly -eq 1 ]]; then
    exit 0
fi

if [[ $release -eq 0 ]] && [[ $debug -eq 0 ]]; then
    echo "No build type selected, please specify either --debug, --release or both."
    exit 1
fi

if [ ! -d "home" ]; then
    mkdir home
    distrobox create -n -Y ${distroboxName} -i ${distroboxImage} --home ${distroboxHomeDir}

    distrobox enter ${distroboxName} -nw -T -e sudo dnf install -y okular
    distrobox enter ${distroboxName} -nw -T -e sudo dnf downgrade -y okular --allowerasing
fi

if [[ $debug -eq 1 ]]; then
    rm -f ./home/okularGenerator_v3d.so
    rm -f ./home/okularGenerator_poppler.so

    rm -f ./home/vertex.spv
    rm -f ./home/fragment.spv

    sudo cp ../../build/${okularVersion}/build/debug/bin/okular_generators/okularGenerator_v3d.so ./home/

    sudo cp ../../build/${okularVersion}/build/debug/bin/okular_generators/okularGenerator_poppler.so ./home/

    cp ../../3rdParty/v3d-Common/shaders/vertex.spv ./home/

    cp ../../3rdParty/v3d-Common/shaders/fragment.spv ./home/

    cp -r ../../base-release/* ./home/

    distrobox enter ${distroboxName} --no-workdir -T -e sudo ./install.sh

    echo "Testing the v3d plugin on Okular version "${okularVersion}" on "${distro}" using teapot.v3d and debug plugins"
    distrobox enter ${distroboxName} --no-workdir -T -e okular examples/teapot.v3d
    echo "Testing the pdf plugin on Okular version "${okularVersion}" on "${distro}" using modelGrid.pdf and debug plugins"
    distrobox enter ${distroboxName} --no-workdir -T -e okular examples/modelGrid.pdf
fi

if [[ $release -eq 1 ]]; then
    rm -f ./home/okularGenerator_v3d.so
    rm -f ./home/okularGenerator_poppler.so

    rm -f ./home/vertex.spv
    rm -f ./home/fragment.spv

    sudo cp ../../build/${okularVersion}/build/debug/bin/okular_generators/okularGenerator_v3d.so ./home/

    sudo cp ../../build/${okularVersion}/build/debug/bin/okular_generators/okularGenerator_poppler.so ./home/

    cp ../../3rdParty/v3d-Common/shaders/vertex.spv ./home/

    cp ../../3rdParty/v3d-Common/shaders/fragment.spv ./home/

    cp -r ../../base-release/* ./home/

    distrobox enter ${distroboxName} --no-workdir -T -e sudo ./install.sh

    echo "Testing the v3d plugin on Okular version "${okularVersion}" on "${distro}" using teapot.v3d and release plugins"
    distrobox enter ${distroboxName} --no-workdir -T -e okular examples/teapot.v3d
    echo "Testing the pdf plugin on Okular version "${okularVersion}" on "${distro}" using modelGrid.pdf and release plugins"
    distrobox enter ${distroboxName} --no-workdir -T -e okular examples/modelGrid.pdf
fi

