#pragma once

#include "V3dObjects.h"
#include "V3dHeaderInfo.h"

class V3dFile {
public:
    V3dFile(const std::string& fileName);
    V3dFile(xdr::memixstream& xdrFile);

    UINT versionNumber;
    BOOL doublePrecisionFlag;

    std::vector<TRIPLE> centers;
    std::vector<V3dMaterial> materials;

    std::vector<std::unique_ptr<V3dObject>> m_Objects;

    V3dHeaderInfo headerInfo;

    std::vector<float> vertices;
    std::vector<unsigned int> indices;

private:
    void load(xdr::ixstream& xdrFile);
};