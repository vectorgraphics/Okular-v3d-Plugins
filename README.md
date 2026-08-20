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
In order to build the plugins for a specific version of Okular navigate to `release/<desired version>/` and execute the script `./build.sh`, you will most likely only be able to build the plugin for the versions of okular that ship with your package manager due to dependency versioning.

For convenience here is the full list of packages needed to build the plugins on Fedora 44:
`sudo dnf install cmake g++ extra-cmake-modules qt6-qttools-devel qt6-qtsvg-devel kf6-karchive-devel kf6-kbookmarks-devel kf6-kcompletion-devel kf6-kconfig-devel kf6-kconfigwidgets-devel kf6-ki18n-devel kf6-kio-devel kf6-threadweaver-devel kf6-kparts-devel kf6-kcrash-devel kf6-kiconthemes-devel plasma-activities-devel kf6-kpty-devel poppler-qt6-devel glm-devel libtirpc-devel vulkan-validation-layers qt6-qtbase-private-devel okular`

And for Ubuntu 26.04:
`sudo apt install cmake g++ extra-cmake-modules qt6-tools-dev qt6-svg-dev libkf6archive-dev libkf6bookmarks-dev libkf6completion-dev libkf6config-dev libkf6configwidgets-dev libkf6i18n-dev libkf6kio-dev libkf6threadweaver-dev libkf6parts-dev libkf6crash-dev libkf6iconthemes-dev libkf6pty-dev libpoppler-qt6-dev libglm-dev libtirpc-dev vulkan-validationlayers qt6-base-private-dev libkf6textwidgets-dev glslang-dev spirv-tools okular`

## Building plugins for a new version of Okular
For this example we will be building plugins for Okular version 25.04

Firstly, create a new folder for the version of Okular you want to build under  `build/`, and clone the okular source code into that folder. ie `build/25.04/okular`.

Be sure to check out the correct branch of the Okular source code for the desired version. ie. the branch named: `release/25.04`.

Then copy the build script from another version of the plugin into the folder you created with the Okular version as its name. ie. into the folder: `build/25.04/`.

Assuming you only want to build either the v3d or pdf plugin or both, and none of the default plugins that Okular ships with, you can force a bunch of dependencies to become optional instead of required. Do this by replacing the line mentioning the `FORCE_NOT_REQUIRED_DEPENDENCIES` variable with the following line:

`set(FORCE_NOT_REQUIRED_DEPENDENCIES "KF6Wallet;KF6DocTools;KF6Purpose;Qt6TextToSpeech;Phonon4Qt6;Freetype;TIFF;LibSpectre;KExiv2Qt6;DjVuLibre;EPub;Discount;JPEG")`

in the CMakeLists.txt file in the root of the Okular source code. ie `build/25.04/okular/CMakeLists.txt`. The line you need to replace will be near the top of the file.

Then in the CMakeLists.txt file located in the generators directory (`build/25.04/okular/generators/CMakeLists.txt`), add the following lines before all the `add_subdirectory()` calls:

```
set(V3D_OKULAR_CORE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../core")
add_subdirectory(../../../../v3d-Common v3dCommon)
```

This will build the v3dCommon shared library before either plugin so that both can link against it.

### v3d
The v3d plugin is quite simple, and dosent rely on many features of Okular, therefore it most likely dosent require any changes to work with a new version of okular, however it does need to be re-built specificly for the new version of Okular.

Firstly, copy the source code from an older version of the plugin into the generator folder of your freshly cloned Okular source code. ie. copy the folder `build/24.12/okular/generators/v3d/` into `build/25.04/okular/generators/`.

Then in the CMakeLists.txt file located in `build/version/okular/generators/` add the line: `add_subdirectory(v3d)` amongst the other `add_subdirectory` function calls.

Then follow the above build instructions to build

### pdf
Instead of being an entire standalone plugin, the pdf plugin is a modification to the pre-existing poppler plugin, meaning that specific blocks of code need to be inserted in specific locations.

Start by copying the existing poppler plugin source code folder (located in `build/version/okular/generators/`) into a new folder named `pdf`.

Then in the CMakeLists.txt file located in `build/version/okular/generators/` add the line: `add_subdirectory(pdf)` amongst the other `add_subdirectory` function calls.

Also be sure to comment out the existing `add_subdirectory(poppler)` call, otherwise cmake will complain about building two libraries with the same name.

Then a few files need to be modified:

#### generator_pdf.h
Located in `build/version/okular/generators/pdf/`

* Insert the following amongst the other includes:
```
#include "V3dModelManager.h"
```

* Insert the following near the top of the class definition above the constructor:

```
public:
    V3dModelManager modelManager{ document() };
```
#### generator_pdf.cpp
Located in `build/version/okular/generators/pdf/`

* Insert the following amongst the other includes:
```
#include <gzip/compress.hpp>
#include <gzip/config.hpp>
#include <gzip/decompress.hpp>
#include <gzip/utils.hpp>
#include <gzip/version.hpp>
```

* In the function `Okular::Document::OpenResult PDFGenerator::loadDocumentWithPassword(const QString &filePath, QList<Okular::Page *> &pagesVector, const QString &password)` insert the following at the top of the function:
```
if (document() != nullptr) {
    modelManager.SetDocument(document());
}
```

* In the function: `QImage PDFGenerator::image(Okular::PixmapRequest *request)` insert the following near the end of the function just before the mutex is unlocked:

```
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
```

* In the function `void PDFGenerator::addAnnotations(Poppler::Page *popplerPage, Okular::Page *page)` insert the following at the begining of the for loop that iterates over all of the `popplerAnnotations`:

```
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

        // Guard: check for gzip magic bytes before decompressing.
        // gzip-hpp throws on bad data but exceptions are disabled in this build
        // (-fno-exceptions), so a throw would abort the process.
        bool isGzip = (fileData.size() >= 2 &&
            static_cast<unsigned char>(fileData[0]) == 0x1f &&
            static_cast<unsigned char>(fileData[1]) == 0x8b);

        if (!isGzip) {
            std::cerr << "v3d: embedded file is not gzip-compressed, skipping." << std::endl;
            continue;
        }

        std::string decompressedData = gzip::decompress(fileData.data(), fileData.size());

        if (decompressedData.empty()) {
            std::cerr << "v3d: embedded file decompressed to empty data, skipping." << std::endl;
            continue;
        }

        xdr::memixstream xdrFile{ (uint8_t*)decompressedData.data(), decompressedData.size() };

        QRectF bound = a->boundary();

        bound = bound.normalized();

        glm::vec2 minBound{ bound.left(), bound.top() };
        glm::vec2 maxBound{ bound.right(), bound.bottom() };

        modelManager.AddModel(V3dModel{ xdrFile, minBound, maxBound }, page->number());
    }
}
```

#### CMakeLists.txt
Located in `build/version/okular/generators/pdf/`

* Insert the following inside of the `include_directories` function below what is already there:
```
"../../../../../gzip-hpp/include"
```

* Insert the following inside of the `target_link_libraries` function below what is already there:
```
v3dCommon z
```

* Insert the following just above the call to `target_link_libraries`
```
set_target_properties(okularGenerator_poppler PROPERTIES
    SKIP_BUILD_RPATH TRUE
    BUILD_WITH_INSTALL_RPATH TRUE
    INSTALL_RPATH "\$ORIGIN"
)
```

## Creating Releases
Run `create-releases.sh` to automatically build and create a release zip file for the version of okular installed on your device.

