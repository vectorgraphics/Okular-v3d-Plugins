#pragma once

#include "V3dObjects.h"
#include "V3dHeaderInfo.h"
#include "Mesh.h"

#include "xstream.h"

class V3dModel;

class V3dFile {
public:
    V3dFile(const std::string& fileName);
    // totalBytes is the size of the stream's buffer; load() validates the
    // file's length/count claims against it (the natural cap: EOF).
    V3dFile(xdr::memixstream& xdrFile, std::size_t totalBytes);

    UINT versionNumber;
    V3D_BOOL doublePrecisionFlag;

    std::vector<TRIPLE> centers;
    std::vector<V3dMaterial> materials;

    std::vector<std::unique_ptr<V3dObject>> objects;

    V3dHeaderInfo headerInfo;

    void QueueMesh(int imageWidth, int imageHeight, triple sceneMinBound, triple sceneMaxBound, bool remesh, bool orthographic, DrawMode drawMode);


private:
    // totalBytes is the stream's full size (file size, or buffer size); it
    // bounds every length/count claim the file makes about itself.
    void load(xdr::ixstream& xdrFile, int64_t totalBytes);
};
