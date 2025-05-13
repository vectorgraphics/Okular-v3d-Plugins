### Building plugins for a new version of Okular
For this example we will be building plugins for Okular version 25.04

Firstly, create a new folder for the version of Okular you want to build under  `releases/`, and clone the okular source code into that folder. ie `releases/25.04/okular`.

Be sure to check out the correct branch of the Okular source code for the desired version. ie the branch named: `release/25.04`.

Then copy the build script from another version of the plugin into the folder you created with the Okular version as its name. ie. into the folder: `releases/25.04/`.

Assuming you only want to build either the v3d or pdf plugin or both, and none of the default plugins that Okular ships with, you can force a bunch of dependencies to become optional instead of required. Do this by replacing the line mentioning the `FORCE_NOT_REQUIRED_DEPENDENCIES` variable with the following line:

`set(FORCE_NOT_REQUIRED_DEPENDENCIES "KF6Wallet;KF6DocTools;KF6Purpose;Qt6TextToSpeech;Phonon4Qt6;Freetype;TIFF;LibSpectre;KExiv2Qt6;DjVuLibre;EPub;Discount")`

in the CMakeLists.txt file in the root of the Okular source code. ie `releases/25.04/okular/CMakeLists.txt`. The line you need to replace will be near the top of the file.

You will also need to install many packages in order to build the plugins, you can just repeatadly run the build script and install whatever packages cmake cannot find, but if you use dnf as a package manager you can simply execute:

`sudo dnf install kate cmake g++ extra-cmake-modules qt6-qttools-devel qt6-qtsvg-devel kf6-kbookmarks-devel kf6-kcompletion-devel kf6-kconfig-devel kf6-kconfigwidgets-devel kf6-kcoreaddons-devel kf6-ki18n-devel kf6-threadweaver-devel kf6-kwindowsystem-devel kf6-kxmlgui-devel kf6-kio-devel kf6-kparts-devel kf6-kcrash-devel kf6-kiconthemes-devel kf6-ktextwidgets-devel plasma-activities-devel kf6-kpty-devel poppler-qt6-devel glm-devel libtirpc-devel glew-devel libXxf86vm-devel libXrandr-devel libXi-devel kf6-karchive-devel`

which is a mostly minimal list of packages required.

#### v3d
The v3d plugin is quite simple, and dosent rely on many features of Okular, therefore it most likely dosent require any changes to work with a new version of okular, however it does need to be re-built specificly for the new version of Okular.

Firstly, copy the source code from an older version of the plugin into the generator folder of your freshly cloned Okular source code. ie. copy the folder `releases/24.12/okular/generators/v3d/` into `releases/25.04/okular/generators/`.

Then in the CMakeLists.txt file located in `releases/version/okular/generators/` add the line: `add_subdirectory(v3d)`.

Finally, navigate back to the build script you cloned earlier (located in `releases/version/`) and run it to build the plugin.

#### pdf
Instead of being an entire standalone plugin, the pdf plugin is a modification to the pre-existing poppler plugin, meaning that specific blocks of code need to be insterted in specific locations.

Start by copying the existing poppler plugin source code folder (located in `releases/version/okular/generators/`) into a new folder named `pdf`.

Then a few files need to be modified, Here they will all be surounded by comments indicating that they are not custom code in order to make them easier to find.

Look at existing versions of the plugin and Ctrl-F for `begin v3d` to help with placement.
##### generator_pdf.h
Located in `releases/version/okular/generators/pdf/`

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
##### generator_pdf.cpp
Located in `releases/version/okular/generators/pdf/`

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

        if (isTile) {
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

        xdr::memixstream xdrFile{ decompressedData.data(), decompressedData.size() };

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

##### CMakeLists.txt
Located in `releases/version/okular/generators/pdf/`

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
# ========== end v3d ==========
```


* Insert the following inside of the `starget_link_libraries` function below what is already there:
```
# ========== begin v3d ==========
vulkan tirpc z
# ========== end v3d ==========
```

Finally, navigate back to the build script you cloned earlier (located in `releases/version/`) and run it to build the plugin.

### Building
In order to build the plugins for a specific version of Okular navigate to `release/<desired version>/` and execute the script `./build.sh`.

In order to build the plugins for all supported versions of Okular, navigate to `/releases/` and execute `./build-all.sh`.

### Testing
After you've built the plugin, you can begin testing. Testing is mostly automated, except for the final step of testing interaction, which should be very brief if everything works as expected.

Testing is done inside of distroboxes which allow for testing on many different linux distributions.

In order to test the plugin for one specific distribution, navigate to `testing/<desired linux distro>/` and run `./test.sh`. If you want to run a clean test on a brand new distrobox then add then use the option `--clean` to first delete the old distrobox and then make a new one.

If you want to instead test all supported operating systems then navigate to `testing/` and execute `./test-all.sh` again including the optional `--clean` argument.

You will need to give root permissions to the script as it executes to allow it to install the plugin into the distrobox, and to copy the built plugin into the home folder of the distrobox.

## TODO
* multi page documents - Basics are done, still need to do:
    * in the short term im assuming all pages in a document are the same size, fix this
    * the 12 pixel margin may change depending on resolution, 2k vs 4k vs 1080p
* Okular zoom optimization
* shader paths change when an external user uses the plugin
    * Currently the plugin looks for the shaders in the path that Okular is run in
* New renderer
* Clean up includes
* Clean up CMAKE files
* Look into adding a tile manager to the generator
* panning and shifting
* make the index and vertex buffers once, than cache them, currently they are recreated every time we rerender
* documentation
* Presentation Mode dosent work at all
* Customizable Controls

## BUGS
* Opening multiple documents causes a crash
* Flickering on model movement on some systems
