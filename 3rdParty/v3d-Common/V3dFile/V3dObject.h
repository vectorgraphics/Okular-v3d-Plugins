#pragma once

#include <vector>

#include "V3dTypes.h"
#include "Mesh.h"

#include "triple.h"

#include "material.h"
#include "render.h"

using namespace camp;

class V3dObject {
public:
    V3dObject(UINT objectType);
    virtual ~V3dObject() = default;

    virtual void QueueMesh(int imageWidth, int imageHeight, triple sceneMinBound, triple sceneMaxBound, bool remesh, bool orthographic = false) { }

    UINT objectType;

    // centerIndex > 0 means this object is a billboard (label/annotation).
    // The index references into the file's centers array (1-based).
    // Applied during QueueMesh to make labels always face the camera.
    UINT centerIndex{ 0 };

    bool fullyOnscreen{ false };

protected:
    camp::VertexBuffer vertexData{ };
};
