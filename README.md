# Okular-v3d-Plugins
A set of plugins adding v3d file support to the pdf reader Okular.

Note: Most scripts can be run with --help for more info

## V3D
Adds support for opening v3d files directly inside of Okular, currently rotation, zooming, and panning are all supported, however v3d files must be prerendered.

## PDF
Adds support for opening pdf files with v3d files embedded inside, currently rotation, zooming and panning of models is supported, however v3d files must be prerendered.

## Installing
1. Install Okular through your favourite package manager
2. Find out what version of Okular you have installed either by running `okular --version` or launch okular, click on `help` in the top bar and then `About Okular` in the drop down. The version will have the format: `YY.MM.0X` where YY is the year, MM is the month, and 0X is the sub version.
3. Navigate to the releases page on github, and download the `release.zip` file for the plugin whos version matches the version of Okular you have installed on your system. If you cant find a plugin version that matches your version of Okular try downgrading the version of Okular you have installed and go back to step 2. If you still cannot find a matching version, consider following the build instructions below and building your own version of the plugin.
4. Unzip `release.zip`
5. `cd release`
6. Finally, run `./install.sh` with root permissions

## Building
In order to build the plugins for a specific version of Okular navigate to `release/<desired version>/` and execute the script `./build.sh`.

In order to build the plugins for all supported versions of Okular, navigate to `/build/` and execute `./build-all.sh`.

If you want to build all versions of the plugin at once you need to have access to qt packages with version 6.6. An easy way to do this is to either use a fedora40 distrobox or a fedora40 VM where you downgrade all the qt6 packages to 6.6, ie `sudo dnf downgrade qt6-qt*` works on fedora40. If you just want to build the most recent version of the plugin then any modern distro should be fine.

## Creating Releases
First, install the github cli (`gh`) on your system and authorise with `gh auth login`.

Then build all the plugins in release mode.

And finally run `create-releases.sh` to automatically create, and upload releases for all currently supported versions of Okular.

## Testing
After you've built the plugin, you can begin testing. Testing is mostly automated, except for the final step of testing interaction, which should be very brief if everything works as expected.

Testing is done inside of distroboxes which allow for testing on many different linux distributions.

In order to test the plugin for one specific distribution, navigate to `testing/<desired linux distro>/` and run `./test.sh`. If you want to run a clean test on a brand new distrobox then add then use the option `--clean` to first delete the old distrobox and then make a new one.

If you want to instead test all supported operating systems then navigate to `testing/` and execute `./test-all.sh` again including the optional `--clean` argument.

You will need to give root permissions to the script as it executes to allow it to install the plugin into the distrobox, and to copy the built plugin into the home folder of the distrobox.


## Building plugins for a new version of Okular
For this example we will be building plugins for Okular version 25.04

Firstly, create a new folder for the version of Okular you want to build under  `build/`, and clone the okular source code into that folder. ie `build/25.04/okular`.

Be sure to check out the correct branch of the Okular source code for the desired version. ie the branch named: `release/25.04`.

Then copy the build script from another version of the plugin into the folder you created with the Okular version as its name. ie. into the folder: `build/25.04/`.

Assuming you only want to build either the v3d or pdf plugin or both, and none of the default plugins that Okular ships with, you can force a bunch of dependencies to become optional instead of required. Do this by replacing the line mentioning the `FORCE_NOT_REQUIRED_DEPENDENCIES` variable with the following line:

`set(FORCE_NOT_REQUIRED_DEPENDENCIES "KF6Wallet;KF6DocTools;KF6Purpose;Qt6TextToSpeech;Phonon4Qt6;Freetype;TIFF;LibSpectre;KExiv2Qt6;DjVuLibre;EPub;Discount;JPEG")`

in the CMakeLists.txt file in the root of the Okular source code. ie `build/25.04/okular/CMakeLists.txt`. The line you need to replace will be near the top of the file.

You will also need to install many packages in order to build the plugins, you can just repeatadly run the build script and install whatever packages cmake cannot find, but if you use dnf as a package manager you can simply execute:

`sudo dnf install cmake g++ extra-cmake-modules qt6-qttools-devel qt6-qtsvg-devel kf6-karchive-devel kf6-kbookmarks-devel kf6-kcompletion-devel kf6-kconfig-devel kf6-kconfigwidgets-devel kf6-ki18n-devel kf6-kio-devel kf6-threadweaver-devel kf6-kparts-devel kf6-kcrash-devel kf6-kiconthemes-devel plasma-activities-devel kf6-kpty-devel poppler-qt6-devel glm-devel libtirpc-devel`

which is a minimal list of packages required.

### v3d
The v3d plugin is quite simple, and dosent rely on many features of Okular, therefore it most likely dosent require any changes to work with a new version of okular, however it does need to be re-built specificly for the new version of Okular.

Firstly, copy the source code from an older version of the plugin into the generator folder of your freshly cloned Okular source code. ie. copy the folder `build/24.12/okular/generators/v3d/` into `build/25.04/okular/generators/`.

Then in the CMakeLists.txt file located in `build/version/okular/generators/` add the line: `add_subdirectory(v3d)` amongst the other `add_subdirectory` function calls.

Finally, navigate back to the build script you cloned earlier (located in `build/version/`) and run it to build the plugin.

### pdf
Instead of being an entire standalone plugin, the pdf plugin is a modification to the pre-existing poppler plugin, meaning that specific blocks of code need to be insterted in specific locations.

Start by copying the existing poppler plugin source code folder (located in `build/version/okular/generators/`) into a new folder named `pdf`.

Then in the CMakeLists.txt file located in `build/version/okular/generators/` add the line: `add_subdirectory(odf)` amongst the other `add_subdirectory` function calls.

Also be sure to comment out the existing `add_subdirectory(poppler)` call, otherwise cmake will complain about building two libraries with the same name.

Then a few files need to be modified, Here they will all be surounded by comments indicating that they are custom code in order to make them easier to find.

Look at existing versions of the plugin and Ctrl-F for `begin v3d` to help with placement.
#### generator_pdf.h
Located in `build/version/okular/generators/pdf/`

* Insert the following amongst the other includes:
```
// ========== begin v3d ==========
#include "V3dModelManager.h"
// ========== end v3d ==========
```

* Insert the following near the top of the class definition above the constructor:

```
// ========== begin v3d ==========
public:
    V3dModelManager modelManager{ document() };
// ========== end v3d ==========
```
#### generator_pdf.cpp
Located in `build/version/okular/generators/pdf/`

* Insert the following amongst the other includes:
```
// ========== begin v3d ==========
#include <gzip/compress.hpp>
#include <gzip/config.hpp>
#include <gzip/decompress.hpp>
#include <gzip/utils.hpp>
#include <gzip/version.hpp>
// ========== end v3d ==========
```

* In the function: `QImage PDFGenerator::image(Okular::PixmapRequest *request)` insert the following near the end of the function just before the mutex is unlocked:

```
// ========== begin v3d ==========
if (!img.isNull() && img.format() != QImage::Format_Mono && !modelManager.Empty()) {
    size_t pageNumber = (size_t)request->page()->number();

    int i = 0;
    for (auto& model : modelManager.Models(pageNumber)) {
        int xMin = (int)(request->width() * model.minBound.x);
        int xMax = (int)(request->width() * model.maxBound.x);
        int yMin = (int)(request->height() * model.minBound.y);
        int yMax = (int)(request->height() * model.maxBound.y);
        
        int imageWidth = xMax - xMin;
        int imageHeight = yMax - yMin;

        modelManager.CacheRequest(request);

        QImage image = modelManager.RenderModel(pageNumber, i, imageWidth, imageHeight);

        QPainter painter{ &img };

        if (request->isTile()) {
            painter.drawImage(xMin - request->normalizedRect().left * request->width(), yMin - request->normalizedRect().top * request->height(), image);
        } else {
            painter.drawImage(xMin, yMin, image);
        }

        ++i;
    }
}

modelManager.DrawMouseBoundaries(&img, request->pageNumber());
// ========== end v3d ==========
```

* In the function `void PDFGenerator::addAnnotations(Poppler::Page *popplerPage, Okular::Page *page)` insert the following at the begining of the for loop that iterates over all of the `popplerAnnotations`:

```
// ========== begin v3d ==========
if (a->subType() == Poppler::Annotation::SubType::ARichMedia) {
    QRectF bound = a->boundary();
    bound = bound.normalized();

    Poppler::RichMediaAnnotation* richMedia = dynamic_cast<Poppler::RichMediaAnnotation*>(a.get());
    if (richMedia == nullptr) {
        continue;
    }

    Poppler::RichMediaAnnotation::Content* content = richMedia->content();
    if (content == nullptr) {
        continue;
    }

    QList<Poppler::RichMediaAnnotation::Asset*> assets = content->assets();

    for (Poppler::RichMediaAnnotation::Asset* asset : assets) {
        if (asset == nullptr) {
            continue;
        }
        
        Poppler::EmbeddedFile* embeddedFile = asset->embeddedFile();
        if (embeddedFile == nullptr) {
            continue;
        }

        QByteArray fileData = embeddedFile->data();

        std::string decompressedData = gzip::decompress(fileData.data(), fileData.size());

        xdr::memixstream xdrFile{ (uint8_t*)decompressedData.data(), decompressedData.size() };

        QRectF bound = a->boundary();
        bound = bound.normalized();

        glm::vec2 minBound{ bound.left(), bound.top() };
        glm::vec2 maxBound{ bound.right(), bound.bottom() };

        modelManager.AddModel(V3dModel{ xdrFile, minBound, maxBound }, page->number());         
    }    
}
// ========== end v3d ==========
```

* In the function `Okular::Document::OpenResult PDFGenerator::loadDocumentWithPassword(const QString &filePath, QVector<Okular::Page *> &pagesVector, const QString &password)` insert the following at the top of the function:
```
// ========== begin v3d ==========
if (document() != nullptr) {
    modelManager.SetDocument(document());
}
// ========== end v3d ==========
```

#### CMakeLists.txt
Located in `build/version/okular/generators/pdf/`

* Insert the following inside of the `include_directories` function below what is already there:
```
# ========== begin v3d ==========
"../../../../../3rdParty/v3d-Common/"
"../../../../../3rdParty/gzip-hpp/include"
"../../core/"
"/usr/include/tirpc"
# ========== end v3d ==========
```

* Insert the following inside of the `set(okularGenerator_poppler_PART_SRCS` function below what is already there:
```
# ========== begin v3d ==========
../../../../../3rdParty/v3d-Common/Rendering/renderheadless.cpp
../../../../../3rdParty/v3d-Common/3rdParty/VulkanTools/VulkanTools.cpp
../../../../../3rdParty/v3d-Common/V3dFile/V3dFile.cpp
../../../../../3rdParty/v3d-Common/V3dFile/V3dObject.cpp
../../../../../3rdParty/v3d-Common/V3dFile/V3dObjects.cpp
../../../../../3rdParty/v3d-Common/V3dFile/V3dUtil.cpp
../../../../../3rdParty/v3d-Common/V3dFile/V3dHeaderInfo.cpp
../../../../../3rdParty/v3d-Common/Utility/Arcball.cpp
../../../../../3rdParty/v3d-Common/Utility/ProtectedFunctionCaller.cpp
../../../../../3rdParty/v3d-Common/Utility/EventFilter.cpp
../../../../../3rdParty/v3d-Common/V3dModel.cpp
../../../../../3rdParty/v3d-Common/V3dModelManager.cpp
../../../../../3rdParty/v3d-Common/3rdParty/xstream.cc
# ========== end v3d ==========
```


* Insert the following inside of the `target_link_libraries` function below what is already there:
```
# ========== begin v3d ==========
vulkan tirpc z
# ========== end v3d ==========
```

* Insert the following just above the install files secition
```
# ========== begin v3d ==========
add_compile_definitions(HAVE_LIBTIRPC)
# ========== end v3d ==========
```

Finally, navigate back to the build script you cloned earlier (located in `build/version/`) and run it to build the plugin.

## Adding testing for a new linux distrobution
First, create a new folder for the distro in the testing folder ie: `testing/distro/`

Copy in the `test.sh` script from another testing environment into the new one, and at minimum Update the variables located at the top of the file: `okularVersion`, `distro`, and `distroboxImage`.

Finally run the test script.

## TODO
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
* Preview of release name in create release script

## BUGS
* Opening multiple documents causes a crash
* Flickering on model movement on some systems
* Using on a device with multiple monitors with different resolutions breaks interaction due to different device pixel ratio per monitor
