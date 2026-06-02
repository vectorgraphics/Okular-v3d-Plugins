#include "V3dObjects.h"

#include <iostream>

#include "V3dUtil.h"

#include "rgba.h"
#include "bezierpatch.h"
#include "beziercurve.h"

using namespace std;
using namespace camp;

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
        xdrFile >> materialIndex;
    }

V3dBezierPatch::V3dBezierPatch(std::array<TRIPLE, 16> controlPoints, UINT centerIndex, UINT materialIndex) 
    : V3dObject{ ObjectTypes::BEZIER_PATCH }
    , controlPoints{ controlPoints }
    , centerIndex{ centerIndex }
    , materialIndex{ materialIndex } { }


void V3dBezierPatch::QueueMesh(int imageWidth, int imageHeight, triple sceneMinBound, triple sceneMaxBound, bool remesh, bool orthographic) {
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

    BezierPatch S;

    triple b=sceneMinBound;
    triple B=sceneMaxBound;

    double Zmax=B.getz();

    double perspective=orthographic ? 0.0 : 1.0/Zmax;
    double s=perspective ? b.getz()*perspective : 1.0; // Move to glrender
    double size2=hypot(imageWidth,imageHeight);

    bool transparent=false;
    bool straight=false;

    const camp::pair size3(s*(B.getx()-b.getx()),s*(B.gety()-b.gety()));

    triple Min=b;
    triple Max=B;
    
    bool offscreen=bbox2(Min,Max).offscreen();

    if(offscreen) { // Fully offscreen
        fullyOnscreen = false;
        vertexData.clear();
        return;
    }
    
    if(!remesh && fullyOnscreen) { // Fully onscreen; no need to re-render
        materialData.extendMaterial(vertexData);
        return;
    }

    S.queue(Controls,straight,size3.length()/size2,transparent,NULL);
    fullyOnscreen = true;
    vertexData = S.data;
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
        xdrFile >> materialIndex;
    }

V3dBezierTriangle::V3dBezierTriangle(std::array<TRIPLE, 10> controlPoints, UINT centerIndex, UINT materialIndex) 
    : V3dObject{ ObjectTypes::BEZIER_TRIANGLE }
    , controlPoints{ controlPoints }
    , centerIndex{ centerIndex }
    , materialIndex{ materialIndex } { }

void V3dBezierTriangle::QueueMesh(int imageWidth, int imageHeight, triple sceneMinBound, triple sceneMaxBound, bool remesh, bool orthographic) {
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

    BezierTriangle S;

    triple b=sceneMinBound;
    triple B=sceneMaxBound;

    double Zmax=B.getz();

    double perspective=orthographic ? 0.0 : 1.0/Zmax;
    double s=perspective ? b.getz()*perspective : 1.0; // Move to glrender
    double size2=hypot(imageWidth,imageHeight);

    bool transparent=false;
    bool straight=false;

    const camp::pair size3(s*(B.getx()-b.getx()),s*(B.gety()-b.gety()));

    triple Min=b;
    triple Max=B;
    
    bool offscreen=bbox2(Min,Max).offscreen();

    if(offscreen) { // Fully offscreen
        fullyOnscreen = false;
        vertexData.clear();
        return;
    }
    
    if(!remesh && fullyOnscreen) { // Fully onscreen; no need to re-render
        materialData.extendMaterial(vertexData);
        return;
    }

    S.queue(Controls,straight,size3.length()/size2,transparent,NULL);
    fullyOnscreen = true;
    vertexData = S.data;
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
        xdrFile >> materialIndex;    

        for (int i = 0; i < 4; ++i) {
            xdrFile >> cornerColors[i].r;
            xdrFile >> cornerColors[i].g;
            xdrFile >> cornerColors[i].b;
            xdrFile >> cornerColors[i].a;
        }
    }

void V3dBezierPatchWithCornerColors::QueueMesh(int imageWidth, int imageHeight, triple sceneMinBound, triple sceneMaxBound, bool remesh, bool orthographic) {
    camp::materialIndex = materialIndex;

    std::cout << "V3dBezierPatchWithCornerColors cannot queue" << std::endl;
    return;
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
        xdrFile >> materialIndex;    

        for (int i = 0; i < 3; ++i) {
            xdrFile >> cornerColors[i].r;
            xdrFile >> cornerColors[i].g;
            xdrFile >> cornerColors[i].b;
            xdrFile >> cornerColors[i].a;
        }    
    }

void V3dBezierTriangleWithCornerColors::QueueMesh(int imageWidth, int imageHeight, triple sceneMinBound, triple sceneMaxBound, bool remesh, bool orthographic) {
    camp::materialIndex = materialIndex;

    std::cout << "V3dBezierTriangleWithCornerColors cannot queue" << std::endl;
    return;
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
        xdrFile >> materialIndex; 
    }

// std::vector<float> V3dStraightPlanarQuad::getVertexData() {
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
// std::vector<unsigned int> V3dStraightPlanarQuad::getIndices() {
//     std::vector<unsigned int> out {
//         0, 1, 2,
//         0, 2, 3
//     };
//
//     return out;
// }

void V3dStraightPlanarQuad::QueueMesh(int imageWidth, int imageHeight, triple sceneMinBound, triple sceneMaxBound, bool remesh, bool orthographic) {
    camp::materialIndex = materialIndex;

    std::cout << "V3dStraightPlanarQuad cannot queue" << std::endl;
    return;
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

void V3dStraightTriangle::QueueMesh(int imageWidth, int imageHeight, triple sceneMinBound, triple sceneMaxBound, bool remesh, bool orthographic) {
    camp::materialIndex = materialIndex;

    std::cout << "V3dStraightTriangle cannot queue" << std::endl;
    return;
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
        xdrFile >> materialIndex;     

        for (int i = 0; i < 4; ++i) {
            xdrFile >> cornerColors[i].r;
            xdrFile >> cornerColors[i].g;
            xdrFile >> cornerColors[i].b;
            xdrFile >> cornerColors[i].a;
        }
    }

void V3dStraightPlanarQuadWithCornerColors::QueueMesh(int imageWidth, int imageHeight, triple sceneMinBound, triple sceneMaxBound, bool remesh, bool orthographic) {
    camp::materialIndex = materialIndex;

    std::cout << "V3dStraightPlanarQuadWithCornerColors cannot queue" << std::endl;
    return;
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
        xdrFile >> materialIndex;     

        for (int i = 0; i < 3; ++i) {
            xdrFile >> cornerColors[i].r;
            xdrFile >> cornerColors[i].g;
            xdrFile >> cornerColors[i].b;
            xdrFile >> cornerColors[i].a;
        }
    }

void V3dStraightTriangleWithCornerColors::QueueMesh(int imageWidth, int imageHeight, triple sceneMinBound, triple sceneMaxBound, bool remesh, bool orthographic) {
    camp::materialIndex = materialIndex;

    std::cout << "V3dStraightTriangleWithCornerColors cannot queue" << std::endl;
    return;
}


V3dTriangleGroup::V3dTriangleGroup(
    xdr::ixstream& xdrFile, 
    V3D_BOOL doublePrecision)
    : V3dObject{ ObjectTypes::TRIANGLES } { 
        nI = 0;
        xdrFile >> nI;

        nP = 0;
        xdrFile >> nP;
        vertexPositions.resize(nP);
        for (UINT i = 0; i < nP; ++i) {
            vertexPositions[i].x = readReal(xdrFile, doublePrecision);
            vertexPositions[i].y = readReal(xdrFile, doublePrecision);
            vertexPositions[i].z = readReal(xdrFile, doublePrecision);
        }

        nN = 0;
        xdrFile >> nN;
        vertexNormalArray.resize(nN);
        for (UINT i = 0; i < nN; ++i) {
            vertexNormalArray[i].x = readReal(xdrFile, doublePrecision);
            vertexNormalArray[i].y = readReal(xdrFile, doublePrecision);
            vertexNormalArray[i].z = readReal(xdrFile, doublePrecision);
        }

        xdrFile >> explicitNI;

        xdrFile >> nC;
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
        xdrFile >> materialIndex;
    }

void V3dTriangleGroup::QueueMesh(int imageWidth, int imageHeight, triple sceneMinBound, triple sceneMaxBound, bool remesh, bool orthographic) {
    camp::materialIndex = materialIndex;

    std::vector<MaterialVertex> matVertices;

    std::vector<TRIPLE> vertices;
    vertices.resize(nP);

    for(size_t i = 0; i < nI; ++i) {
        std::array<unsigned int, 3> PI = positionIndices[i];
        uint32_t PI0 = PI[0];
        uint32_t PI1 = PI[1];
        uint32_t PI2 = PI[2];
        TRIPLE P0 = vertexPositions[PI0];
        TRIPLE P1 = vertexPositions[PI1];
        TRIPLE P2 = vertexPositions[PI2];

        vertices[PI0] = P0;
        vertices[PI1] = P1;
        vertices[PI2] = P2;
    }

    for (size_t i = 0; i < vertices.size(); ++i) {
        MaterialVertex matVert;
        matVert.position = vertices[i];
        matVert.normal = vertexNormalArray[i];

        matVertices.push_back(matVert);
    }

    std::vector<unsigned int> outIndices;

    outIndices.resize(nI * 3);

    for(size_t i = 0; i < nI; ++i) {
        std::array<unsigned int, 3> PI = positionIndices[i];

        uint32_t PI0 = PI[0];
        uint32_t PI1 = PI[1];
        uint32_t PI2 = PI[2];

        size_t i3=3*i;
        outIndices[i3 + 0] = PI0;
        outIndices[i3 + 1] = PI1;
        outIndices[i3 + 2] = PI2;
    }

    VertexBuffer buffer;
    buffer.materialVertices = matVertices;
    buffer.indices = outIndices;
    materialData.extendMaterial(buffer);
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

V3dSphere::V3dSphere(
    xdr::ixstream& xdrFile, 
    V3D_BOOL doublePrecision)
    : V3dObject{ ObjectTypes::SPHERE } { 
        center.x = readReal(xdrFile, doublePrecision);
        center.y = readReal(xdrFile, doublePrecision);
        center.z = readReal(xdrFile, doublePrecision);

        xdrFile >> radius;

        xdrFile >> centerIndex;
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
    UINT centerIndex,
    UINT materialIndex
) {
    double a = 4.0/3.0*(std::sqrt(2.0)-1.0);
    double b = 0.524670512339254;
    double c = 0.595936986722291;
    double d = 0.954967051233925;
    double e = 0.0820155480083437;
    double f = 0.996685028842544;
    double g = 0.0549670512339254;
    double h = 0.998880711874577;
    double i = 0.0405017186586849;

    std::array<triple, 16> patchOctant = {
        triple(1, 0, 0),
        triple(1, 0, b),
        triple(c, 0, d),
        triple(e, 0, f),

        triple(1, a, 0),
        triple(1, a, b),
        triple(c, a * c, d),
        triple(e, a * e, f),

        triple(a, 1, 0),
        triple(a, 1, b),
        triple(a * c, c, d),
        triple(a * e, e, f),

        triple(0, 1, 0),
        triple(0, 1, b),
        triple(0, c, d),
        triple(0, e, f)
    };

    std::array<triple, 10> triangleOctant {
        triple(e, 0, f),
        triple(e, a * e, f),
        triple(g, 0, h),
        triple(a * e, e, f),

        triple(i, i, 1),
        triple(0.05 * a, 0, 1),
        triple(0, e, f),
        triple(0, g, h),

        triple(0, 0.05 * a, 1),
        triple(0, 0, 1)
    };

    double rx, ry, rz;

    Align A(center, dir);

    int s;
    double z;
    std::function<triple(const triple&)> t;

    if(dir) {
        s = 1;
        z = 0;
        t = [&](const TRIPLE& v) { return A.T(v); };
    } else {
        s = -1;
        z = -r;
        t = [&](const TRIPLE& v) { return A.T0(v); };
    }

    auto TPatch = [&](const std::array<triple, 16>& V) {
        std::array<TRIPLE, 16> p{ };

        for(size_t i = 0; i < V.size(); ++i) {
            const TRIPLE& v = V[i];
            p[i] = t(TRIPLE(rx * v.x, ry * v.y, rz * v.z));
        }

        return p;
    };

    auto TTriangle = [&](const std::array<triple, 10>& V) {
        std::array<TRIPLE, 10> p{ };

        for(size_t i = 0; i < V.size(); ++i) {
            const TRIPLE& v = V[i];
            p[i] = t(TRIPLE(rx * v.x, ry * v.y, rz * v.z));
        }

        return p;
    };

    for(int ix = -1; ix <= 1; ix += 2) {
        rx = ix * r;
        for(int iy = -1; iy <= 1; iy += 2) {
            ry = iy * r;
            for(int iz = s; iz <= 1; iz += 2) {
                rz = iz * r;
                V3dBezierPatch patch{ TPatch(patchOctant), centerIndex, materialIndex };
                patch.QueueMesh(imageWidth, imageHeight, sceneMinBound, sceneMaxBound, remesh, orthographic);

                V3dBezierTriangle triangle{ TTriangle(triangleOctant), centerIndex, materialIndex };
                triangle.QueueMesh(imageWidth, imageHeight, sceneMinBound, sceneMaxBound, remesh, orthographic);
            }
        }
    }
}

void V3dSphere::QueueMesh(int imageWidth, int imageHeight, triple sceneMinBound, triple sceneMaxBound, bool remesh, bool orthographic) {
    camp::materialIndex = materialIndex;

    triple* dir = nullptr;
    double r = radius;
    
    sphere(center, r, dir, imageWidth, imageHeight, sceneMinBound, sceneMaxBound, remesh, orthographic, centerIndex, materialIndex);
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
        xdrFile >> materialIndex;    

        polarAngle = readReal(xdrFile, doublePrecision);
        azimuthalAngle = readReal(xdrFile, doublePrecision);

    }

void V3dHemiSphere::QueueMesh(int imageWidth, int imageHeight, triple sceneMinBound, triple sceneMaxBound, bool remesh, bool orthographic) {
    camp::materialIndex = materialIndex;

    triple direction{ polarAngle, azimuthalAngle, 0.0 };
    double r = radius;
    
    sphere(center, r, &direction, imageWidth, imageHeight, sceneMinBound, sceneMaxBound, remesh, orthographic, centerIndex, materialIndex);
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
    bool orthographic
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
        std::array<glm::vec3, 16> p{ };
        for(int i=0; i < V.size(); ++i) {
            const glm::vec3& v=V[i];
            p[i]=A.T(glm::vec3{r*v.x, r*v.y, 0.0});
        }
        return p;
    };

    V3dBezierPatch patch{ TPatch(unitdisk), centerIndex, materialIndex };
    patch.QueueMesh(imageWidth, imageHeight, sceneMinBound, sceneMaxBound, remesh, orthographic);
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
        xdrFile >> materialIndex;    

        polarAngle = readReal(xdrFile, doublePrecision);
        azimuthalAngle = readReal(xdrFile, doublePrecision);
    }

void V3dDisk::QueueMesh(int imageWidth, int imageHeight, triple sceneMinBound, triple sceneMaxBound, bool remesh, bool orthographic) {
    camp::materialIndex = materialIndex;

    triple direction{ polarAngle, azimuthalAngle, 0.0 };

    disk(center, radius, centerIndex, materialIndex, &direction, false, imageWidth, imageHeight, sceneMinBound, sceneMaxBound, remesh, orthographic);
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
    bool orthographic
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
                std::array<glm::vec3, 16> p{ };

                for(size_t i = 0; i < V.size(); ++i) {
                    const glm::vec3& v = V[i];
                    p[i] = t(glm::vec3(rx * v.x, ry * v.y, h * v.z));
                }

                return p;
            };

            V3dBezierPatch patch{ TPatch(unitcylinder), centerIndex, materialIndex };
            patch.QueueMesh(imageWidth, imageHeight, sceneMinBound, sceneMaxBound, remesh, orthographic);
        }
    }

    if(core) {
        triple Center=A.T(triple{0,0,h});
        std::array<glm::vec3, 4> curveControlPoints = {
            glm::vec3{ center.getx(), center.gety(), center.getz() },
            glm::vec3{ Center.getx(), Center.gety(), Center.getz() }
        };

        V3dBezierCurve curve{ curveControlPoints, centerIndex, materialIndex };
        curve.QueueMesh(imageWidth, imageHeight, sceneMinBound, sceneMaxBound, remesh, orthographic);
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
        xdrFile >> materialIndex;    

        polarAngle = readReal(xdrFile, doublePrecision);
        azimuthalAngle = readReal(xdrFile, doublePrecision);
    }

void V3dCylinder::QueueMesh(int imageWidth, int imageHeight, triple sceneMinBound, triple sceneMaxBound, bool remesh, bool orthographic) {
    camp::materialIndex = materialIndex;

    double r = radius;
    triple direction{ polarAngle, azimuthalAngle, 0.0 };

    cylinder(center, radius, height, centerIndex, materialIndex, &direction, false, imageWidth, imageHeight, sceneMinBound, sceneMaxBound, remesh, orthographic);
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
    bool orthographic
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
        patch.QueueMesh(imageWidth, imageHeight, sceneMinBound, sceneMaxBound, remesh, orthographic);

    };

    f(1.0,0.0,0.0,1.0);
    f(0.0,-1.0,1.0,0.0);
    f(-1.0,0.0,0.0,-1.0);
    f(0.0,1.0,-1.0,0.0);

    if(core) {
        std::array<glm::vec3, 4> curveControlPoints = {
            glm::vec3{ v[0].getx(), v[0].gety(), v[0].getz() },
            glm::vec3{ v[1].getx(), v[1].gety(), v[1].getz() },
            glm::vec3{ v[2].getx(), v[2].gety(), v[2].getz() },
            glm::vec3{ v[3].getx(), v[3].gety(), v[3].getz() }
        };

        V3dBezierCurve curve{ curveControlPoints, centerIndex, materialIndex };
        curve.QueueMesh(imageWidth, imageHeight, sceneMinBound, sceneMaxBound, remesh, orthographic);
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
        xdrFile >> materialIndex;
        xdrFile >> core;
    }

void V3dTube::QueueMesh(int imageWidth, int imageHeight, triple sceneMinBound, triple sceneMaxBound, bool remesh, bool orthographic) {
    camp::materialIndex = materialIndex;

    std::array<triple, 4> v = {
        triple{ controlPoints[0].x, controlPoints[0].y, controlPoints[0].z },
        triple{ controlPoints[1].x, controlPoints[1].y, controlPoints[1].z },
        triple{ controlPoints[2].x, controlPoints[2].y, controlPoints[2].z },
        triple{ controlPoints[3].x, controlPoints[3].y, controlPoints[3].z },
    };

    tube(v, width, centerIndex, materialIndex, core, imageWidth, imageHeight, sceneMinBound, sceneMaxBound, remesh, orthographic);
}


V3dBezierCurve::V3dBezierCurve(xdr::ixstream& xdrFile, V3D_BOOL doublePrecision)
    : V3dObject{ ObjectTypes::CURVE } { 
    for (UINT i = 0; i < 4; ++i) {
        controlPoints[i].x = readReal(xdrFile, doublePrecision);
        controlPoints[i].y = readReal(xdrFile, doublePrecision);
        controlPoints[i].z = readReal(xdrFile, doublePrecision);
    }    

    xdrFile >> centerIndex;
    xdrFile >> materialIndex;
}

V3dBezierCurve::V3dBezierCurve(std::array<TRIPLE, 4> controlPoints, UINT centerIndex, UINT materialIndex) 
    : V3dObject{ ObjectTypes::CURVE } 
    , controlPoints{ controlPoints }
    , centerIndex{ centerIndex }
    , materialIndex{ materialIndex } {

}

void V3dBezierCurve::QueueMesh(int imageWidth, int imageHeight, triple sceneMinBound, triple sceneMaxBound, bool remesh, bool orthographic) {
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

    bool transparent=false;
    bool straight=false;

    const camp::pair size3(s*(B.getx()-b.getx()),s*(B.gety()-b.gety()));

    triple Min=b;
    triple Max=B;
    
    bool offscreen=bbox2(Min,Max).offscreen();

    if(offscreen) { // Fully offscreen
        fullyOnscreen = false;
        vertexData.clear();
        return;
    }
    
    if(!remesh && fullyOnscreen) { // Fully onscreen; no need to re-render
        materialData.extendMaterial(vertexData);
        return;
    }

    S.queue(Controls,straight,size3.length()/size2);
    fullyOnscreen = true;
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
        xdrFile >> materialIndex;    
    }

void V3dLineSegment::QueueMesh(int imageWidth, int imageHeight, triple sceneMinBound, triple sceneMaxBound, bool remesh, bool orthographic) {
    camp::materialIndex = materialIndex;

    std::cout << "V3dLineSegment cannot queue" << std::endl;
    return;
}


V3dPixel::V3dPixel(
    xdr::ixstream& xdrFile, 
    V3D_BOOL doublePrecision)
    : V3dObject{ ObjectTypes::PIXEL } { 
        position.x = readReal(xdrFile, doublePrecision);
        position.y = readReal(xdrFile, doublePrecision);
        position.z = readReal(xdrFile, doublePrecision);

        xdrFile >> centerIndex;
        xdrFile >> materialIndex;
    }

void V3dPixel::QueueMesh(int imageWidth, int imageHeight, triple sceneMinBound, triple sceneMaxBound, bool remesh, bool orthographic) {
    camp::materialIndex = materialIndex;

    std::cout << "V3dPixel cannot queue" << std::endl;
    return;
}
