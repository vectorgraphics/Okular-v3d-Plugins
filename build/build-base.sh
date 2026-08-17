#!/bin/bash

buildFunc() {
    for arg in $@
    do
        if [[ $arg = "--help" ]]; then
            echo "Usage: ./build.sh [OPTIONS]..."
            echo "Builds the version of Okular corosponding to the current directory"
            echo "  --help          See this message"
            echo "  --clean         Deletes all existing build files before building"
            echo "  --clean-only    Deletes all existing build files"
            echo "  --release       Builds the release version of the plugins"
            echo "  --debug         Builds the debug version of the plugins"
            echo "  --install       Build then run the install script from the stage directory"

            exit
        fi
    done

    clean=0
    cleanOnly=0
    release=0
    debug=0
    install=0

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

        if [[ $arg = "--install" ]]; then
            install=1
        fi
    done

    if [[ $clean -eq 1 ]] || [[ $cleanOnly -eq 1 ]]; then
        rm -rf build/
        rm -rf usr/
        rm -rf stage/
    fi

    if [[ $cleanOnly -eq 1 ]]; then
        exit 0
    fi

    if [[ $release -eq 0 ]] && [[ $debug -eq 0 ]]; then
        release=1
    fi

    if [[ $debug -eq 1 ]]; then
        cmake -S okular/ -B build/debug/ -DCMAKE_BUILD_TYPE=Debug --install-prefix $PWD/usr/debug/ -DCMAKE_CXX_FLAGS="-U NDEBUG"
        cmake --build build/debug/ --target v3dCommon
        cmake --build build/debug/ --target okularGenerator_v3d
        cmake --build build/debug/ --target okularGenerator_poppler
    fi

    if [[ $release -eq 1 ]]; then
        cmake -S okular/ -B build/release/ -DCMAKE_BUILD_TYPE=Release --install-prefix $PWD/usr/release -DCMAKE_CXX_FLAGS="-U NDEBUG"
        cmake --build build/release/ --target v3dCommon
        cmake --build build/release/ --target okularGenerator_v3d
        cmake --build build/release/ --target okularGenerator_poppler
    fi

    # ---- Staging phase ----
    # Clean the staging directory at the start of each build run
    rm -rf "stage/"
    mkdir -p "stage/"

    # Determine which build tree to stage from (prefer release, fall back to debug)
    BUILD_TREE="build/release"
    if [[ $release -eq 0 ]] && [[ $debug -eq 1 ]]; then
        BUILD_TREE="build/debug"
    fi

    BIN_DIR="${BUILD_TREE}/bin"

    for so_file in okularGenerator_v3d.so okularGenerator_poppler.so; do
        if [[ -f "${BIN_DIR}/okular_generators/${so_file}" ]]; then
            cp "${BIN_DIR}/okular_generators/${so_file}" "stage/"
        fi
    done

    if [[ -f "${BIN_DIR}/libv3dCommon.so" ]]; then
        cp "${BIN_DIR}/libv3dCommon.so" "stage/"
    fi

    if [[ -d "../../asymptote/base/shaders" ]]; then
        cp ../../asymptote/base/shaders/*.glsl "stage/"
    fi

    if [[ -f "../../base-release/install.sh" ]]; then
        cp "../../base-release/install.sh" "stage/"
    fi

    if [[ -f "../../base-release/v3d-mime.xml" ]]; then
        cp "../../base-release/v3d-mime.xml" "stage/"
    fi

    if [[ -f "../../base-release/okularApplication_v3d.desktop" ]]; then
        cp "../../base-release/okularApplication_v3d.desktop" "stage/"
    fi

    if [[ $install -eq 1 ]]; then
        cd "stage/" || { echo "Error: stage directory not found."; exit 1; }
        bash "./install.sh"
    fi
}
