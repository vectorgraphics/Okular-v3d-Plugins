#pragma once

#include <vector>

#include "V3dTypes.h"
#include "Mesh.h"

class V3dObject {
public:
    V3dObject(UINT objectType);
    virtual ~V3dObject() = default;

    virtual Mesh getMesh(int imageWidth, int imageHeight) {
        return Mesh{ getVertexData(), getIndices() };
    }

    virtual std::vector<float> getVertexData() = 0;
    virtual std::vector<unsigned int> getIndices() = 0;

    UINT objectType;
};
