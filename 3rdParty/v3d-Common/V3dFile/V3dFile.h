#pragma once

#include "V3dObjects.h"
#include "V3dHeaderInfo.h"
#include "Mesh.h"

#include "xstream.h"

class V3dFile {
public:
    V3dFile(const std::string& fileName);
    V3dFile(xdr::memixstream& xdrFile);

    UINT versionNumber;
    V3D_BOOL doublePrecisionFlag;

    std::vector<TRIPLE> centers;
    std::vector<V3dMaterial> materials;

    std::vector<std::unique_ptr<V3dObject>> m_Objects;

    V3dHeaderInfo headerInfo;

    void QueueMesh(int imageWidth, int imageHeight);
    Mesh GetMesh();

private:
    void load(xdr::ixstream& xdrFile);
};
