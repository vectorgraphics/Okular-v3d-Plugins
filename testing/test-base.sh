testFunc() {
    for arg in $@
    do
        if [[ $arg = "--help" ]]; then
            echo "Usage: ./test.sh [OPTIONS]..."
            echo "Tests Okular version ${okularVersion} using ${distro}"
            echo "  --help          See this message"
            echo "  --clean         Delete and recreate the testing distrobox before testing"
            echo "  --clean-only    Delete the distrobox without testing"
            echo "  --release       Test the release build of the pdf and v3d plugin"
            echo "  --debug         Test the debug build of the pdf and v3d plugin"
            echo "  --install-only  Creates the distrobox if needed, and then installs the plugins without testing"

            exit
        fi
    done

    clean=0
    cleanOnly=0
    release=0
    debug=0
    installOnly=0

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

        if [[ $arg = "--install-only" ]]; then
            installOnly=1
        fi
    done

    if [[ $release -eq 0 ]] && [[ $debug -eq 0 ]]; then
        echo "No build type selected, please specify either --debug, --release or both. See --help for more information"
        exit 1
    fi

    if [[ $clean -eq 1 ]] || [[ $cleanOnly -eq 1 ]]; then
        distrobox rm -f ${distroboxName}
        sudo rm -rf home
    fi

    if [[ $cleanOnly -eq 1 ]]; then
        exit 0
    fi

    if [ ! -d "home" ]; then
        mkdir home
        distrobox create -n -Y ${distroboxName} -i ${distroboxImage} --home ${distroboxHomeDir}

        distrobox enter ${distroboxName} -nw -T -e $1
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

        if [[ $installOnly -eq 0 ]]; then
            echo "Testing the v3d plugin on Okular version "${okularVersion}" on "${distro}" using teapot.v3d and debug plugins"
            distrobox enter ${distroboxName} --no-workdir -T -e okular examples/teapot.v3d
            echo "Testing the pdf plugin on Okular version "${okularVersion}" on "${distro}" using modelGrid.pdf and debug plugins"
            distrobox enter ${distroboxName} --no-workdir -T -e okular examples/modelGrid.pdf
        fi
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

        if [[ $installOnly -eq 0 ]]; then
            echo "Testing the v3d plugin on Okular version "${okularVersion}" on "${distro}" using teapot.v3d and release plugins"
            distrobox enter ${distroboxName} --no-workdir -T -e okular examples/teapot.v3d
            echo "Testing the pdf plugin on Okular version "${okularVersion}" on "${distro}" using modelGrid.pdf and release plugins"
            distrobox enter ${distroboxName} --no-workdir -T -e okular examples/modelGrid.pdf
        fi
    fi
}
