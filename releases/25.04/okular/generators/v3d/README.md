# Vertex-Data-Plugin-Code

This repo stores the code used to build the shared library in the repository: https://github.com/Ben-Bingham/Vertex-Data-Plugin

## Building

1. In order to build this plugin, first setup a KDE build envrionment refer to this website: https://develop.kde.org/docs/getting-started/building/kdesrc-build-setup/ and in general KDE documentation, on setting that up.

2. After setting up your build environment, make sure you can build okular normaly.

3. Within the okular source code on your system, find the folder titled `generators`, most likly located in: `~/kde/src/okular/generators` or somewhere nearby, and clone this repository into the `generators` folder.

4. Add the line `add_subdirectory(Vertex-Data-Plugin-Code)` (this assumes that `Okular-v3d-Plugin-Code` is the name of the folder with this repository) into the `CMakeLists.txt` file located in the `generators` directory.

5. Install `glfw` and `glew` and make the staticly linked portion of their shared librarys available to your compiler.

(The following steps are taken from https://github.com/Ben-Bingham/Vertex-Data-Plugin)

6. Associating the file extension .vertexdata with the mime-type `text/x-vertdata` by installing vertex-data-mime.xml into `/usr/share/mime/packages/`
then update mime-type database with the command: `update-mime-database /usr/share/mime` (Command can be run from any directory)

7. Add the dependencies folder to `$LD_LIBRARY_PATH` with the command: `export LD_LIBRARY_PATH=$LD_LIBRARY_PATH:$(pwd)/dependencies` (assumes you run the command from the directory containing dependencies)

8. Finaly, rebuild Okular, and load the provided sample file.
