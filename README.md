# Okular-v3d-Plugins for Microsoft Windows
A set of plugins adding v3d file support to the pdf reader Okular.

Note: Most scripts can be run with --help for more info

## V3D
Adds support for opening v3d files directly inside of Okular, currently rotation, zooming, and panning are all supported, however v3d files must be prerendered.

## PDF
Adds support for opening pdf files with v3d files embedded inside, currently rotation, zooming and panning of models is supported, however v3d files must be prerendered.

## Installing
1. If you already have Okular installed on your system, uninstall it
2. Navigate to the release page on github, and download the latest windows release `release.zip`
3. Unzip `release.zip`
4. Execute the executable, its name should be similar to: `okular-25.04.3-windows-cl-msvc2022-x86_64`
5. During the installer do not modify the default install path, ensure that it remains as `C:\Program Files\Okular`
6. Manually copy `vertex.spv` and `fragment.spv` into `C:\Program Files\Okular\bin`

## TODO
WINDOWS:
* Make vertex.spv and fragment.spv auto install with the installer
* Allow the user to customize the install path
* Add passing files to test scripts
* Look into making the pageview location function more consistent, possibly with some kind of check, and or dynamicly writing to file info about the correct one once found
* Optimise rendering code to be more performant
* Make an install script that detects what version of Okular the user has installed and installs the correct version.
* True vector based renderer
* Make the index and vertex buffers once, than cache them, currently they are recreated every time we render
* Documentation
* Presentation Mode dosent work at all
* Customizable Controls
* Documents with variable sized pages.
* Uninstall script

## BUGS
* Opening multiple documents causes a crash
* Flickering on model movement on some systems
* Using on a device with multiple monitors with different resolutions breaks interaction due to different device pixel ratio per monitor
