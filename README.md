## Instalation instructions:

# Install only:
* Find the latest release for your distribution and version of okular, then download the
corresponding release.zip file. Then:
```
unzip release.zip
cd Release
sudo ./install.sh
```

# Code And Install:
* If you want to install the plugin, and already have this repository cloned, then checkout the
branch corresponding to your version of okular and:
```
cd Release
sudo ./install.sh
```

## Building instructions:
* Start by setting up a kdesrc-build build environment: https://develop.kde.org/docs/getting-started/building/kdesrc-build-setup/

* Build okular normally
* Then navigate to `kde/src/okular/generators` and clone this repository into the `generators`
folder
* Finally add the lines:
```
add_subdirectory(Okular-v3d-Plugins/pdf)
add_subdirectory(Okular-v3d-Plugins/v3d)
```
to the CMakeLists.txt file located in the `generators` folder, and build okular again.
