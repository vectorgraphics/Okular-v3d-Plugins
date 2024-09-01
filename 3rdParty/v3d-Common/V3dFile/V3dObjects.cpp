#include "V3dObjects.h"

#include <iostream>

#include "V3dUtil.h"

V3dBezierPatch::V3dBezierPatch(xdr::ixstream& xdrFile, BOOL doublePrecision)
    : V3dObject{ ObjectTypes::BEZIER_PATCH } { 
        for (int i = 0; i < 16; ++i) {
            controlPoints[i].x = readReal(xdrFile, doublePrecision);
            controlPoints[i].y = readReal(xdrFile, doublePrecision);
            controlPoints[i].z = readReal(xdrFile, doublePrecision);
        }

        xdrFile >> centerIndex;
        xdrFile >> materialIndex;
    }

std::vector<float> V3dBezierPatch::getVertexData() {
    std::cout << "ERROR: V3dBezierPatch cannot currently give vertices" << std::endl;
    return std::vector<float>{};
}

std::vector<unsigned int> V3dBezierPatch::getIndices() {
    std::cout << "ERROR: V3dBezierPatch cannot currently give indices" << std::endl;
    return std::vector<unsigned int>{};
}


V3dBezierTriangle::V3dBezierTriangle(xdr::ixstream& xdrFile, BOOL doublePrecision)
    : V3dObject{ ObjectTypes::BEZIER_TRIANGLE } { 
        for (int i = 0; i < 10; ++i) {
            controlPoints[i].x = readReal(xdrFile, doublePrecision);
            controlPoints[i].y = readReal(xdrFile, doublePrecision);
            controlPoints[i].z = readReal(xdrFile, doublePrecision);
        }

        std::cout << "Reading bezierTriangle" << std::endl;

        xdrFile >> centerIndex;
        xdrFile >> materialIndex;
    }

std::vector<float> V3dBezierTriangle::getVertexData() {
    std::cout << "ERROR: V3dBezierTriangle cannot currently give vertices" << std::endl;
    return std::vector<float>{};
}

std::vector<unsigned int> V3dBezierTriangle::getIndices() {
    std::cout << "ERROR: V3dBezierTriangle cannot currently give indices" << std::endl;
    return std::vector<unsigned int>{};
}


V3dBezierPatchWithCornerColors::V3dBezierPatchWithCornerColors(xdr::ixstream& xdrFile, BOOL doublePrecision)
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

std::vector<float> V3dBezierPatchWithCornerColors::getVertexData() {
    std::cout << "ERROR: V3dBezierPatchWithCornerColors cannot currently give vertices" << std::endl;
    return std::vector<float>{};
}

std::vector<unsigned int> V3dBezierPatchWithCornerColors::getIndices() {
    std::cout << "ERROR: V3dBezierPatchWithCornerColors cannot currently give indices" << std::endl;
    return std::vector<unsigned int>{};
}


V3dBezierTriangleWithCornerColors::V3dBezierTriangleWithCornerColors(xdr::ixstream& xdrFile, BOOL doublePrecision)
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

std::vector<float> V3dBezierTriangleWithCornerColors::getVertexData() {
    std::cout << "ERROR: V3dBezierTriangleWithCornerColors cannot currently give vertices" << std::endl;
    return std::vector<float>{};
}

std::vector<unsigned int> V3dBezierTriangleWithCornerColors::getIndices() {
    std::cout << "ERROR: V3dBezierTriangleWithCornerColors cannot currently give indices" << std::endl;
    return std::vector<unsigned int>{};
}


V3dStraightPlanarQuad::V3dStraightPlanarQuad(xdr::ixstream& xdrFile, BOOL doublePrecision)
    : V3dObject{ ObjectTypes::QUAD } {
        for (int i = 0; i < 4; ++i) {
            vertices[i].x = readReal(xdrFile, doublePrecision);
            vertices[i].y = readReal(xdrFile, doublePrecision);
            vertices[i].z = readReal(xdrFile, doublePrecision);
        }

        xdrFile >> centerIndex;
        xdrFile >> materialIndex; 
    }

std::vector<float> V3dStraightPlanarQuad::getVertexData() {
    std::vector<float> out{};


    TRIPLE p1 = vertices[0];
    TRIPLE p2 = vertices[1];
    TRIPLE p3 = vertices[2];

    TRIPLE A = p2 - p1;
    TRIPLE B = p3 - p1;

    TRIPLE N = glm::cross(A, B);

    for (auto& ver : vertices) {
        out.push_back(ver.x);
        out.push_back(ver.y);
        out.push_back(ver.z);

        out.push_back(N.x);
        out.push_back(N.y);
        out.push_back(N.z);
    }

    return out;
}

std::vector<unsigned int> V3dStraightPlanarQuad::getIndices() {
    std::vector<unsigned int> out {
        0, 1, 2,
        0, 2, 3
    };

    return out;
}


V3dStraightTriangle::V3dStraightTriangle(xdr::ixstream& xdrFile, BOOL doublePrecision)
    : V3dObject{ ObjectTypes::TRIANGLE } { 
        for (int i = 0; i < 3; ++i) {
            vertices[i].x = readReal(xdrFile, doublePrecision);
            vertices[i].y = readReal(xdrFile, doublePrecision);
            vertices[i].z = readReal(xdrFile, doublePrecision);
        }

        xdrFile >> centerIndex;
        xdrFile >> materialIndex;
    }

std::vector<float> V3dStraightTriangle::getVertexData() {
    std::vector<float> out{};

    TRIPLE p1 = vertices[0];
    TRIPLE p2 = vertices[1];
    TRIPLE p3 = vertices[2];

    TRIPLE A = p2 - p1;
    TRIPLE B = p3 - p1;

    TRIPLE N = glm::cross(A, B);

    for (auto& ver : vertices) {
        out.push_back(ver.x);
        out.push_back(ver.y);
        out.push_back(ver.z);

        out.push_back(N.x);
        out.push_back(N.y);
        out.push_back(N.z);
    }

    return out;
}

std::vector<unsigned int> V3dStraightTriangle::getIndices() {
    std::vector<unsigned int> out {
        0, 1, 2
    };

    return out;
}


V3dStraightPlanarQuadWithCornderColors::V3dStraightPlanarQuadWithCornderColors(xdr::ixstream& xdrFile, BOOL doublePrecision)
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

std::vector<float> V3dStraightPlanarQuadWithCornderColors::getVertexData() {
    std::cout << "ERROR: V3dStraightPlanarQuadWithCornderColors cannot currently give vertices" << std::endl;
    return std::vector<float>{};
}

std::vector<unsigned int> V3dStraightPlanarQuadWithCornderColors::getIndices() {
    std::cout << "ERROR: V3dStraightPlanarQuadWithCornderColors cannot currently give indices" << std::endl;
    return std::vector<unsigned int>{};
}


V3dStraightTriangleWithCornerColors::V3dStraightTriangleWithCornerColors(xdr::ixstream& xdrFile, BOOL doublePrecision)
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

std::vector<float> V3dStraightTriangleWithCornerColors::getVertexData() {
    std::cout << "ERROR: V3dStraightTriangleWithCornerColors cannot currently give vertices" << std::endl;
    return std::vector<float>{};
}

std::vector<unsigned int> V3dStraightTriangleWithCornerColors::getIndices() {
    std::cout << "ERROR: V3dStraightTriangleWithCornerColors cannot currently give indices" << std::endl;
    return std::vector<unsigned int>{};
}


V3dTriangleGroup::V3dTriangleGroup(xdr::ixstream& xdrFile, BOOL doublePrecision)
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

std::vector<float> V3dTriangleGroup::getVertexData() {
    std::vector<float> out;

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

    for (int i = 0; i < vertices.size(); ++i) {
        out.push_back(vertices[i].x);
        out.push_back(vertices[i].y);
        out.push_back(vertices[i].z);

        out.push_back(vertexNormalArray[i].x);
        out.push_back(vertexNormalArray[i].y);
        out.push_back(vertexNormalArray[i].z);
    }

    return out;
}

std::vector<unsigned int> V3dTriangleGroup::getIndices() {
    std::vector<unsigned int> out;

    out.resize(nI * 3);

    for(size_t i = 0; i < nI; ++i) {
        std::array<unsigned int, 3> PI = positionIndices[i];

        uint32_t PI0 = PI[0];
        uint32_t PI1 = PI[1];
        uint32_t PI2 = PI[2];

        size_t i3=3*i;
        out[i3 + 0] = PI0;
        out[i3 + 1] = PI1;
        out[i3 + 2] = PI2;
    }

    return out;
}


V3dSphere::V3dSphere(xdr::ixstream& xdrFile, BOOL doublePrecision)
    : V3dObject{ ObjectTypes::SPHERE } { 
        center.x = readReal(xdrFile, doublePrecision);
        center.y = readReal(xdrFile, doublePrecision);
        center.z = readReal(xdrFile, doublePrecision);

        xdrFile >> radius;

        xdrFile >> centerIndex;
        xdrFile >> materialIndex;
    }

std::vector<float> V3dSphere::getVertexData() {
    std::cout << "ERROR: V3dSphere cannot currently give vertices" << std::endl;
    return std::vector<float>{};
}

std::vector<unsigned int> V3dSphere::getIndices() {
    std::cout << "ERROR: V3dSphere cannot currently give indices" << std::endl;
    return std::vector<unsigned int>{};
}


V3dHemiSphere::V3dHemiSphere(xdr::ixstream& xdrFile, BOOL doublePrecision)
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

std::vector<float> V3dHemiSphere::getVertexData() {
    std::cout << "ERROR: V3dHemiSphere cannot currently give vertices" << std::endl;
    return std::vector<float>{};
}

std::vector<unsigned int> V3dHemiSphere::getIndices() {
    std::cout << "ERROR: V3dHemiSphere cannot currently give indices" << std::endl;
    return std::vector<unsigned int>{};
}


V3dDisk::V3dDisk(xdr::ixstream& xdrFile, BOOL doublePrecision)
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

std::vector<float> V3dDisk::getVertexData() {
    std::cout << "ERROR: V3dDisk cannot currently give vertices" << std::endl;
    return std::vector<float>{};
}

std::vector<unsigned int> V3dDisk::getIndices() {
    std::cout << "ERROR: V3dDisk cannot currently give indices" << std::endl;
    return std::vector<unsigned int>{};
}


V3dCylinder::V3dCylinder(xdr::ixstream& xdrFile, BOOL doublePrecision)
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

std::vector<float> V3dCylinder::getVertexData() {
    std::cout << "ERROR: V3dCylinder cannot currently give vertices" << std::endl;
    return std::vector<float>{};
}

std::vector<unsigned int> V3dCylinder::getIndices() {
    std::cout << "ERROR: V3dCylinder cannot currently give indices" << std::endl;
    return std::vector<unsigned int>{};
}


V3dTube::V3dTube(xdr::ixstream& xdrFile, BOOL doublePrecision)
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

std::vector<float> V3dTube::getVertexData() {
    std::cout << "ERROR: V3dTube cannot currently give vertices" << std::endl;
    return std::vector<float>{};
}

std::vector<unsigned int> V3dTube::getIndices() {
    std::cout << "ERROR: V3dTube cannot currently give indices" << std::endl;
    return std::vector<unsigned int>{};
}


V3dBezierCurve::V3dBezierCurve(xdr::ixstream& xdrFile, BOOL doublePrecision)
    : V3dObject{ ObjectTypes::CURVE } { 
        for (UINT i = 0; i < 4; ++i) {
            controlPoints[i].x = readReal(xdrFile, doublePrecision);
            controlPoints[i].y = readReal(xdrFile, doublePrecision);
            controlPoints[i].z = readReal(xdrFile, doublePrecision);
        }    

        xdrFile >> centerIndex;
        xdrFile >> materialIndex;
    }

std::vector<float> V3dBezierCurve::getVertexData() {
    std::cout << "ERROR: V3dBezierCurve cannot currently give vertices" << std::endl;
    return std::vector<float>{};
}

std::vector<unsigned int> V3dBezierCurve::getIndices() {
    std::cout << "ERROR: V3dBezierCurve cannot currently give indices" << std::endl;
    return std::vector<unsigned int>{};
}


V3dLineSegment::V3dLineSegment(xdr::ixstream& xdrFile, BOOL doublePrecision)
    : V3dObject{ ObjectTypes::LINE } { 
        for (UINT i = 0; i < 2; ++i) {
            endpoints[i].x = readReal(xdrFile, doublePrecision);
            endpoints[i].y = readReal(xdrFile, doublePrecision);
            endpoints[i].z = readReal(xdrFile, doublePrecision);
        }    

        xdrFile >> centerIndex;
        xdrFile >> materialIndex;    
    }

std::vector<float> V3dLineSegment::getVertexData() {
    std::cout << "ERROR: V3dLineSegment cannot currently give vertices" << std::endl;
    return std::vector<float>{};
}

std::vector<unsigned int> V3dLineSegment::getIndices() {
    std::cout << "ERROR: V3dLineSegment cannot currently give indices" << std::endl;
    return std::vector<unsigned int>{};
}


V3dPixel::V3dPixel(xdr::ixstream& xdrFile, BOOL doublePrecision)
    : V3dObject{ ObjectTypes::PIXEL } { 
        position.x = readReal(xdrFile, doublePrecision);
        position.y = readReal(xdrFile, doublePrecision);
        position.z = readReal(xdrFile, doublePrecision);

        xdrFile >> centerIndex;
        xdrFile >> materialIndex;
    }

std::vector<float> V3dPixel::getVertexData() {
    std::cout << "ERROR: V3dPixel cannot currently give vertices" << std::endl;
    return std::vector<float>{};
}

std::vector<unsigned int> V3dPixel::getIndices() {
    std::cout << "ERROR: V3dPixel cannot currently give indices" << std::endl;
    return std::vector<unsigned int>{};
}
