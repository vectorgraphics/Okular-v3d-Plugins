#pragma once

#include <vector>

#include "V3dTypes.h"
#include "Mesh.h"

#include "triple.h"

using namespace camp;

class V3dObject {
public:
    V3dObject(UINT objectType);
    virtual ~V3dObject() = default;

    virtual void QueueMesh(int imageWidth, int imageHeight, triple sceneMinBound, triple sceneMaxBound, bool orthographic = false) { }
    virtual Mesh getMesh() {
        return Mesh{ getVertexData(), getIndices() };
    }

    virtual std::vector<float> getVertexData() = 0;
    virtual std::vector<unsigned int> getIndices() = 0;

    UINT objectType;
};
