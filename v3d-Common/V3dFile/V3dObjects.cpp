#include "V3dObjects.h"

#include "rgba.h"
#include "bezierpatch.h"
#include "beziercurve.h"
#include "bbox.h"
#include "bound.h"

#include <iostream>

#include "V3dUtil.h"

using namespace camp;

// Definition of the global materials pointer declared in V3dObjects.h.
namespace camp { const std::vector<V3dMaterial>* materials = nullptr; }

// Compute tight Bézier patch bounds via recursive subdivision (matches drawsurface.cc).
static void computeBezierPatchBounds(const triple* controls, triple& Min, triple& Max) {
    double cx[16], cy[16], cz[16];
    for (int i = 0; i < 16; ++i) {
        cx[i] = controls[i].getx();
        cy[i] = controls[i].gety();
        cz[i] = controls[i].getz();
    }

    double c0 = cx[0];
    double fuzz = Fuzz * run::norm(cx, 16);
    double x = bound(cx, min, c0, fuzz, maxdepth);
    double X = bound(cx, max, c0, fuzz, maxdepth);

    c0 = cy[0];
    fuzz = Fuzz * run::norm(cy, 16);
    double y = bound(cy, min, c0, fuzz, maxdepth);
    double Y = bound(cy, max, c0, fuzz, maxdepth);

    c0 = cz[0];
    fuzz = Fuzz * run::norm(cz, 16);
    double z = bound(cz, min, c0, fuzz, maxdepth);
    double Z = bound(cz, max, c0, fuzz, maxdepth);

    Min = triple(x, y, z);
    Max = triple(X, Y, Z);
}

// Compute tight Bézier triangle bounds via recursive subdivision (matches drawsurface.cc).
static void computeBezierTriangleBounds(const triple* controls, triple& Min, triple& Max) {
    double cx[10], cy[10], cz[10];
    for (int i = 0; i < 10; ++i) {
        cx[i] = controls[i].getx();
        cy[i] = controls[i].gety();
        cz[i] = controls[i].getz();
    }

    double c0 = cx[0];
    double fuzz = Fuzz * run::norm(cx, 10);
    double x = boundtri(cx, min, c0, fuzz, maxdepth);
    double X = boundtri(cx, max, c0, fuzz, maxdepth);

    c0 = cy[0];
    fuzz = Fuzz * run::norm(cy, 10);
    double y = boundtri(cy, min, c0, fuzz, maxdepth);
    double Y = boundtri(cy, max, c0, fuzz, maxdepth);

    c0 = cz[0];
    fuzz = Fuzz * run::norm(cz, 10);
    double z = boundtri(cz, min, c0, fuzz, maxdepth);
    double Z = boundtri(cz, max, c0, fuzz, maxdepth);

    Min = triple(x, y, z);
    Max = triple(X, Y, Z);
}

// Tight bounds for a cubic Bézier curve (4 control points).
// Uses bound(triple z0,c0,c1,z1,...) from path3.cc, now in bound.cc.
static double getx(const triple& v) { return v.getx(); }
static double gety(const triple& v) { return v.gety(); }
static double getz(const triple& v) { return v.getz(); }

static void computeBezierCurveBounds(const triple* controls, triple& Min, triple& Max) {
    double cx[4], cy[4], cz[4];
    for (int i = 0; i < 4; ++i) {
        cx[i] = controls[i].getx();
        cy[i] = controls[i].gety();
        cz[i] = controls[i].getz();
    }

    double c0 = cx[0];
    double fuzz = Fuzz * run::norm(cx, 4);
    double x = bound(controls[0], controls[1], controls[2], controls[3], min, getx, c0, fuzz, maxdepth);
    double X = bound(controls[0], controls[1], controls[2], controls[3], max, getx, c0, fuzz, maxdepth);

    c0 = cy[0];
    fuzz = Fuzz * run::norm(cy, 4);
    double y = bound(controls[0], controls[1], controls[2], controls[3], min, gety, c0, fuzz, maxdepth);
    double Y = bound(controls[0], controls[1], controls[2], controls[3], max, gety, c0, fuzz, maxdepth);

    c0 = cz[0];
    fuzz = Fuzz * run::norm(cz, 4);
    double z = bound(controls[0], controls[1], controls[2], controls[3], min, getz, c0, fuzz, maxdepth);
    double Z = bound(controls[0], controls[1], controls[2], controls[3], max, getz, c0, fuzz, maxdepth);

    Min = triple(x, y, z);
    Max = triple(X, Y, Z);
}


V3dBezierPatch::V3dBezierPatch(
    xdr::ixstream& xdrFile, 
    V3D_BOOL doublePrecision)
    : V3dObject{ ObjectTypes::BEZIER_PATCH } { 
        for (int i = 0; i < 16; ++i) {
            controlPoints[i].x = readReal(xdrFile, doublePrecision);
            controlPoints[i].y = readReal(xdrFile, doublePrecision);
            controlPoints[i].z = readReal(xdrFile, doublePrecision);
        }

        xdrFile >> centerIndex;
        V3dObject::centerIndex = centerIndex;
        xdrFile >> materialIndex;

        // Compute tight bounds at construction time (matches drawsurface.cc).
        triple Controls[16];
        for (int i = 0; i < 16; ++i)
            Controls[i] = triple(controlPoints[i].x, controlPoints[i].y, controlPoints[i].z);
        computeBezierPatchBounds(Controls, V3dObject::Min, V3dObject::Max);
    }

V3dBezierPatch::V3dBezierPatch(std::array<TRIPLE, 16> controlPoints, UINT centerIndex, UINT materialIndex) 
    : V3dObject{ ObjectTypes::BEZIER_PATCH }
    , controlPoints{ controlPoints }
    , centerIndex{ centerIndex }
    , materialIndex{ materialIndex } {
        // Compute tight bounds at construction time (matches drawsurface.cc).
        triple Controls[16];
        for (int i = 0; i < 16; ++i)
            Controls[i] = triple(controlPoints[i].x, controlPoints[i].y, controlPoints[i].z);
        computeBezierPatchBounds(Controls, V3dObject::Min, V3dObject::Max);
    }


void V3dBezierPatch::QueueMesh(int imageWidth, int imageHeight, triple sceneMinBound, triple sceneMaxBound, bool remesh, bool orthographic, DrawMode drawMode) {
    camp::materialIndex = materialIndex;

    triple Controls[] = {
        triple(controlPoints[0].x, controlPoints[0].y, controlPoints[0].z),
        triple(controlPoints[1].x, controlPoints[1].y, controlPoints[1].z),
        triple(controlPoints[2].x, controlPoints[2].y, controlPoints[2].z),
        triple(controlPoints[3].x, controlPoints[3].y, controlPoints[3].z),

        triple(controlPoints[4].x, controlPoints[4].y, controlPoints[4].z),
        triple(controlPoints[5].x, controlPoints[5].y, controlPoints[5].z),
        triple(controlPoints[6].x, controlPoints[6].y, controlPoints[6].z),
        triple(controlPoints[7].x, controlPoints[7].y, controlPoints[7].z),

        triple(controlPoints[8].x, controlPoints[8].y, controlPoints[8].z),
        triple(controlPoints[9].x, controlPoints[9].y, controlPoints[9].z),
        triple(controlPoints[10].x, controlPoints[10].y, controlPoints[10].z),
        triple(controlPoints[11].x, controlPoints[11].y, controlPoints[11].z),

        triple(controlPoints[12].x, controlPoints[12].y, controlPoints[12].z),
        triple(controlPoints[13].x, controlPoints[13].y, controlPoints[13].z),
        triple(controlPoints[14].x, controlPoints[14].y, controlPoints[14].z),
        triple(controlPoints[15].x, controlPoints[15].y, controlPoints[15].z),
    };

    triple b=sceneMinBound;
    triple B=sceneMaxBound;

    double Zmax=B.getz();

    double perspective=orthographic ? 0.0 : 1.0/Zmax;
    double s=perspective ? b.getz()*perspective : 1.0; // Move to glrender
    double size2=hypot(imageWidth,imageHeight);

    // Match Asymptote drawBezierPatch::render(): check material alpha in NORMAL mode only.
    // WIREFRAME/OUTLINE force opaque (commit 316f906894).
    bool transparent = false;
    if (drawMode == DRAWMODE_NORMAL && camp::materials && materialIndex < (int)camp::materials->size()) {
        transparent = (*camp::materials)[materialIndex].diffuse.a < 1.0f;
    }
    bool straight=false;
    bool color=false;
    const camp::pair size3(s*(B.getx()-b.getx()),s*(B.gety()-b.gety()));

    // Use tight bounds computed at construction time (matches Asymptote drawBezierPatch::Min/Max).
    bool offscreen = bbox2(Min, Max).offscreen();

    if(offscreen) { // Fully offscreen
        // Match Asymptote drawBezierPatch::render(): S.Onscreen=false, S.data.clear(), S.notRendered().
        S.Onscreen = false;
        S.data.clear();
        S.transparent = transparent;
        S.color = NULL;
        S.notRendered();
        fullyOnscreen = false;
        vertexData.clear();
        lineData.clear();
        return;
    }

    if(drawMode == DRAWMODE_OUTLINE) {
        // Match drawsurface.cc: OUTLINE mode only queues boundary curves via C.queue().
        // C is a persistent member (like drawSurface::C), not a local variable.
        // Does NOT call S.queue(), does NOT touch S.Onscreen or vertexData.
        triple edge0[] = { Controls[0],  Controls[4],  Controls[8],  Controls[12] };
        C.queue(edge0, straight, size3.length()/size2);
        triple edge1[] = { Controls[12], Controls[13], Controls[14], Controls[15] };
        C.queue(edge1, straight, size3.length()/size2);
        triple edge2[] = { Controls[15], Controls[11], Controls[7],  Controls[3]  };
        C.queue(edge2, straight, size3.length()/size2);
        triple edge3[] = { Controls[3],  Controls[2],  Controls[1],  Controls[0]  };
        C.queue(edge3, straight, size3.length()/size2);
    } else {
        if(!remesh && S.Onscreen) { // Fully onscreen; no need to re-render (matches Asymptote)
            S.append();
            return;
        }

        S.queue(Controls,straight,size3.length()/size2,transparent,NULL);
        fullyOnscreen = S.Onscreen;
        if (drawMode == DRAWMODE_NORMAL)
            isTransparent = S.transparent;  // Only persist in NORMAL mode
    }
}

V3dBezierTriangle::V3dBezierTriangle(
    xdr::ixstream& xdrFile, 
    V3D_BOOL doublePrecision)
    : V3dObject{ ObjectTypes::BEZIER_TRIANGLE } { 
        for (int i = 0; i < 10; ++i) {
            controlPoints[i].x = readReal(xdrFile, doublePrecision);
            controlPoints[i].y = readReal(xdrFile, doublePrecision);
            controlPoints[i].z = readReal(xdrFile, doublePrecision);
        }

        xdrFile >> centerIndex;
        V3dObject::centerIndex = centerIndex;
        xdrFile >> materialIndex;

        // Compute tight bounds at construction time (matches drawsurface.cc).
        triple Controls[10];
        for (int i = 0; i < 10; ++i)
            Controls[i] = triple(controlPoints[i].x, controlPoints[i].y, controlPoints[i].z);
        computeBezierTriangleBounds(Controls, V3dObject::Min, V3dObject::Max);
    }

V3dBezierTriangle::V3dBezierTriangle(std::array<TRIPLE, 10> controlPoints, UINT centerIndex, UINT materialIndex) 
    : V3dObject{ ObjectTypes::BEZIER_TRIANGLE }
    , controlPoints{ controlPoints }
    , centerIndex{ centerIndex }
    , materialIndex{ materialIndex } {
        // Compute tight bounds at construction time (matches drawsurface.cc).
        triple Controls[10];
        for (int i = 0; i < 10; ++i)
            Controls[i] = triple(controlPoints[i].x, controlPoints[i].y, controlPoints[i].z);
        computeBezierTriangleBounds(Controls, V3dObject::Min, V3dObject::Max);
    }

void V3dBezierTriangle::QueueMesh(int imageWidth, int imageHeight, triple sceneMinBound, triple sceneMaxBound, bool remesh, bool orthographic, DrawMode drawMode) {
    camp::materialIndex = materialIndex;

    triple Controls[] = {
        triple(controlPoints[0].x, controlPoints[0].y, controlPoints[0].z),
        triple(controlPoints[1].x, controlPoints[1].y, controlPoints[1].z),
        triple(controlPoints[2].x, controlPoints[2].y, controlPoints[2].z),
        triple(controlPoints[3].x, controlPoints[3].y, controlPoints[3].z),
        triple(controlPoints[4].x, controlPoints[4].y, controlPoints[4].z),

        triple(controlPoints[5].x, controlPoints[5].y, controlPoints[5].z),
        triple(controlPoints[6].x, controlPoints[6].y, controlPoints[6].z),
        triple(controlPoints[7].x, controlPoints[7].y, controlPoints[7].z),
        triple(controlPoints[8].x, controlPoints[8].y, controlPoints[8].z),
        triple(controlPoints[9].x, controlPoints[9].y, controlPoints[9].z),
    };

    triple b=sceneMinBound;
    triple B=sceneMaxBound;

    double Zmax=B.getz();

    double perspective=orthographic ? 0.0 : 1.0/Zmax;
    double s=perspective ? b.getz()*perspective : 1.0; // Move to glrender
    double size2=hypot(imageWidth,imageHeight);

    // Match Asymptote drawBezierPatch::render(): check material alpha in NORMAL mode only.
    // WIREFRAME/OUTLINE force opaque (commit 316f906894).
    bool transparent = false;
    if (drawMode == DRAWMODE_NORMAL && camp::materials && materialIndex < (int)camp::materials->size()) {
        transparent = (*camp::materials)[materialIndex].diffuse.a < 1.0f;
    }
    bool straight=false;
    bool color=false;
    const camp::pair size3(s*(B.getx()-b.getx()),s*(B.gety()-b.gety()));

    // Use tight bounds computed at construction time (matches Asymptote drawBezierPatch::Min/Max).
    bool offscreen = bbox2(Min, Max).offscreen();

    if(offscreen) { // Fully offscreen
        // Match Asymptote drawBezierPatch::render(): S.Onscreen=false, S.data.clear(), S.notRendered().
        S.Onscreen = false;
        S.data.clear();
        S.transparent = transparent;
        S.color = NULL;
        S.notRendered();
        fullyOnscreen = false;
        vertexData.clear();
        lineData.clear();
        return;
    }

    if(drawMode == DRAWMODE_OUTLINE) {
        // Match drawsurface.cc: OUTLINE mode only queues boundary curves via C.queue().
        // Does NOT call S.queue(), does NOT touch S.Onscreen or vertexData.

        triple edge0[] = { Controls[0], Controls[1], Controls[3], Controls[6] };
        C.queue(edge0, straight, size3.length()/size2);
        triple edge1[] = { Controls[6], Controls[7], Controls[8], Controls[9] };
        C.queue(edge1, straight, size3.length()/size2);
        triple edge2[] = { Controls[9], Controls[5], Controls[2], Controls[0] };
        C.queue(edge2, straight, size3.length()/size2);
    } else {
        if(!remesh && S.Onscreen) { // Fully onscreen; no need to re-render (matches Asymptote)
            S.append();
            return;
        }

        S.queue(Controls,straight,size3.length()/size2,transparent,NULL);
        fullyOnscreen = S.Onscreen;
        if (drawMode == DRAWMODE_NORMAL)
            isTransparent = S.transparent;  // Only persist in NORMAL mode
    }
}


V3dBezierPatchWithCornerColors::V3dBezierPatchWithCornerColors(
    xdr::ixstream& xdrFile, 
    V3D_BOOL doublePrecision)
    : V3dObject{ ObjectTypes::BEZIER_PATCH_COLOR } {
        for (int i = 0; i < 16; ++i) {
            controlPoints[i].x = readReal(xdrFile, doublePrecision);
            controlPoints[i].y = readReal(xdrFile, doublePrecision);
            controlPoints[i].z = readReal(xdrFile, doublePrecision);
        }

        xdrFile >> centerIndex;
        V3dObject::centerIndex = centerIndex;
        xdrFile >> materialIndex;    

        for (int i = 0; i < 4; ++i) {
            xdrFile >> cornerColors[i].r;
            xdrFile >> cornerColors[i].g;
            xdrFile >> cornerColors[i].b;
            xdrFile >> cornerColors[i].a;
        }

        // Compute tight bounds at construction time (matches drawsurface.cc).
        triple Controls[16];
        for (int i = 0; i < 16; ++i)
            Controls[i] = triple(controlPoints[i].x, controlPoints[i].y, controlPoints[i].z);
        computeBezierPatchBounds(Controls, V3dObject::Min, V3dObject::Max);
    }

void V3dBezierPatchWithCornerColors::QueueMesh(int imageWidth, int imageHeight, triple sceneMinBound, triple sceneMaxBound, bool remesh, bool orthographic, DrawMode drawMode) {
    camp::materialIndex = materialIndex;
    if(drawMode == DRAWMODE_OUTLINE) {
        V3dBezierPatch patch(controlPoints, centerIndex, materialIndex);
        patch.QueueMesh(imageWidth, imageHeight, sceneMinBound, sceneMaxBound, true, orthographic, drawMode);
        return;
    }

    triple Controls[] = {
        triple(controlPoints[0].x, controlPoints[0].y, controlPoints[0].z),
        triple(controlPoints[1].x, controlPoints[1].y, controlPoints[1].z),
        triple(controlPoints[2].x, controlPoints[2].y, controlPoints[2].z),
        triple(controlPoints[3].x, controlPoints[3].y, controlPoints[3].z),

        triple(controlPoints[4].x, controlPoints[4].y, controlPoints[4].z),
        triple(controlPoints[5].x, controlPoints[5].y, controlPoints[5].z),
        triple(controlPoints[6].x, controlPoints[6].y, controlPoints[6].z),
        triple(controlPoints[7].x, controlPoints[7].y, controlPoints[7].z),

        triple(controlPoints[8].x, controlPoints[8].y, controlPoints[8].z),
        triple(controlPoints[9].x, controlPoints[9].y, controlPoints[9].z),
        triple(controlPoints[10].x, controlPoints[10].y, controlPoints[10].z),
        triple(controlPoints[11].x, controlPoints[11].y, controlPoints[11].z),

        triple(controlPoints[12].x, controlPoints[12].y, controlPoints[12].z),
        triple(controlPoints[13].x, controlPoints[13].y, controlPoints[13].z),
        triple(controlPoints[14].x, controlPoints[14].y, controlPoints[14].z),
        triple(controlPoints[15].x, controlPoints[15].y, controlPoints[15].z),
    };

    triple b = sceneMinBound;
    triple B = sceneMaxBound;

    double Zmax = B.getz();
    double perspective = orthographic ? 0.0 : 1.0 / Zmax;
    double s = perspective ? b.getz() * perspective : 1.0;
    double size2 = hypot(imageWidth, imageHeight);

    bool straight = false;

    const camp::pair size3(s * (B.getx() - b.getx()), s * (B.gety() - b.gety()));

    // Match Asymptote drawBezierPatch::render(): only detect transparency in NORMAL mode.
    // WIREFRAME/OUTLINE force opaque (commit 316f906894).
    bool transparent = (drawMode == DRAWMODE_NORMAL && cornerColors[0].a + cornerColors[1].a +
                        cornerColors[2].a + cornerColors[3].a < 4.0f);

    // Use tight bounds computed at construction time (matches Asymptote drawBezierPatch::Min/Max).
    bool offscreen = bbox2(Min, Max).offscreen();

    if (offscreen) {
        // Match Asymptote drawBezierPatch::render(): S.Onscreen=false, S.data.clear(), S.notRendered().
        S.Onscreen = false;
        S.data.clear();
        S.transparent = transparent;
        S.color = NULL;
        S.notRendered();
        fullyOnscreen = false;
        vertexData.clear();
        return;
    }

    if (!remesh && S.Onscreen) { // Fully onscreen; no need to re-render (matches Asymptote)
        S.append();
        return;
    }

    // Convert corner colors to float arrays matching BezierPatch::render signature.
    float corners[16];
    for (int i = 0; i < 4; ++i) {
        corners[i*4+0] = static_cast<float>(cornerColors[i].r);
        corners[i*4+1] = static_cast<float>(cornerColors[i].g);
        corners[i*4+2] = static_cast<float>(cornerColors[i].b);
        corners[i*4+3] = static_cast<float>(cornerColors[i].a);
    }

    S.queue(Controls, straight, size3.length() / size2, transparent, corners);
    fullyOnscreen = S.Onscreen;
}


V3dBezierTriangleWithCornerColors::V3dBezierTriangleWithCornerColors(
    xdr::ixstream& xdrFile, 
    V3D_BOOL doublePrecision)
    : V3dObject{ ObjectTypes::BEZIER_TRIANGLE_COLOR } { 
        for (int i = 0; i < 10; ++i) {
            controlPoints[i].x = readReal(xdrFile, doublePrecision);
            controlPoints[i].y = readReal(xdrFile, doublePrecision);
            controlPoints[i].z = readReal(xdrFile, doublePrecision);
        }

        xdrFile >> centerIndex;
        V3dObject::centerIndex = centerIndex;
        xdrFile >> materialIndex;    

        for (int i = 0; i < 3; ++i) {
            xdrFile >> cornerColors[i].r;
            xdrFile >> cornerColors[i].g;
            xdrFile >> cornerColors[i].b;
            xdrFile >> cornerColors[i].a;
        }

        // Compute tight bounds at construction time (matches drawsurface.cc).
        triple Controls[10];
        for (int i = 0; i < 10; ++i)
            Controls[i] = triple(controlPoints[i].x, controlPoints[i].y, controlPoints[i].z);
        computeBezierTriangleBounds(Controls, V3dObject::Min, V3dObject::Max);
    }

void V3dBezierTriangleWithCornerColors::QueueMesh(int imageWidth, int imageHeight, triple sceneMinBound, triple sceneMaxBound, bool remesh, bool orthographic, DrawMode drawMode) {
    camp::materialIndex = materialIndex;
    if(drawMode == DRAWMODE_OUTLINE) {
        V3dBezierTriangle tri(controlPoints, centerIndex, materialIndex);
        tri.QueueMesh(imageWidth, imageHeight, sceneMinBound, sceneMaxBound, remesh, orthographic, drawMode);
        return;
    }

    triple Controls[] = {
        triple(controlPoints[0].x, controlPoints[0].y, controlPoints[0].z),
        triple(controlPoints[1].x, controlPoints[1].y, controlPoints[1].z),
        triple(controlPoints[2].x, controlPoints[2].y, controlPoints[2].z),
        triple(controlPoints[3].x, controlPoints[3].y, controlPoints[3].z),
        triple(controlPoints[4].x, controlPoints[4].y, controlPoints[4].z),

        triple(controlPoints[5].x, controlPoints[5].y, controlPoints[5].z),
        triple(controlPoints[6].x, controlPoints[6].y, controlPoints[6].z),
        triple(controlPoints[7].x, controlPoints[7].y, controlPoints[7].z),
        triple(controlPoints[8].x, controlPoints[8].y, controlPoints[8].z),
        triple(controlPoints[9].x, controlPoints[9].y, controlPoints[9].z),
    };

    triple b = sceneMinBound;
    triple B = sceneMaxBound;

    double Zmax = B.getz();
    double perspective = orthographic ? 0.0 : 1.0 / Zmax;
    double s = perspective ? b.getz() * perspective : 1.0;
    double size2 = hypot(imageWidth, imageHeight);

    bool straight = false;

    const camp::pair size3(s * (B.getx() - b.getx()), s * (B.gety() - b.gety()));

    // Match Asymptote: only detect transparency in NORMAL mode (commit 316f906894).
    bool transparent = (drawMode == DRAWMODE_NORMAL && cornerColors[0].a + cornerColors[1].a +
                        cornerColors[2].a < 3.0f);

    // Use tight bounds computed at construction time (matches Asymptote drawBezierTriangle::Min/Max).
    bool offscreen = bbox2(Min, Max).offscreen();


    if (offscreen) {
        // Match Asymptote drawBezierTriangle::render(): S.Onscreen=false, S.data.clear(), S.notRendered().
        S.Onscreen = false;
        S.data.clear();
        S.transparent = transparent;
        S.color = NULL;
        S.notRendered();
        fullyOnscreen = false;
        vertexData.clear();
        return;
    }

    if (!remesh && S.Onscreen) { // Fully onscreen; no need to re-render (matches Asymptote)
        S.append();
        return;
    }

    // Convert corner colors to float arrays matching BezierTriangle::render signature.
    float corners[12];
    for (int i = 0; i < 3; ++i) {
        corners[i*4+0] = static_cast<float>(cornerColors[i].r);
        corners[i*4+1] = static_cast<float>(cornerColors[i].g);
        corners[i*4+2] = static_cast<float>(cornerColors[i].b);
        corners[i*4+3] = static_cast<float>(cornerColors[i].a);
    }

    S.queue(Controls, straight, size3.length() / size2, transparent, corners);
    fullyOnscreen = S.Onscreen;
}


V3dStraightPlanarQuad::V3dStraightPlanarQuad(
    xdr::ixstream& xdrFile, 
    V3D_BOOL doublePrecision)
    : V3dObject{ ObjectTypes::QUAD } {
        for (int i = 0; i < 4; ++i) {
            vertices[i].x = readReal(xdrFile, doublePrecision);
            vertices[i].y = readReal(xdrFile, doublePrecision);
            vertices[i].z = readReal(xdrFile, doublePrecision);
        }

        xdrFile >> centerIndex;
        V3dObject::centerIndex = centerIndex;
        xdrFile >> materialIndex; 
    }

void V3dStraightPlanarQuad::QueueMesh(int imageWidth, int imageHeight, triple sceneMinBound, triple sceneMaxBound, bool remesh, bool orthographic, DrawMode drawMode) {
    camp::materialIndex = materialIndex;

    TRIPLE p1 = vertices[0];
    TRIPLE p2 = vertices[1];
    TRIPLE p3 = vertices[2];
    TRIPLE p4 = vertices[3];

    TRIPLE A = p2 - p1;
    TRIPLE B = p3 - p1;
    glm::dvec3 normal = glm::normalize(glm::cross(A, B));

    // Offscreen cull: match algorithm §\ref{cull}.
    triple Min{vertices[0].x, vertices[0].y, vertices[0].z};
    triple Max{vertices[0].x, vertices[0].y, vertices[0].z};
    for (int i = 1; i < 4; ++i) {
        Min = triple(std::min(Min.getx(), (double)vertices[i].x),
                     std::min(Min.gety(), (double)vertices[i].y),
                     std::min(Min.getz(), (double)vertices[i].z));
        Max = triple(std::max(Max.getx(), (double)vertices[i].x),
                     std::max(Max.gety(), (double)vertices[i].y),
                     std::max(Max.getz(), (double)vertices[i].z));
    }

    if (bbox2(Min, Max).offscreen()) {
        // notRendered() — ensures upload gate fires when object comes back onscreen.
        quadOnscreen = false;
        S.clear();
        if (S_color) transparentData.renderCount = 0;
        else materialData.renderCount = 0;
        return;
    }

    if(drawMode == DRAWMODE_OUTLINE) {
        triple v[] = {
            triple(p1.x, p1.y, p1.z),
            triple(p2.x, p2.y, p2.z),
            triple(p3.x, p3.y, p3.z),
            triple(p4.x, p4.y, p4.z)
        };
        bool straight = true;
        double s = 1.0;
        double size2 = hypot(imageWidth, imageHeight);
        camp::pair size3(s * (sceneMaxBound.getx() - sceneMinBound.getx()),
                         s * (sceneMaxBound.gety() - sceneMinBound.gety()));
        triple edge0[] = { v[0], v[0], v[1], v[1] };
        C.queue(edge0, straight, size3.length() / size2);
        triple edge1[] = { v[1], v[1], v[2], v[2] };
        C.queue(edge1, straight, size3.length() / size2);
        triple edge2[] = { v[2], v[2], v[3], v[3] };
        C.queue(edge2, straight, size3.length() / size2);
        triple edge3[] = { v[3], v[3], v[0], v[0] };
        C.queue(edge3, straight, size3.length() / size2);
        return;
    }

    // Determine transparency (NORMAL mode only; WIREFRAME/OUTLINE force opaque).
    bool transparent = false;
    if (drawMode == DRAWMODE_NORMAL && camp::materials && materialIndex < (int)camp::materials->size()) {
        transparent = (*camp::materials)[materialIndex].diffuse.a < 1.0f;
    }

    // Fast path: fully onscreen, same vertex format — just re-append S to global buffer.
    if (!remesh && quadOnscreen && centerIndex == 0) {
        if (S_color) transparentData.extendColor(S);
        else materialData.extendMaterial(S);
        return;
    }

    // Slow path: rebuild S with per-triangle culling (algorithm §\ref{cull}).
    // notRendered() — reset renderCount so upload gate fires.
    if (transparent) transparentData.renderCount = 0;
    else materialData.renderCount = 0;

    S.clear();
    quadOnscreen = true;
    fullyOnscreen = true;

    // Two triangles: T0=(p1,p2,p3), T1=(p1,p3,p4).
    triple tri0[] = { p1, p2, p3 };
    triple tri1[] = { p1, p3, p4 };
    bool t0Off = bbox2(3, tri0).offscreen();
    bool t1Off = bbox2(3, tri1).offscreen();
    if (t0Off || t1Off) { quadOnscreen = false; fullyOnscreen = false; }

    // Build S with the correct vertex format.
    S_color = transparent;  // transparent → ColorVertex, opaque → MaterialVertex
    if (transparent) {
        size_t base = S.colorVertices.size();
        S.colorVertices.push_back(ColorVertex{p1, normal, 1+materialIndex, glm::vec4(0,0,0,0)});
        S.colorVertices.push_back(ColorVertex{p2, normal, 1+materialIndex, glm::vec4(0,0,0,0)});
        S.colorVertices.push_back(ColorVertex{p3, normal, 1+materialIndex, glm::vec4(0,0,0,0)});
        S.colorVertices.push_back(ColorVertex{p4, normal, 1+materialIndex, glm::vec4(0,0,0,0)});
        if (!t0Off) { S.indices.push_back(base); S.indices.push_back(base+1); S.indices.push_back(base+2); }
        if (!t1Off) { S.indices.push_back(base); S.indices.push_back(base+2); S.indices.push_back(base+3); }
        transparentData.extendColor(S);
    } else {
        size_t base = S.materialVertices.size();
        S.materialVertices.push_back(MaterialVertex{p1, normal, materialIndex});
        S.materialVertices.push_back(MaterialVertex{p2, normal, materialIndex});
        S.materialVertices.push_back(MaterialVertex{p3, normal, materialIndex});
        S.materialVertices.push_back(MaterialVertex{p4, normal, materialIndex});
        if (!t0Off) { S.indices.push_back(base); S.indices.push_back(base+1); S.indices.push_back(base+2); }
        if (!t1Off) { S.indices.push_back(base); S.indices.push_back(base+2); S.indices.push_back(base+3); }
        materialData.extendMaterial(S);
    }

    if (drawMode == DRAWMODE_NORMAL) isTransparent = transparent;
}


V3dStraightTriangle::V3dStraightTriangle(
    xdr::ixstream& xdrFile, 
    V3D_BOOL doublePrecision)
    : V3dObject{ ObjectTypes::TRIANGLE } { 
        for (int i = 0; i < 3; ++i) {
            vertices[i].x = readReal(xdrFile, doublePrecision);
            vertices[i].y = readReal(xdrFile, doublePrecision);
            vertices[i].z = readReal(xdrFile, doublePrecision);
        }

        xdrFile >> centerIndex;
        V3dObject::centerIndex = centerIndex;
        xdrFile >> materialIndex;
    }

// std::vector<float> V3dStraightTriangle::getVertexData() {
//     std::vector<float> out{};
//
//     TRIPLE p1 = vertices[0];
//     TRIPLE p2 = vertices[1];
//     TRIPLE p3 = vertices[2];
//
//     TRIPLE A = p2 - p1;
//     TRIPLE B = p3 - p1;
//
//     TRIPLE N = glm::cross(A, B);
//
//     for (auto& ver : vertices) {
//         out.push_back(ver.x);
//         out.push_back(ver.y);
//         out.push_back(ver.z);
//
//         out.push_back(N.x);
//         out.push_back(N.y);
//         out.push_back(N.z);
//     }
//
//     return out;
// }
//
// std::vector<unsigned int> V3dStraightTriangle::getIndices() {
//     std::vector<unsigned int> out {
//         0, 1, 2
//     };
//
//     return out;
// }

void V3dStraightTriangle::QueueMesh(int imageWidth, int imageHeight, triple sceneMinBound, triple sceneMaxBound, bool remesh, bool orthographic, DrawMode drawMode) {
    camp::materialIndex = materialIndex;

    TRIPLE p1 = vertices[0];
    TRIPLE p2 = vertices[1];
    TRIPLE p3 = vertices[2];

    TRIPLE A = p2 - p1;
    TRIPLE B = p3 - p1;

    glm::dvec3 normal = glm::normalize(glm::cross(A, B));

    // Offscreen cull: match algorithm §\ref{cull}.
    triple Min{vertices[0].x, vertices[0].y, vertices[0].z};
    triple Max{vertices[0].x, vertices[0].y, vertices[0].z};
    for (int i = 1; i < 3; ++i) {
        Min = triple(std::min(Min.getx(), (double)vertices[i].x),
                     std::min(Min.gety(), (double)vertices[i].y),
                     std::min(Min.getz(), (double)vertices[i].z));
        Max = triple(std::max(Max.getx(), (double)vertices[i].x),
                     std::max(Max.gety(), (double)vertices[i].y),
                     std::max(Max.getz(), (double)vertices[i].z));
    }

    if (bbox2(Min, Max).offscreen()) {
        triOnscreen = false;
        S.clear();
        if (S_color) transparentData.renderCount = 0;
        else materialData.renderCount = 0;
        return;
    }

    if(drawMode == DRAWMODE_OUTLINE) {
        triple v[] = {
            triple(p1.x, p1.y, p1.z),
            triple(p2.x, p2.y, p2.z),
            triple(p3.x, p3.y, p3.z)
        };
        bool straight = true;
        double s = 1.0;
        double size2 = hypot(imageWidth, imageHeight);
        camp::pair size3(s * (sceneMaxBound.getx() - sceneMinBound.getx()),
                         s * (sceneMaxBound.gety() - sceneMinBound.gety()));
        triple edge0[] = { v[0], v[0], v[1], v[1] };
        C.queue(edge0, straight, size3.length() / size2);
        triple edge1[] = { v[1], v[1], v[2], v[2] };
        C.queue(edge1, straight, size3.length() / size2);
        triple edge2[] = { v[2], v[2], v[0], v[0] };
        C.queue(edge2, straight, size3.length() / size2);
        return;
    }

    bool transparent = false;
    if (drawMode == DRAWMODE_NORMAL && camp::materials && materialIndex < (int)camp::materials->size()) {
        transparent = (*camp::materials)[materialIndex].diffuse.a < 1.0f;
    }

    // Fast path: fully onscreen, same vertex format — just re-append S to global buffer.
    if (!remesh && triOnscreen && centerIndex == 0) {
        if (S_color) transparentData.extendColor(S);
        else materialData.extendMaterial(S);
        return;
    }

    // Slow path: rebuild S with per-triangle culling (algorithm §\ref{cull}).
    if (transparent) transparentData.renderCount = 0;
    else materialData.renderCount = 0;

    S.clear();
    triOnscreen = true;
    fullyOnscreen = true;
    triple tri[] = { p1, p2, p3 };
    if (bbox2(3, tri).offscreen()) { triOnscreen = false; fullyOnscreen = false; }

    S_color = transparent;
    if (transparent) {
        size_t base = S.colorVertices.size();
        S.colorVertices.push_back(ColorVertex{p1, normal, 1+materialIndex, glm::vec4(0,0,0,0)});
        S.colorVertices.push_back(ColorVertex{p2, normal, 1+materialIndex, glm::vec4(0,0,0,0)});
        S.colorVertices.push_back(ColorVertex{p3, normal, 1+materialIndex, glm::vec4(0,0,0,0)});
        if (fullyOnscreen) { S.indices.push_back(base); S.indices.push_back(base+1); S.indices.push_back(base+2); }
        transparentData.extendColor(S);
    } else {
        size_t base = S.materialVertices.size();
        S.materialVertices.push_back(MaterialVertex{p1, normal, materialIndex});
        S.materialVertices.push_back(MaterialVertex{p2, normal, materialIndex});
        S.materialVertices.push_back(MaterialVertex{p3, normal, materialIndex});
        if (fullyOnscreen) { S.indices.push_back(base); S.indices.push_back(base+1); S.indices.push_back(base+2); }
        materialData.extendMaterial(S);
    }

    if (drawMode == DRAWMODE_NORMAL) isTransparent = transparent;
}


V3dStraightPlanarQuadWithCornerColors::V3dStraightPlanarQuadWithCornerColors(
    xdr::ixstream& xdrFile, 
    V3D_BOOL doublePrecision)
    : V3dObject{ ObjectTypes::QUAD_COLOR } { 
        for (int i = 0; i < 4; ++i) {
            vertices[i].x = readReal(xdrFile, doublePrecision);
            vertices[i].y = readReal(xdrFile, doublePrecision);
            vertices[i].z = readReal(xdrFile, doublePrecision);
        }

        xdrFile >> centerIndex;
        V3dObject::centerIndex = centerIndex;
        xdrFile >> materialIndex;     

        for (int i = 0; i < 4; ++i) {
            xdrFile >> cornerColors[i].r;
            xdrFile >> cornerColors[i].g;
            xdrFile >> cornerColors[i].b;
            xdrFile >> cornerColors[i].a;
        }
    }

void V3dStraightPlanarQuadWithCornerColors::QueueMesh(int imageWidth, int imageHeight, triple sceneMinBound, triple sceneMaxBound, bool remesh, bool orthographic, DrawMode drawMode) {
    camp::materialIndex = materialIndex;

    TRIPLE p1 = vertices[0];
    TRIPLE p2 = vertices[1];
    TRIPLE p3 = vertices[2];
    TRIPLE p4 = vertices[3];

    TRIPLE A = p2 - p1;
    TRIPLE B = p3 - p1;

    glm::dvec3 normal = glm::normalize(glm::cross(A, B));

    // Offscreen cull: match algorithm §\ref{cull}.
    triple Min{vertices[0].x, vertices[0].y, vertices[0].z};
    triple Max{vertices[0].x, vertices[0].y, vertices[0].z};
    for (int i = 1; i < 4; ++i) {
        Min = triple(std::min(Min.getx(), (double)vertices[i].x),
                     std::min(Min.gety(), (double)vertices[i].y),
                     std::min(Min.getz(), (double)vertices[i].z));
        Max = triple(std::max(Max.getx(), (double)vertices[i].x),
                     std::max(Max.gety(), (double)vertices[i].y),
                     std::max(Max.getz(), (double)vertices[i].z));
    }

    if (bbox2(Min, Max).offscreen()) {
        quadOnscreen = false;
        S.clear();
        if (S_color) transparentData.renderCount = 0;
        else colorData.renderCount = 0;
        return;
    }

    if(drawMode == DRAWMODE_OUTLINE) {
        triple v[] = { p1, p2, p3, p4 };
        bool straight = true;
        double size2 = hypot(imageWidth, imageHeight);
        camp::pair size3(sceneMaxBound.getx() - sceneMinBound.getx(),
                         sceneMaxBound.gety() - sceneMinBound.gety());
        triple edge0[] = { v[0], v[0], v[1], v[1] };
        C.queue(edge0, straight, size3.length() / size2);
        triple edge1[] = { v[1], v[1], v[2], v[2] };
        C.queue(edge1, straight, size3.length() / size2);
        triple edge2[] = { v[2], v[2], v[3], v[3] };
        C.queue(edge2, straight, size3.length() / size2);
        triple edge3[] = { v[3], v[3], v[0], v[0] };
        C.queue(edge3, straight, size3.length() / size2);
        return;
    }

    bool transparent = (drawMode == DRAWMODE_NORMAL && cornerColors[0].a + cornerColors[1].a +
                        cornerColors[2].a + cornerColors[3].a < 4.0f);

    // Fast path: fully onscreen — just re-append S to global buffer.
    if (!remesh && quadOnscreen && centerIndex == 0) {
        if (transparent) transparentData.extendColor(S);
        else colorData.extendColor(S);
        return;
    }

    // Slow path: rebuild S with per-triangle culling (algorithm §\ref{cull}).
    if (transparent) transparentData.renderCount = 0;
    else colorData.renderCount = 0;

    S.clear();
    quadOnscreen = true;
    fullyOnscreen = true;

    RGBA c0{ cornerColors[0].r, cornerColors[0].g, cornerColors[0].b, cornerColors[0].a };
    RGBA c1{ cornerColors[1].r, cornerColors[1].g, cornerColors[1].b, cornerColors[1].a };
    RGBA c2{ cornerColors[2].r, cornerColors[2].g, cornerColors[2].b, cornerColors[2].a };
    RGBA c3{ cornerColors[3].r, cornerColors[3].g, cornerColors[3].b, cornerColors[3].a };

    glm::vec4 vc0{ c0.r, c0.g, c0.b, c0.a };
    glm::vec4 vc1{ c1.r, c1.g, c1.b, c1.a };
    glm::vec4 vc2{ c2.r, c2.g, c2.b, c2.a };
    glm::vec4 vc3{ c3.r, c3.g, c3.b, c3.a };

    int matIdx = -1 - (int)materialIndex;

    // Two triangles: T0=(p1,p2,p3), T1=(p1,p3,p4).
    triple tri0[] = { p1, p2, p3 };
    triple tri1[] = { p1, p3, p4 };
    bool t0Off = bbox2(3, tri0).offscreen();
    bool t1Off = bbox2(3, tri1).offscreen();
    if (t0Off || t1Off) { quadOnscreen = false; fullyOnscreen = false; }

    S_color = true;  // Always ColorVertex for corner-colored geometry
    size_t base = S.colorVertices.size();
    S.colorVertices.push_back(ColorVertex{p1, normal, matIdx, vc0});
    S.colorVertices.push_back(ColorVertex{p2, normal, matIdx, vc1});
    S.colorVertices.push_back(ColorVertex{p3, normal, matIdx, vc2});
    S.colorVertices.push_back(ColorVertex{p4, normal, matIdx, vc3});
    if (!t0Off) { S.indices.push_back(base); S.indices.push_back(base+1); S.indices.push_back(base+2); }
    if (!t1Off) { S.indices.push_back(base); S.indices.push_back(base+2); S.indices.push_back(base+3); }

    if (transparent) transparentData.extendColor(S);
    else colorData.extendColor(S);

    if (drawMode == DRAWMODE_NORMAL) isTransparent = transparent;
}


V3dStraightTriangleWithCornerColors::V3dStraightTriangleWithCornerColors(
    xdr::ixstream& xdrFile, 
    V3D_BOOL doublePrecision)
    : V3dObject{ ObjectTypes::TRIANGLE_COLOR } { 
        for (int i = 0; i < 3; ++i) {
            vertices[i].x = readReal(xdrFile, doublePrecision);
            vertices[i].y = readReal(xdrFile, doublePrecision);
            vertices[i].z = readReal(xdrFile, doublePrecision);
        }

        xdrFile >> centerIndex;
        V3dObject::centerIndex = centerIndex;
        xdrFile >> materialIndex;     

        for (int i = 0; i < 3; ++i) {
            xdrFile >> cornerColors[i].r;
            xdrFile >> cornerColors[i].g;
            xdrFile >> cornerColors[i].b;
            xdrFile >> cornerColors[i].a;
        }
    }

void V3dStraightTriangleWithCornerColors::QueueMesh(int imageWidth, int imageHeight, triple sceneMinBound, triple sceneMaxBound, bool remesh, bool orthographic, DrawMode drawMode) {
    camp::materialIndex = materialIndex;

    TRIPLE p1 = vertices[0];
    TRIPLE p2 = vertices[1];
    TRIPLE p3 = vertices[2];

    TRIPLE A = p2 - p1;
    TRIPLE B = p3 - p1;

    glm::dvec3 normal = glm::normalize(glm::cross(A, B));

    // Offscreen cull: match algorithm §\ref{cull}.
    triple Min{vertices[0].x, vertices[0].y, vertices[0].z};
    triple Max{vertices[0].x, vertices[0].y, vertices[0].z};
    for (int i = 1; i < 3; ++i) {
        Min = triple(std::min(Min.getx(), (double)vertices[i].x),
                     std::min(Min.gety(), (double)vertices[i].y),
                     std::min(Min.getz(), (double)vertices[i].z));
        Max = triple(std::max(Max.getx(), (double)vertices[i].x),
                     std::max(Max.gety(), (double)vertices[i].y),
                     std::max(Max.getz(), (double)vertices[i].z));
    }

    if (bbox2(Min, Max).offscreen()) {
        triOnscreen = false;
        S.clear();
        if (S_color) transparentData.renderCount = 0;
        else colorData.renderCount = 0;
        return;
    }

    if(drawMode == DRAWMODE_OUTLINE) {
        triple v[] = { p1, p2, p3 };
        bool straight = true;
        double size2 = hypot(imageWidth, imageHeight);
        camp::pair size3(sceneMaxBound.getx() - sceneMinBound.getx(),
                         sceneMaxBound.gety() - sceneMinBound.gety());
        triple edge0[] = { v[0], v[0], v[1], v[1] };
        C.queue(edge0, straight, size3.length() / size2);
        triple edge1[] = { v[1], v[1], v[2], v[2] };
        C.queue(edge1, straight, size3.length() / size2);
        triple edge2[] = { v[2], v[2], v[0], v[0] };
        C.queue(edge2, straight, size3.length() / size2);
        return;
    }

    bool transparent = (drawMode == DRAWMODE_NORMAL && cornerColors[0].a +
                        cornerColors[1].a + cornerColors[2].a < 3.0f);

    // Fast path: fully onscreen — just re-append S to global buffer.
    if (!remesh && triOnscreen && centerIndex == 0) {
        if (transparent) transparentData.extendColor(S);
        else colorData.extendColor(S);
        return;
    }

    // Slow path: rebuild S with per-triangle culling (algorithm §\ref{cull}).
    if (transparent) transparentData.renderCount = 0;
    else colorData.renderCount = 0;

    S.clear();
    triOnscreen = true;
    fullyOnscreen = true;
    triple tri[] = { p1, p2, p3 };
    if (bbox2(3, tri).offscreen()) { triOnscreen = false; fullyOnscreen = false; }

    RGBA c0{ cornerColors[0].r, cornerColors[0].g, cornerColors[0].b, cornerColors[0].a };
    RGBA c1{ cornerColors[1].r, cornerColors[1].g, cornerColors[1].b, cornerColors[1].a };
    RGBA c2{ cornerColors[2].r, cornerColors[2].g, cornerColors[2].b, cornerColors[2].a };

    glm::vec4 vc0{ c0.r, c0.g, c0.b, c0.a };
    glm::vec4 vc1{ c1.r, c1.g, c1.b, c1.a };
    glm::vec4 vc2{ c2.r, c2.g, c2.b, c2.a };

    int matIdx = -1 - (int)materialIndex;

    S_color = true;  // Always ColorVertex for corner-colored geometry
    size_t base = S.colorVertices.size();
    S.colorVertices.push_back(ColorVertex{p1, normal, matIdx, vc0});
    S.colorVertices.push_back(ColorVertex{p2, normal, matIdx, vc1});
    S.colorVertices.push_back(ColorVertex{p3, normal, matIdx, vc2});
    if (fullyOnscreen) { S.indices.push_back(base); S.indices.push_back(base+1); S.indices.push_back(base+2); }

    if (transparent) transparentData.extendColor(S);
    else colorData.extendColor(S);

    if (drawMode == DRAWMODE_NORMAL) isTransparent = transparent;
}


V3dTriangleGroup::V3dTriangleGroup(
    xdr::ixstream& xdrFile, 
    V3D_BOOL doublePrecision)
    : V3dObject{ ObjectTypes::TRIANGLES } { 
        nI = 0;
        xdrFile >> nI;

        nP = 0;
        xdrFile >> nP;

        // Sanity caps to prevent bad_alloc or infinite loops from crafted files.
        constexpr UINT maxCount = 10000000;
        if (nI > maxCount || nP > maxCount) {
            std::cout << "WARNING: V3dTriangleGroup count exceeds limit (nI=" << nI << ", nP=" << nP << "), skipping." << std::endl;
            nI = 0;
            nP = 0;
            return;
        }

        vertexPositions.resize(nP);
        for (UINT i = 0; i < nP; ++i) {
            vertexPositions[i].x = readReal(xdrFile, doublePrecision);
            vertexPositions[i].y = readReal(xdrFile, doublePrecision);
            vertexPositions[i].z = readReal(xdrFile, doublePrecision);
        }

        nN = 0;
        xdrFile >> nN;
        if (nN > maxCount) {
            std::cout << "WARNING: V3dTriangleGroup normal count exceeds limit, capping." << std::endl;
            nN = 0;
        }
        vertexNormalArray.resize(nN);
        for (UINT i = 0; i < nN; ++i) {
            vertexNormalArray[i].x = readReal(xdrFile, doublePrecision);
            vertexNormalArray[i].y = readReal(xdrFile, doublePrecision);
            vertexNormalArray[i].z = readReal(xdrFile, doublePrecision);
        }

        xdrFile >> explicitNI;

        xdrFile >> nC;
        if (nC > maxCount) {
            std::cout << "WARNING: V3dTriangleGroup color count exceeds limit, capping." << std::endl;
            nC = 0;
        }
        if (nC > 0) {
            vertexColorArray.resize(nC);
            for (UINT i = 0; i < nC; ++i) {
                xdrFile >> vertexColorArray[i].r;
                xdrFile >> vertexColorArray[i].g;
                xdrFile >> vertexColorArray[i].b;
                xdrFile >> vertexColorArray[i].a;
            }

            xdrFile >> explicitCI;
        }

        positionIndices.resize(nI);
        normalIndices.resize(nI);
        colorIndices.resize(nI);

        for (UINT i = 0; i < nI; ++i) {
            xdrFile >> positionIndices[i][0];
            xdrFile >> positionIndices[i][1];
            xdrFile >> positionIndices[i][2];

            if (explicitNI) {
                xdrFile >> normalIndices[i][0];
                xdrFile >> normalIndices[i][1];
                xdrFile >> normalIndices[i][2];
            } else {
                normalIndices[i] = positionIndices[i];
            }

            if (nC > 0 && explicitCI) {
                xdrFile >> colorIndices[i][0];
                xdrFile >> colorIndices[i][1];
                xdrFile >> colorIndices[i][2];
            } else {
                colorIndices[i] = positionIndices[i];
            }
        }

        xdrFile >> centerIndex;
        V3dObject::centerIndex = centerIndex;
        xdrFile >> materialIndex;
    }

void V3dTriangleGroup::QueueMesh(int imageWidth, int imageHeight, triple sceneMinBound, triple sceneMaxBound, bool remesh, bool orthographic, DrawMode drawMode) {
    camp::materialIndex = materialIndex;

    if (nI == 0 || nP == 0) return;

    // Match Asymptote drawTriangles::render(): only detect transparency in NORMAL mode.
    // WIREFRAME/OUTLINE force opaque (commit 316f906894).
    // In OUTLINE mode, skip rendering entirely (drawTriangles has no outline path).
    if (drawMode == DRAWMODE_OUTLINE) {
        return;
    }

    // Offscreen cull: match algorithm §\ref{cull}.
    triple Min = sceneMinBound;
    triple Max = sceneMaxBound;
    if (bbox2(Min, Max).offscreen()) {
        fullyOnscreen = false;
        vertexData.clear();
        // Match Asymptote: S.notRendered() ensures upload gate fires when object comes back onscreen.
        transparentData.renderCount = 0;
        colorData.renderCount = 0;
        return;
    }

    bool transparent = false;
    if (drawMode == DRAWMODE_NORMAL && camp::materials && materialIndex < (int)camp::materials->size()) {
        transparent = (*camp::materials)[materialIndex].diffuse.a < 1.0f;
    }

    if (!remesh && fullyOnscreen && centerIndex == 0) {
        // Fast path: already onscreen, just re-append.
        if (transparent)
            transparentData.extendColor(vertexData);
        else
            colorData.extendColor(vertexData);
        return;
    }

    // Match Asymptote Triangles::notRendered() — reset renderCount so upload gate fires.
    if (transparent)
        transparentData.renderCount = 0;
    else
        colorData.renderCount = 0;

    vertexData.clear();

    // Match Asymptote Triangles::queue(): with per-vertex colors, store as ColorVertex
    // and check per-triangle corner alpha (transparent |= c0+c1+c2 < 3.0).
    // MaterialIndex = nC ? -1-materialIndex : 1+materialIndex.
    int materialIdx = (nC > 0) ? -1-(int)materialIndex : 1+(int)materialIndex;

    if (nC > 0) {
        // Per-vertex colors: use ColorVertex with actual RGBA values.
        std::vector<ColorVertex> colVertices;
        colVertices.reserve(nP);

        for (size_t i = 0; i < nP; ++i) {
            TRIPLE pos = vertexPositions[i];
            TRIPLE norm = (i < nN) ? vertexNormalArray[i] : TRIPLE{0.0, 0.0, 1.0};
            glm::vec4 col(0, 0, 0, 0);
            // Find a color index referencing this vertex (approximate: check first triangle)
            // Asymptote stores colors per-triangle-vertex; we need to find the right CI.
            // For simplicity, use position index as color index fallback.
            if (i < nC) {
                col = glm::vec4(vertexColorArray[i].r, vertexColorArray[i].g,
                                vertexColorArray[i].b, vertexColorArray[i].a);
            }
            colVertices.push_back(ColorVertex{pos, norm, materialIdx, col});
        }

        // Check per-triangle corner alpha (Asymptote: transparent |= c0[3]+c1[3]+c2[3] < 3.0)
        for (size_t i = 0; i < nI; ++i) {
            std::array<unsigned int, 3> CI = colorIndices[i];
            if (CI[0] < nC && CI[1] < nC && CI[2] < nC) {
                float aSum = vertexColorArray[CI[0]].a + vertexColorArray[CI[1]].a +
                             vertexColorArray[CI[2]].a;
                if (aSum < 3.0f) { transparent = true; break; }
            }
        }

        std::vector<unsigned int> outIndices;
        outIndices.reserve(nI * 3);
        for (size_t i = 0; i < nI; ++i) {
            std::array<unsigned int, 3> PI = positionIndices[i];
            if (PI[0] >= nP || PI[1] >= nP || PI[2] >= nP) continue;
            outIndices.push_back(PI[0]);
            outIndices.push_back(PI[1]);
            outIndices.push_back(PI[2]);
        }

        VertexBuffer colBuffer;
        colBuffer.colorVertices = colVertices;
        colBuffer.indices = outIndices;
        vertexData = colBuffer;
        if (transparent)
            transparentData.extendColor(colBuffer);
        else
            colorData.extendColor(colBuffer);
    } else {
        // No per-vertex colors: use MaterialVertex.
        std::vector<MaterialVertex> matVertices;
        matVertices.reserve(nP);

        for (size_t i = 0; i < nP; ++i) {
            TRIPLE pos = vertexPositions[i];
            TRIPLE norm = (i < nN) ? vertexNormalArray[i] : TRIPLE{0.0, 0.0, 1.0};
            matVertices.push_back(MaterialVertex{pos, norm, materialIdx});
        }

        std::vector<unsigned int> outIndices;
        outIndices.reserve(nI * 3);
        for (size_t i = 0; i < nI; ++i) {
            std::array<unsigned int, 3> PI = positionIndices[i];
            if (PI[0] >= nP || PI[1] >= nP || PI[2] >= nP) continue;
            outIndices.push_back(PI[0]);
            outIndices.push_back(PI[1]);
            outIndices.push_back(PI[2]);
        }

        VertexBuffer buffer;
        // Convert to ColorVertex — Asymptote Triangles::append() always uses color path.
        std::vector<ColorVertex> colVertices;
        colVertices.reserve(matVertices.size());
        for (auto& mv : matVertices) {
            colVertices.push_back(ColorVertex{mv.position, mv.normal, materialIdx, glm::vec4(0,0,0,0)});
        }
        VertexBuffer colBuffer;
        colBuffer.colorVertices = std::move(colVertices);
        colBuffer.indices = outIndices;
        vertexData = colBuffer;
        if (transparent)
            transparentData.extendColor(colBuffer);
        else
            colorData.extendColor(colBuffer);
    }

    fullyOnscreen = true;
    if (drawMode == DRAWMODE_NORMAL)
        isTransparent = transparent;
}


class Align {
private:
    triple center;

    double ct = 0.0;
    double st = 0.0;
    double cp = 0.0;
    double sp = 0.0;

public:
    Align(const triple& center, const triple* dir = nullptr)
        : center(center)
    {
        if(dir) {
            double theta = (*dir).getx();
            double phi   = (*dir).gety();

            ct = std::cos(theta);
            st = std::sin(theta);

            cp = std::cos(phi);
            sp = std::sin(phi);
        }
    }

    triple T0(const triple& v) const {
        return triple(
            v.getx() + center.getx(),
            v.gety() + center.gety(),
            v.getz() + center.getz()
        );
    }

    triple T(const triple& v) const {
        double x = v.getx();
        double Y = v.gety();
        double z = v.getz();

        double X = x * ct + z * st;

        return triple(
            X * cp - Y * sp + center.getx(),
            X * sp + Y * cp + center.gety(),
            -x * st + z * ct + center.getz()
        );
    }
};

// ---------------------------------------------------------------------------
// Sphere-octant helpers (from three_octant.asy / gl.ts).
// The first-octant triangles are computed once and cached, exactly like
// gl.ts's IIFE `const sphereOctant`.
// ---------------------------------------------------------------------------

static inline double dot3(const triple& a, const triple& b) {
    return a.getx()*b.getx() + a.gety()*b.gety() + a.getz()*b.getz();
}

static inline double norm3(const triple& v) {
    return std::sqrt(dot3(v, v));
}

static inline triple unit3(const triple& v) {
    double n = norm3(v);
    return triple(v.getx()/n, v.gety()/n, v.getz()/n);
}

// Midpoint of great-circle arc from P to Q.
static inline triple gcMidPoint(triple P, triple Q) {
    double scale = 1.0 / (std::sqrt(2.0) * std::sqrt(1.0 + dot3(P, Q)));
    return triple((P.getx()+Q.getx())*scale,
                  (P.gety()+Q.gety())*scale,
                  (P.getz()+Q.getz())*scale);
}

// Optimal cubic Bezier edge control points for great-circle arc P → Q.
static inline std::array<triple, 2> bezierEdge(triple P, triple Q) {
    double x = dot3(P, Q);
    double k = (4.0/3.0)*std::sqrt(1.0-x)/
               (std::sqrt(2.0)+std::sqrt(1.0+x));
    triple u = unit3(triple(Q.getx()-x*P.getx(),
                            Q.gety()-x*P.gety(),
                            Q.getz()-x*P.getz()));
    triple v = unit3(triple(P.getx()-x*Q.getx(),
                            P.gety()-x*Q.gety(),
                            P.getz()-x*Q.getz()));
    return {{
        triple(P.getx()+k*u.getx(), P.gety()+k*u.gety(), P.getz()+k*u.getz()),
        triple(Q.getx()+k*v.getx(), Q.gety()+k*v.gety(), Q.getz()+k*v.getz())
    }};
}

// Build a single Bezier triangle (10 control points) for spherical triangle ABC.
// Interior point tuned so the barycentric centroid lies on the unit sphere.
static std::array<TRIPLE, 10> makeBezierTriangle(triple A, triple B, triple C) {
    auto ab = bezierEdge(A, B);
    auto bc = bezierEdge(B, C);
    auto ca = bezierEdge(C, A);

    triple S9 = triple(
        A.getx()+B.getx()+C.getx() + 3.0*(ab[0].getx()+ab[1].getx()+bc[0].getx()+bc[1].getx()+ca[0].getx()+ca[1].getx()),
        A.gety()+B.gety()+C.gety() + 3.0*(ab[0].gety()+ab[1].gety()+bc[0].gety()+bc[1].gety()+ca[0].gety()+ca[1].gety()),
        A.getz()+B.getz()+C.getz() + 3.0*(ab[0].getz()+ab[1].getz()+bc[0].getz()+bc[1].getz()+ca[0].getz()+ca[1].getz())
    );
    triple dir = unit3(triple(A.getx()+B.getx()+C.getx(),
                              A.gety()+B.gety()+C.gety(),
                              A.getz()+B.getz()+C.getz()));
    double dotSD = dot3(S9, dir);
    double disc = 144.0*dotSD*dotSD - 144.0*(dot3(S9,S9) - 729.0);
    if (disc < 0) disc = 0;
    double R = (-12.0*dotSD + std::sqrt(disc)) / 72.0;
    triple p9 = triple(R*dir.getx(), R*dir.gety(), R*dir.getz());

    // Row-major: A, ab[0], ca[1], ab[1], p9, ca[0], B, bc[0], bc[1], C
    return {{
        TRIPLE(A.getx(),     A.gety(),     A.getz()),
        TRIPLE(ab[0].getx(), ab[0].gety(), ab[0].getz()),
        TRIPLE(ca[1].getx(), ca[1].gety(), ca[1].getz()),
        TRIPLE(ab[1].getx(), ab[1].gety(), ab[1].getz()),
        TRIPLE(p9.getx(),    p9.gety(),    p9.getz()),
        TRIPLE(ca[0].getx(), ca[0].gety(), ca[0].getz()),
        TRIPLE(B.getx(),     B.gety(),     B.getz()),
        TRIPLE(bc[0].getx(), bc[0].gety(), bc[0].getz()),
        TRIPLE(bc[1].getx(), bc[1].gety(), bc[1].getz()),
        TRIPLE(C.getx(),     C.gety(),     C.getz())
    }};
}

// Recursively subdivide spherical triangle ABC into 4^depth Bezier triangles.
static void subdivideTriangle(triple A, triple B, triple C, int depth,
                              std::vector<std::array<TRIPLE,10>>& result) {
    if (depth == 0) {
        result.push_back(makeBezierTriangle(A, B, C));
        return;
    }
    triple midAB = gcMidPoint(A, B);
    triple midBC = gcMidPoint(B, C);
    triple midCA = gcMidPoint(C, A);

    subdivideTriangle(A,     midAB, midCA, depth-1, result);
    subdivideTriangle(B,     midBC, midAB, depth-1, result);
    subdivideTriangle(C,     midCA, midBC, depth-1, result);
    subdivideTriangle(midAB, midBC, midCA, depth-1, result);
}

// Cached first-octant triangles (depth=2 → 16 triangles).
// Computed once at first use, matching gl.ts's IIFE `sphereOctant`.
static const std::vector<std::array<TRIPLE,10>>& getSphereOctant() {
    static std::vector<std::array<TRIPLE,10>> octant;
    if (octant.empty()) {
        subdivideTriangle(triple(1,0,0), triple(0,1,0), triple(0,0,1), 2, octant);
    }
    return octant;
}

V3dSphere::V3dSphere(
    xdr::ixstream& xdrFile, 
    V3D_BOOL doublePrecision)
    : V3dObject{ ObjectTypes::SPHERE } { 
        center.x = readReal(xdrFile, doublePrecision);
        center.y = readReal(xdrFile, doublePrecision);
        center.z = readReal(xdrFile, doublePrecision);

        radius = readReal(xdrFile, doublePrecision);

        xdrFile >> centerIndex;
        V3dObject::centerIndex = centerIndex;
        xdrFile >> materialIndex;
    }

void sphere(
    triple center,
    double r,
    triple* dir,
    int imageWidth,
    int imageHeight,
    triple sceneMinBound,
    triple sceneMaxBound,
    bool remesh,
    bool orthographic,
    DrawMode drawMode,
    UINT centerIndex,
    UINT materialIndex
) {
    // Cached first-octant triangles (16 Bezier triangles, depth=2).
    const auto& octantTriangles = getSphereOctant();

    Align A(center, dir);

    int s;
    std::function<TRIPLE(const TRIPLE&)> t;

    if (dir) {
        s = 1;
        t = [&](const TRIPLE& v) { return A.T(v); };
    } else {
        s = -1;
        t = [&](const TRIPLE& v) { return A.T0(v); };
    }

    // Reflect across coordinate planes to cover all octants (mirrors gl.ts).
    for (int ix = -1; ix <= 1; ix += 2) {
        double rx = ix * r;
        for (int iy = -1; iy <= 1; iy += 2) {
            double ry = iy * r;
            for (int iz = s; iz <= 1; iz += 2) {
                double rz = iz * r;
                for (const auto& tri : octantTriangles) {
                    std::array<TRIPLE, 10> p;
                    for (int i = 0; i < 10; ++i) {
                        p[i] = t(TRIPLE(rx*tri[i].x, ry*tri[i].y, rz*tri[i].z));
                    }
                    V3dBezierTriangle triangle{p, centerIndex, materialIndex};
                    triangle.QueueMesh(imageWidth, imageHeight, sceneMinBound,
                                       sceneMaxBound, true, orthographic, drawMode);
                }
            }
        }
    }
}

void V3dSphere::QueueMesh(int imageWidth, int imageHeight, triple sceneMinBound, triple sceneMaxBound, bool remesh, bool orthographic, DrawMode drawMode) {
    camp::materialIndex = materialIndex;

    triple* dir = nullptr;
    double r = radius;
    
    sphere(center, r, dir, imageWidth, imageHeight, sceneMinBound, sceneMaxBound, remesh, orthographic, drawMode, centerIndex, materialIndex);
}


V3dHemiSphere::V3dHemiSphere(
    xdr::ixstream& xdrFile, 
    V3D_BOOL doublePrecision)
    : V3dObject{ ObjectTypes::HALF_SPHERE } { 
        center.x = readReal(xdrFile, doublePrecision);
        center.y = readReal(xdrFile, doublePrecision);
        center.z = readReal(xdrFile, doublePrecision);

        radius = readReal(xdrFile, doublePrecision);

        xdrFile >> centerIndex;
        V3dObject::centerIndex = centerIndex;
        xdrFile >> materialIndex;    

        polarAngle = readReal(xdrFile, doublePrecision);
        azimuthalAngle = readReal(xdrFile, doublePrecision);

    }

void V3dHemiSphere::QueueMesh(int imageWidth, int imageHeight, triple sceneMinBound, triple sceneMaxBound, bool remesh, bool orthographic, DrawMode drawMode) {
    camp::materialIndex = materialIndex;

    triple direction{ polarAngle, azimuthalAngle, 0.0 };
    double r = radius;
    
    sphere(center, r, &direction, imageWidth, imageHeight, sceneMinBound, sceneMaxBound, remesh, orthographic, drawMode, centerIndex, materialIndex);
}

void disk(
    triple center,
    double r,
    UINT centerIndex,
    UINT materialIndex,
    triple* dir,
    bool core,
    int imageWidth, 
    int imageHeight, 
    triple sceneMinBound, 
    triple sceneMaxBound, 
    bool remesh,
    bool orthographic,
    DrawMode drawMode
) {
    double a = 4.0/3.0*(std::sqrt(2.0)-1.0);
    double b=1.0-2.0*a/3.0;

    std::array<triple, 16> unitdisk={
        triple{1,0,0},
        triple{1,-a,0},
        triple{a,-1,0},
        triple{0,-1,0},

        triple{1,a,0},
        triple{b,0,0},
        triple{0,-b,0},
        triple{-a,-1,0},

        triple{a,1,0},
        triple{0,b,0},
        triple{-b,0,0},
        triple{-1,-a,0},

        triple{0,1,0},
        triple{-a,1,0},
        triple{-1,a,0},
        triple{-1,0,0}
    };

    Align A{center,dir};

    auto TPatch = [&](const std::array<triple, 16>& V) {
        std::array<TRIPLE, 16> p{ };
        for(int i=0; i < V.size(); ++i) {
            TRIPLE v{V[i].getx(), V[i].gety(), V[i].getz()};
            p[i]=A.T(triple{r*v.x, r*v.y, 0.0});
        }
        return p;
    };

    V3dBezierPatch patch{ TPatch(unitdisk), centerIndex, materialIndex };
    patch.QueueMesh(imageWidth, imageHeight, sceneMinBound, sceneMaxBound, true, orthographic, drawMode);
}


V3dDisk::V3dDisk(
    xdr::ixstream& xdrFile, 
    V3D_BOOL doublePrecision)
    : V3dObject{ ObjectTypes::DISK } { 
        center.x = readReal(xdrFile, doublePrecision);
        center.y = readReal(xdrFile, doublePrecision);
        center.z = readReal(xdrFile, doublePrecision);

        radius = readReal(xdrFile, doublePrecision);

        xdrFile >> centerIndex;
        V3dObject::centerIndex = centerIndex;
        xdrFile >> materialIndex;    

        polarAngle = readReal(xdrFile, doublePrecision);
        azimuthalAngle = readReal(xdrFile, doublePrecision);
    }

void V3dDisk::QueueMesh(int imageWidth, int imageHeight, triple sceneMinBound, triple sceneMaxBound, bool remesh, bool orthographic, DrawMode drawMode) {
    camp::materialIndex = materialIndex;

    triple direction{ polarAngle, azimuthalAngle, 0.0 };

    disk(center, radius, centerIndex, materialIndex, &direction, false, imageWidth, imageHeight, sceneMinBound, sceneMaxBound, remesh, orthographic, drawMode);
}

void cylinder(
    triple center,
    double r,
    double h,
    UINT centerIndex,
    UINT materialIndex,
    triple* dir,
    bool core,
    int imageWidth, 
    int imageHeight, 
    triple sceneMinBound, 
    triple sceneMaxBound, 
    bool remesh, 
    bool orthographic,
    DrawMode drawMode
) {
    double a = 4.0/3.0*(std::sqrt(2.0)-1.0);

    std::array<triple, 16> unitcylinder = {
        triple{1,0,0},
        triple{1,0,1/3},
        triple{1,0,2/3},
        triple{1,0,1},

        triple{1,a,0},
        triple{1,a,1/3},
        triple{1,a,2/3},
        triple{1,a,1},

        triple{a,1,0},
        triple{a,1,1/3},
        triple{a,1,2/3},
        triple{a,1,1},

        triple{0,1,0},
        triple{0,1,1/3},
        triple{0,1,2/3},
        triple{0,1,1}
    };

    double rx,ry,rz;
    Align A{center,dir};

    std::function<triple(const triple&)> t = [&](const triple& v) { return A.T(v); };

    for(int i=-1; i <= 1; i += 2) {
        rx=i*r;
        for(int j=-1; j <= 1; j += 2) {
            ry=j*r;
            auto TPatch = [&](const std::array<triple, 16>& V) {
                std::array<TRIPLE, 16> p{ };

                for(size_t i = 0; i < V.size(); ++i) {
                    TRIPLE v{V[i].getx(), V[i].gety(), V[i].getz()};
                    p[i] = t(triple(rx * v.x, ry * v.y, h * v.z));
                }

                return p;
            };

            V3dBezierPatch patch{ TPatch(unitcylinder), centerIndex, materialIndex };
            patch.QueueMesh(imageWidth, imageHeight, sceneMinBound, sceneMaxBound, true, orthographic, drawMode);
        }
    }

    if(core) {
        triple Center=A.T(triple{0,0,h});
        std::array<TRIPLE, 2> endpoints = {
            TRIPLE{ center.getx(), center.gety(), center.getz() },
            TRIPLE{ Center.getx(), Center.gety(), Center.getz() }
        };

        V3dLineSegment line{ endpoints, centerIndex, materialIndex };
        line.QueueMesh(imageWidth, imageHeight, sceneMinBound, sceneMaxBound, true, orthographic, drawMode);
    }
}


V3dCylinder::V3dCylinder(
    xdr::ixstream& xdrFile, 
    V3D_BOOL doublePrecision)
    : V3dObject{ ObjectTypes::CYLINDER } { 
        center.x = readReal(xdrFile, doublePrecision);
        center.y = readReal(xdrFile, doublePrecision);
        center.z = readReal(xdrFile, doublePrecision);

        radius = readReal(xdrFile, doublePrecision);

        height = readReal(xdrFile, doublePrecision);

        xdrFile >> centerIndex;
        V3dObject::centerIndex = centerIndex;
        xdrFile >> materialIndex;    

        polarAngle = readReal(xdrFile, doublePrecision);
        azimuthalAngle = readReal(xdrFile, doublePrecision);

        xdrFile >> core;
    }

void V3dCylinder::QueueMesh(int imageWidth, int imageHeight, triple sceneMinBound, triple sceneMaxBound, bool remesh, bool orthographic, DrawMode drawMode) {
    camp::materialIndex = materialIndex;

    double r = radius;
    triple direction{ polarAngle, azimuthalAngle, 0.0 };

    cylinder(center, radius, height, centerIndex, materialIndex, &direction, core, imageWidth, imageHeight, sceneMinBound, sceneMaxBound, remesh, orthographic, drawMode);
}

class Rmf {
public:
    triple r;
    triple s;
    triple p;
    triple t;

    Rmf(triple p = triple{}, triple r = triple{}, triple t = triple{}) 
        : p{ p }, r{ r }, t{ t } {
        s = cross(t, r);
    }
};

std::vector<Rmf> rmf(triple z0,triple c0,triple c1,triple z1,std::vector<double> t) {
    // Return a unit vector perpendicular to a given unit vector v.
    auto perp = [&](triple v){
        triple u=cross(v,triple{0.0,1.0,0.0});
        double norm=std::numeric_limits<double>::epsilon()*abs2(v);
        if(abs2(u) > norm) return unit(u);
        u=cross(v,triple{0,0,1});
        return (abs2(u) > norm) ? unit(u) : triple{1,0,0};
    };

    double norm=std::numeric_limits<double>::epsilon()*glm::max(glm::max(glm::max(abs2(z0),abs2(c0)),abs2(c1)),abs2(z1));

    // Special case of dir for t in (0,1].
    auto d = [&](double t) {
        if(t == 1) {
            triple dir{z1.getx()-c1.getx(),
                    z1.gety()-c1.gety(),
                    z1.getz()-c1.getz()};
            if(abs2(dir) > norm) return unit(dir);
            dir=triple{2*c1.getx()-c0.getx()-z1.getx(),
                2*c1.gety()-c0.gety()-z1.gety(),
                2*c1.getz()-c0.getz()-z1.getz()};
            if(abs2(dir) > norm) return unit(dir);
            return triple{z1.getx()-z0.getx()+3*(c0.getx()-c1.getx()),
                    z1.gety()-z0.gety()+3*(c0.gety()-c1.gety()),
                    z1.getz()-z0.getz()+3*(c0.getz()-c1.getz())};
        }
        triple a{z1.getx()-z0.getx()+3*(c0.getx()-c1.getx()),
            z1.gety()-z0.gety()+3*(c0.gety()-c1.gety()),
            z1.getz()-z0.getz()+3*(c0.getz()-c1.getz())};
        triple b{2*(z0.getx()+c1.getx())-4*c0.getx(),
            2*(z0.gety()+c1.gety())-4*c0.gety(),
            2*(z0.getz()+c1.getz())-4*c0.getz()};
        triple c{c0.getx()-z0.getx(),c0.gety()-z0.gety(),c0.getz()-z0.getz()};
        double t2=t*t;
        triple dir{a.getx()*t2+b.getx()*t+c.getx(),
                a.gety()*t2+b.gety()*t+c.gety(),
                a.getz()*t2+b.getz()*t+c.getz()};
        if(abs2(dir) > norm) return unit(dir);
        t2=2*t;
        dir=triple{a.getx()*t2+b.getx(),
            a.gety()*t2+b.gety(),
            a.getz()*t2+b.getz()};
        if(abs2(dir) > norm) return unit(dir);
        return unit(a);
    };

    std::vector<Rmf> R{t.size()};
    triple T{c0.getx()-z0.getx(),
            c0.gety()-z0.gety(),
            c0.getz()-z0.getz()};
    if(abs2(T) < norm) {
        T=triple{z0.getx()-2*c0.getx()+c1.getx(),
        z0.gety()-2*c0.gety()+c1.gety(),
        z0.getz()-2*c0.getz()+c1.getz()};
        if(abs2(T) < norm)
        T=triple{z1.getx()-z0.getx()+3*(c0.getx()-c1.getx()),
            z1.gety()-z0.gety()+3*(c0.gety()-c1.gety()),
            z1.getz()-z0.getz()+3*(c0.getz()-c1.getz())};
    }
    T=unit(T);
    triple Tp=perp(T);
    R[0]=Rmf{z0,Tp,T};
    for(int i=1; i < t.size(); ++i) {
        Rmf Ri=R[i-1];
        double s=t[i];
        double onemt=1-s;
        double onemt2=onemt*onemt;
        double onemt3=onemt2*onemt;
        double s3=3*s;
        onemt2 *= s3;
        onemt *= s3*s;
        double t3=s*s*s;
        triple p{
            onemt3*z0.getx()+onemt2*c0.getx()+onemt*c1.getx()+t3*z1.getx(),
            onemt3*z0.gety()+onemt2*c0.gety()+onemt*c1.gety()+t3*z1.gety(),
            onemt3*z0.getz()+onemt2*c0.getz()+onemt*c1.getz()+t3*z1.getz()};
        triple v1{p.getx()-Ri.p.getx(),p.gety()-Ri.p.gety(),p.getz()-Ri.p.getz()};
        if(v1.getx() != 0 || v1.gety() != 0 || v1.getz() != 0) {
            triple r=Ri.r;
            triple u1=unit(v1);
            triple ti=Ri.t;
            double dotu1ti=dot(u1,ti);
            triple tp=triple{ti.getx()-2*dotu1ti*u1.getx(),
                    ti.gety()-2*dotu1ti*u1.gety(),
                    ti.getz()-2*dotu1ti*u1.getz()};
            ti=d(s);
            double dotu1r2=2*dot(u1,r);
            triple rp{r.getx()-dotu1r2*u1.getx(),r.gety()-dotu1r2*u1.gety(),r.getz()-dotu1r2*u1.getz()};
            triple u2=unit(triple{ti.getx()-tp.getx(),ti.gety()-tp.gety(),ti.getz()-tp.getz()});
            double dotu2rp2=2*dot(u2,rp);
            rp=triple{rp.getx()-dotu2rp2*u2.getx(),rp.gety()-dotu2rp2*u2.gety(),rp.getz()-dotu2rp2*u2.getz()};
            R[i]=Rmf{p,unit(rp),unit(ti)};
        } else
            R[i]=R[i-1];
    }
    return R;
}

// draw a tube of width w using control points v
void tube(
    std::array<triple, 4> v,
    double w,
    UINT centerIndex,
    UINT materialIndex,
    bool core,
    int imageWidth, 
    int imageHeight, 
    triple sceneMinBound, 
    triple sceneMaxBound, 
    bool remesh, 
    bool orthographic,
    DrawMode drawMode
) {

    std::vector<Rmf> r=rmf(v[0],v[1],v[2],v[3],std::vector<double>{0.0,1.0/3.0,2.0/3.0,1.0});

    double a = 4.0/3.0*(std::sqrt(2.0)-1.0);
    double aw=a*w;
    std::array<triple, 4> arc={triple{w,0,0},triple{w,aw,0},triple{aw,w,0},triple{0,w,0}};

    auto f = [&](double a,double b,double c,double d) {
        std::array<triple, 16> s={};
        for(int i=0; i < 4; ++i) {
            Rmf R{r[i]};

            double R0=R.r.getx(), R1=R.s.getx();
            double T0=R0*a+R1*b;
            double T1=R0*c+R1*d;

            R0=R.r.gety(); R1=R.s.gety();
            double T4=R0*a+R1*b;
            double T5=R0*c+R1*d;

            R0=R.r.getz(); R1=R.s.getz();
            double T8=R0*a+R1*b;
            double T9=R0*c+R1*d;

            triple w=v[i];
            double w0=w.getx(), w1=w.gety(), w2=w.getz();
            for(int j=0; j < 4; ++j) {
                triple u=arc[j];
                double x=u.getx(), y=u.gety();
                s[4*i+j]=triple{T0*x+T1*y+w0,
                        T4*x+T5*y+w1,
                        T8*x+T9*y+w2};
            }
        }
        // P.push(new BezierPatch(s,CenterIndex,MaterialIndex));

        auto Convert = [&](std::array<triple, 16> controlPoints) {
            std::array<TRIPLE, 16> newControlPoints;
            for (int i = 0; i < 16; ++i) {
                newControlPoints[i] = TRIPLE{ 
                    controlPoints[i].getx(),
                    controlPoints[i].gety(),
                    controlPoints[i].getz()
                };
            }

            return newControlPoints;
        };

        V3dBezierPatch patch{ Convert(s), centerIndex, materialIndex };
        patch.QueueMesh(imageWidth, imageHeight, sceneMinBound, sceneMaxBound, true, orthographic, drawMode);

    };

    f(1.0,0.0,0.0,1.0);
    f(0.0,-1.0,1.0,0.0);
    f(-1.0,0.0,0.0,-1.0);
    f(0.0,1.0,-1.0,0.0);

    if(core) {
        std::array<TRIPLE, 4> curveControlPoints = {
            TRIPLE{ v[0].getx(), v[0].gety(), v[0].getz() },
            TRIPLE{ v[1].getx(), v[1].gety(), v[1].getz() },
            TRIPLE{ v[2].getx(), v[2].gety(), v[2].getz() },
            TRIPLE{ v[3].getx(), v[3].gety(), v[3].getz() }
        };

        V3dBezierCurve curve{ curveControlPoints, centerIndex, materialIndex };
        curve.QueueMesh(imageWidth, imageHeight, sceneMinBound, sceneMaxBound, true, orthographic, drawMode);
    }
}

V3dTube::V3dTube(
    xdr::ixstream& xdrFile, 
    V3D_BOOL doublePrecision)
    : V3dObject{ ObjectTypes::TUBE } { 
        for (UINT i = 0; i < 4; ++i) {
            controlPoints[i].x = readReal(xdrFile, doublePrecision);
            controlPoints[i].y = readReal(xdrFile, doublePrecision);
            controlPoints[i].z = readReal(xdrFile, doublePrecision);
        }

        width = readReal(xdrFile, doublePrecision);

        xdrFile >> centerIndex;
        V3dObject::centerIndex = centerIndex;
        xdrFile >> materialIndex;
        xdrFile >> core;
    }

void V3dTube::QueueMesh(int imageWidth, int imageHeight, triple sceneMinBound, triple sceneMaxBound, bool remesh, bool orthographic, DrawMode drawMode) {
    camp::materialIndex = materialIndex;

    std::array<triple, 4> v = {
        triple{ controlPoints[0].x, controlPoints[0].y, controlPoints[0].z },
        triple{ controlPoints[1].x, controlPoints[1].y, controlPoints[1].z },
        triple{ controlPoints[2].x, controlPoints[2].y, controlPoints[2].z },
        triple{ controlPoints[3].x, controlPoints[3].y, controlPoints[3].z },
    };

    tube(v, width, centerIndex, materialIndex, core, imageWidth, imageHeight, sceneMinBound, sceneMaxBound, remesh, orthographic, drawMode);
}


V3dBezierCurve::V3dBezierCurve(xdr::ixstream& xdrFile, V3D_BOOL doublePrecision)
    : V3dObject{ ObjectTypes::CURVE } { 
    for (UINT i = 0; i < 4; ++i) {
        controlPoints[i].x = readReal(xdrFile, doublePrecision);
        controlPoints[i].y = readReal(xdrFile, doublePrecision);
        controlPoints[i].z = readReal(xdrFile, doublePrecision);
    }    

    xdrFile >> centerIndex;
        V3dObject::centerIndex = centerIndex;
    xdrFile >> materialIndex;

        // Compute tight bounds at construction time (matches drawsurface.cc).
        triple Controls[4];
        for (int i = 0; i < 4; ++i)
            Controls[i] = triple(controlPoints[i].x, controlPoints[i].y, controlPoints[i].z);
        computeBezierCurveBounds(Controls, V3dObject::Min, V3dObject::Max);
}

V3dBezierCurve::V3dBezierCurve(std::array<TRIPLE, 4> controlPoints, UINT centerIndex, UINT materialIndex) 
    : V3dObject{ ObjectTypes::CURVE } 
    , controlPoints{ controlPoints }
    , centerIndex{ centerIndex }
    , materialIndex{ materialIndex } {
        // Compute tight bounds at construction time (matches drawsurface.cc).
        triple Controls[4];
        for (int i = 0; i < 4; ++i)
            Controls[i] = triple(controlPoints[i].x, controlPoints[i].y, controlPoints[i].z);
        computeBezierCurveBounds(Controls, V3dObject::Min, V3dObject::Max);
    }

void V3dBezierCurve::QueueMesh(int imageWidth, int imageHeight, triple sceneMinBound, triple sceneMaxBound, bool remesh, bool orthographic, DrawMode drawMode) {
    camp::materialIndex = materialIndex;

    triple Controls[] = {
        triple(controlPoints[0].x, controlPoints[0].y, controlPoints[0].z),
        triple(controlPoints[1].x, controlPoints[1].y, controlPoints[1].z),
        triple(controlPoints[2].x, controlPoints[2].y, controlPoints[2].z),
        triple(controlPoints[3].x, controlPoints[3].y, controlPoints[3].z)
    };

    BezierCurve S;

    triple b=sceneMinBound;
    triple B=sceneMaxBound;

    double Zmax=B.getz();

    double perspective=orthographic ? 0.0 : 1.0/Zmax;
    double s=perspective ? b.getz()*perspective : 1.0; // Move to glrender
    double size2=hypot(imageWidth,imageHeight);

    // Match Asymptote drawBezierPatch::render(): check material alpha in NORMAL mode only.
    // WIREFRAME/OUTLINE force opaque (commit 316f906894).
    bool transparent = false;
    if (drawMode == DRAWMODE_NORMAL && camp::materials && materialIndex < (int)camp::materials->size()) {
        transparent = (*camp::materials)[materialIndex].diffuse.a < 1.0f;
    }
    bool straight=false;
    bool color=false;

    const camp::pair size3(s*(B.getx()-b.getx()),s*(B.gety()-b.gety()));

    // Use tight bounds computed at construction time (matches Asymptote drawPath3::Min/Max).
    bool offscreen = bbox2(Min, Max).offscreen();

    if(offscreen) { // Fully offscreen
        fullyOnscreen = false;
        vertexData.clear();
        // Match Asymptote: C.notRendered() ensures upload gate fires when object comes back onscreen.
        transparentData.renderCount = 0;
        materialData.renderCount = 0;
        return;
    }
    
    if(!remesh && fullyOnscreen && centerIndex == 0) { // Fully onscreen; no need to re-render
        materialData.extendMaterial(vertexData);
        return;
    }

    S.queue(Controls,straight,size3.length()/size2);
    fullyOnscreen = S.Onscreen;
    vertexData = S.data;
}


V3dLineSegment::V3dLineSegment(
    xdr::ixstream& xdrFile, 
    V3D_BOOL doublePrecision)
    : V3dObject{ ObjectTypes::LINE } { 
        for (UINT i = 0; i < 2; ++i) {
            endpoints[i].x = readReal(xdrFile, doublePrecision);
            endpoints[i].y = readReal(xdrFile, doublePrecision);
            endpoints[i].z = readReal(xdrFile, doublePrecision);
        }    

        xdrFile >> centerIndex;
        V3dObject::centerIndex = centerIndex;
        xdrFile >> materialIndex;    

        // Straight geometry: bounds = min/max of endpoints.
        V3dObject::Min = triple(endpoints[0].x, endpoints[0].y, endpoints[0].z);
        V3dObject::Max = triple(endpoints[0].x, endpoints[0].y, endpoints[0].z);
        for (int i = 1; i < 2; ++i) {
            V3dObject::Min = triple(min(V3dObject::Min.getx(), endpoints[i].x),
                                    min(V3dObject::Min.gety(), endpoints[i].y),
                                    min(V3dObject::Min.getz(), endpoints[i].z));
            V3dObject::Max = triple(max(V3dObject::Max.getx(), endpoints[i].x),
                                    max(V3dObject::Max.gety(), endpoints[i].y),
                                    max(V3dObject::Max.getz(), endpoints[i].z));
        }
    }

V3dLineSegment::V3dLineSegment(std::array<TRIPLE, 2> endpoints, UINT centerIndex, UINT materialIndex) 
    : V3dObject{ ObjectTypes::LINE } 
    , endpoints{ endpoints }
    , centerIndex{ centerIndex }
    , materialIndex{ materialIndex } {
        // Straight geometry: bounds = min/max of endpoints.
        V3dObject::Min = triple(endpoints[0].x, endpoints[0].y, endpoints[0].z);
        V3dObject::Max = triple(endpoints[0].x, endpoints[0].y, endpoints[0].z);
        for (int i = 1; i < 2; ++i) {
            V3dObject::Min = triple(min(V3dObject::Min.getx(), endpoints[i].x),
                                    min(V3dObject::Min.gety(), endpoints[i].y),
                                    min(V3dObject::Min.getz(), endpoints[i].z));
            V3dObject::Max = triple(max(V3dObject::Max.getx(), endpoints[i].x),
                                    max(V3dObject::Max.gety(), endpoints[i].y),
                                    max(V3dObject::Max.getz(), endpoints[i].z));
        }
    }

void V3dLineSegment::QueueMesh(int imageWidth, int imageHeight, triple sceneMinBound, triple sceneMaxBound, bool remesh, bool orthographic, DrawMode drawMode) {
    camp::materialIndex = materialIndex;

    // For a straight line segment, BezierCurve::render(straight=true) uses only
    // p[0] and p[3] as the two endpoints.  p[1] and p[2] are ignored.
    triple Controls[] = {
        triple(endpoints[0].x, endpoints[0].y, endpoints[0].z),
        triple{0.0, 0.0, 0.0}, // dummy (ignored when straight=true)
        triple{0.0, 0.0, 0.0}, // dummy (ignored when straight=true)
        triple(endpoints[1].x, endpoints[1].y, endpoints[1].z)
    };

    BezierCurve S;

    triple b = sceneMinBound;
    triple B = sceneMaxBound;

    double Zmax = B.getz();
    double perspective = orthographic ? 0.0 : 1.0 / Zmax;
    double s = perspective ? b.getz() * perspective : 1.0;
    double size2 = hypot(imageWidth, imageHeight);

    bool straight = true;

    const camp::pair size3(s * (B.getx() - b.getx()), s * (B.gety() - b.gety()));

    // Use tight bounds computed at construction time (matches Asymptote drawBezierTriangle::Min/Max).
    bool offscreen = bbox2(Min, Max).offscreen();

    if (offscreen) {
        fullyOnscreen = false;
        vertexData.clear();
        // Match Asymptote: C.notRendered() ensures upload gate fires when object comes back onscreen.
        transparentData.renderCount = 0;
        materialData.renderCount = 0;
        return;
    }

    S.queue(Controls, straight, size3.length() / size2);
    fullyOnscreen = S.Onscreen;
    vertexData = S.data;
}


V3dPixel::V3dPixel(
    xdr::ixstream& xdrFile, 
    V3D_BOOL doublePrecision)
    : V3dObject{ ObjectTypes::PIXEL } { 
        position.x = readReal(xdrFile, doublePrecision);
        position.y = readReal(xdrFile, doublePrecision);
        position.z = readReal(xdrFile, doublePrecision);

        width = readReal(xdrFile, doublePrecision);

        xdrFile >> materialIndex;
    }

void V3dPixel::QueueMesh(int imageWidth, int imageHeight, triple sceneMinBound, triple sceneMaxBound, bool remesh, bool orthographic, DrawMode drawMode) {
    camp::materialIndex = materialIndex;

    // Match Asymptote drawPixel::render(): offscreen check using bbox2(Min,Max).
    triple Min{position.x, position.y, position.z};
    triple Max{position.x, position.y, position.z};

    if (bbox2(Min, Max).offscreen()) { // Fully offscreen
        fullyOnscreen = false;
        vertexData.clear();
        // Match Asymptote: notRendered() ensures upload gate fires when object comes back onscreen.
        pointData.renderCount = 0;
        return;
    }

    // Match Asymptote Pixel::queue() exactly.
    VertexBuffer data;
    data.clear();
    pointData.renderCount = 0;
    data.indices.push_back(data.addVertex(PointVertex{
        glm::vec3(position.x, position.y, position.z),
        static_cast<float>(width),
        static_cast<glm::i32>(materialIndex)
    }));

    pointData.extendPoint(data);
    fullyOnscreen = true;
}
