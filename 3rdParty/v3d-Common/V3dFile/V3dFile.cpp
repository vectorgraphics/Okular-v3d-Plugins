#include <iostream>
#include <fstream>
#include <queue>
#include <memory>

#include "V3dFile.h"

#include "xstream.h"

#include "V3dUtil.h"

#include "rgba.h"
#include "bezierpatch.h"

// Billboard transform: rotate vertex positions around a center point using the
// normal matrix (inverse-transpose of view rotation) so that billboard objects
// (labels, annotations) always face the camera.  Mirrors camp::billboardTransform
// from asymptote/render.h.
static inline glm::vec3 billboardTransform(const glm::vec3& center, const glm::vec3& v)
{
    double cx = center.x;
    double cy = center.y;
    double cz = center.z;

    double x = v.x - cx;
    double y = v.y - cy;
    double z = v.z - cz;

    const glm::dmat3& normMat = camp::getNormMat();
    const double* BBT = glm::value_ptr(normMat);

    return glm::vec3(
        float(x * BBT[0] + y * BBT[3] + z * BBT[6] + cx),
        float(x * BBT[1] + y * BBT[4] + z * BBT[7] + cy),
        float(x * BBT[2] + y * BBT[5] + z * BBT[8] + cz)
    );
}

#define printObjectTypes

#ifdef printObjectTypes
    #define PRINT_OBJECT_TYPE(t) std::cout << #t << std::endl
#else
    #define PRINT_OBJECT_TYPE(t)
#endif

// append array b onto array a with offset
void appendOffset(std::vector<UINT>& a, const std::vector<UINT>& b, size_t offset) {
    size_t n=a.size();
    size_t m=b.size();
    a.resize(n+m);
    for(size_t i=0; i < m; ++i)
        a[n+i]=b[i]+(UINT)offset;
}

V3dFile::V3dFile(const std::string& fileName) { 
    xdr::ixstream xdrFile{ fileName.c_str() };
    load(xdrFile);
}

V3dFile::V3dFile(xdr::memixstream& xdrFile) {
    load(xdrFile);
}

void V3dFile::load(xdr::ixstream& xdrFile) {
    xdrFile >> versionNumber;
    xdrFile >> doublePrecisionFlag;

    UINT objectType;
    while (xdrFile >> objectType) {
        switch (objectType) {
        default:
            std::cout << "UNKNOWN TYPE: " << objectType << std::endl;
            break;

        case ObjectTypes::MATERIAL:
            PRINT_OBJECT_TYPE(MATERIAL);
            V3dMaterial material;
            xdrFile >> material.diffuse.r;
            xdrFile >> material.diffuse.g;
            xdrFile >> material.diffuse.b;
            xdrFile >> material.diffuse.a;

            xdrFile >> material.emissive.r;
            xdrFile >> material.emissive.g;
            xdrFile >> material.emissive.b;
            xdrFile >> material.emissive.a;

            xdrFile >> material.specular.r;
            xdrFile >> material.specular.g;
            xdrFile >> material.specular.b;
            xdrFile >> material.specular.a;

            xdrFile >> material.shininess;
            xdrFile >> material.metallic;
            xdrFile >> material.fresnel0;
            xdrFile >> material.lightOn;

            materials.push_back(material);
            break;

        case ObjectTypes::TRANSFORM:
            PRINT_OBJECT_TYPE(TRANSFORM);
            std::cout << "ERROR: No current way to store v3d object: TRANSFORM" << std::endl;
            break;

        case ObjectTypes::ELEMENT:
            PRINT_OBJECT_TYPE(ELEMENT);
            std::cout << "ERROR: No current way to store v3d object: ELEMENT" << std::endl;
            break;

        case ObjectTypes::CENTERS:
            PRINT_OBJECT_TYPE(CENTERS);
            UINT centersLength;
            xdrFile >> centersLength;

            // Sanity cap to prevent bad_alloc from a crafted file.
            if (centersLength > 1000000) {
                std::cout << "WARNING: centers count (" << centersLength << ") exceeds limit, skipping." << std::endl;
                break;
            }

            if (centersLength > 0) {
                centers.resize(centersLength);

                for (UINT i = 0; i < centersLength; ++i) {
                    centers[i].x = readReal(xdrFile, doublePrecisionFlag);
                    centers[i].y = readReal(xdrFile, doublePrecisionFlag);
                    centers[i].z = readReal(xdrFile, doublePrecisionFlag);
                }
            }

            break;

        case ObjectTypes::HEADER:
            PRINT_OBJECT_TYPE(HEADER);
            UINT headerEntryCount;
            xdrFile >> headerEntryCount;

            for (UINT i = 0; i < headerEntryCount; ++i) {
                UINT headerKey;
                xdrFile >> headerKey;

                UINT length;
                xdrFile >> length;

                switch (headerKey) {
                    case CANVAS_WIDTH:
                        xdrFile >> headerInfo.canvasWidth;   
                        break;     

                    case CANVAS_HEIGHT:
                        xdrFile >> headerInfo.canvasHeight; 
                        break;    

                    case V3D_ABSOLUTE:
                        xdrFile >> headerInfo.absolute;   
                        break;     

                    case MIN_BOUND:
                        headerInfo.minBound.x = readReal(xdrFile, doublePrecisionFlag);
                        headerInfo.minBound.y = readReal(xdrFile, doublePrecisionFlag);
                        headerInfo.minBound.z = readReal(xdrFile, doublePrecisionFlag);
                        break; 

                    case MAX_BOUND:
                        headerInfo.maxBound.x = readReal(xdrFile, doublePrecisionFlag);
                        headerInfo.maxBound.y = readReal(xdrFile, doublePrecisionFlag);
                        headerInfo.maxBound.z = readReal(xdrFile, doublePrecisionFlag);
                        break;       

                    case ORTHOGRAPHIC:
                        xdrFile >> headerInfo.orthographic;   
                        break;     

                    case ANGLE_OF_VIEW:
                        headerInfo.angleOfView = readReal(xdrFile, doublePrecisionFlag);
                        break;     

                    case INITIAL_ZOOM:
                        headerInfo.initialZoom = readReal(xdrFile, doublePrecisionFlag);
                        break;     

                    case VIEWPORT_SHIFT:
                        headerInfo.viewportShift.x = readReal(xdrFile, doublePrecisionFlag);
                        headerInfo.viewportShift.y = readReal(xdrFile, doublePrecisionFlag);
                        break;    

                    case VIEWPORT_MARGIN:
                        headerInfo.viewportMargin.x = readReal(xdrFile, doublePrecisionFlag);
                        headerInfo.viewportMargin.y = readReal(xdrFile, doublePrecisionFlag); 
                        break;

                    case LIGHT:
                        headerInfo.light.direction.x = readReal(xdrFile, doublePrecisionFlag);
                        headerInfo.light.direction.y = readReal(xdrFile, doublePrecisionFlag); 
                        headerInfo.light.direction.z = readReal(xdrFile, doublePrecisionFlag);

                        xdrFile >> headerInfo.light.color.r;
                        xdrFile >> headerInfo.light.color.g;       
                        xdrFile >> headerInfo.light.color.b;              
                        break;       

                    case BACKGROUND:
                        xdrFile >> headerInfo.background.r;
                        xdrFile >> headerInfo.background.g;
                        xdrFile >> headerInfo.background.b;
                        xdrFile >> headerInfo.background.a; 
                        break;      

                    case ZOOM_FACTOR:
                        headerInfo.zoomFactor = readReal(xdrFile, doublePrecisionFlag);   
                        break;    

                    case ZOOM_PINCH_FACTOR:
                        headerInfo.zoomPinchFactor = readReal(xdrFile, doublePrecisionFlag);   
                        break;

                    case ZOOM_PINCH_CAP:
                        headerInfo.zoomPinchCap = readReal(xdrFile, doublePrecisionFlag);   
                        break;

                    case ZOOM_STEP:
                        headerInfo.zoomStep = readReal(xdrFile, doublePrecisionFlag);            
                        break;

                    case SHIFT_HOLD_DISTANCE:
                        headerInfo.shiftHoldDistance = readReal(xdrFile, doublePrecisionFlag);   
                        break;

                    case SHIFT_WAIT_TIME:
                        headerInfo.shiftWaitTime = readReal(xdrFile, doublePrecisionFlag);   
                        break;
                        
                    case VIBRATE_TIME:
                        headerInfo.vibrateTime = readReal(xdrFile, doublePrecisionFlag);    
                        break;    

                    default:
                        // Skip unknown header entries by consuming 'length' XDR words.
                        // This prevents stream corruption when newer v3d files contain
                        // header keys that this version doesn't recognize.
                        for (UINT j = 0; j < length; ++j) {
                            uint32_t skipWord;
                            xdrFile >> skipWord;
                        }
                        break;
                }
            }
            break;

        case ObjectTypes::LINE:
            PRINT_OBJECT_TYPE(LINE);
            objects.push_back(std::move(std::make_unique<V3dLineSegment>(xdrFile, doublePrecisionFlag)));
            break;

        case ObjectTypes::TRIANGLE:
            PRINT_OBJECT_TYPE(TRIANGLE);
            objects.push_back(std::move(std::make_unique<V3dStraightTriangle>(xdrFile, doublePrecisionFlag)));
            break;

        case ObjectTypes::QUAD:
            PRINT_OBJECT_TYPE(QUAD);
            objects.push_back(std::move(std::make_unique<V3dStraightPlanarQuad>(xdrFile, doublePrecisionFlag)));
            break;

        case ObjectTypes::CURVE:
            PRINT_OBJECT_TYPE(CURVE);
            objects.push_back(std::move(std::make_unique<V3dBezierCurve>(xdrFile, doublePrecisionFlag)));
            break;

        case ObjectTypes::BEZIER_TRIANGLE:
            PRINT_OBJECT_TYPE(BEZIER_TRIANGLE);
            objects.push_back(std::move(std::make_unique<V3dBezierTriangle>(xdrFile, doublePrecisionFlag)));
            break;

        case ObjectTypes::BEZIER_PATCH:
            PRINT_OBJECT_TYPE(BEZIER_PATCH);
            objects.push_back(std::move(std::make_unique<V3dBezierPatch>(xdrFile, doublePrecisionFlag)));
            break;

        case ObjectTypes::LINE_COLOR:
            PRINT_OBJECT_TYPE(LINE_COLOR);
            std::cout << "ERROR: No current way to store v3d object: LINE_COLOR" << std::endl;
            break;

        case ObjectTypes::TRIANGLE_COLOR:
            PRINT_OBJECT_TYPE(TRIANGLE_COLOR);
            objects.push_back(std::move(std::make_unique<V3dStraightTriangleWithCornerColors>(xdrFile, doublePrecisionFlag)));
            break;

        case ObjectTypes::QUAD_COLOR:
            PRINT_OBJECT_TYPE(QUAD_COLOR);
            objects.push_back(std::move(std::make_unique<V3dStraightPlanarQuadWithCornerColors>(xdrFile, doublePrecisionFlag)));
            break;

        case ObjectTypes::CURVE_COLOR:
            PRINT_OBJECT_TYPE(CURVE_COLOR);
            std::cout << "ERROR: No current way to store v3d object: CURVE_COLOR" << std::endl;
            break;

        case ObjectTypes::BEZIER_TRIANGLE_COLOR:
            PRINT_OBJECT_TYPE(BEZIER_TRIANGLE_COLOR);
            objects.push_back(std::move(std::make_unique<V3dBezierTriangleWithCornerColors>(xdrFile, doublePrecisionFlag)));
            break;

        case ObjectTypes::BEZIER_PATCH_COLOR:
            PRINT_OBJECT_TYPE(BEZIER_PATCH_COLOR);
            objects.push_back(std::move(std::make_unique<V3dBezierPatchWithCornerColors>(xdrFile, doublePrecisionFlag)));
            break;

        case ObjectTypes::TRIANGLES:
            PRINT_OBJECT_TYPE(TRIANGLES);
            objects.push_back(std::move(std::make_unique<V3dTriangleGroup>(xdrFile, doublePrecisionFlag)));
            break;

        case ObjectTypes::DISK:
            PRINT_OBJECT_TYPE(DISK);
            objects.push_back(std::move(std::make_unique<V3dDisk>(xdrFile, doublePrecisionFlag)));
            break;

        case ObjectTypes::CYLINDER:
            PRINT_OBJECT_TYPE(CYLINDER);
            objects.push_back(std::move(std::make_unique<V3dCylinder>(xdrFile, doublePrecisionFlag)));
            break;

        case ObjectTypes::TUBE:
            PRINT_OBJECT_TYPE(TUBE);
            objects.push_back(std::move(std::make_unique<V3dTube>(xdrFile, doublePrecisionFlag)));
            break;

        case ObjectTypes::SPHERE:
            PRINT_OBJECT_TYPE(SPHERE);
            objects.push_back(std::move(std::make_unique<V3dSphere>(xdrFile, doublePrecisionFlag)));
            break;

        case ObjectTypes::HALF_SPHERE:
            PRINT_OBJECT_TYPE(HALF_SPHERE);
            objects.push_back(std::move(std::make_unique<V3dHemiSphere>(xdrFile, doublePrecisionFlag)));
            break;

        case ObjectTypes::ANIMATION:
            PRINT_OBJECT_TYPE(ANIMATION);
            std::cout << "ERROR: No current way to store v3d object: ANIMATION" << std::endl;
            break;

        case ObjectTypes::PIXEL:
            PRINT_OBJECT_TYPE(PIXEL);
            objects.push_back(std::move(std::make_unique<V3dPixel>(xdrFile, doublePrecisionFlag)));
            break;
        }
    }

    xdrFile.close();
}

void V3dFile::QueueMesh(int imageWidth, int imageHeight, triple sceneMinBound, triple sceneMaxBound, bool remesh, bool orthographic) {
    for (auto& object : objects) {
        // Record buffer sizes before this object adds its vertices.
        size_t matBefore = materialData.materialVertices.size();
        size_t colBefore = colorData.colorVertices.size();
        size_t lineBefore = lineData.materialVertices.size();

        object->QueueMesh(imageWidth, imageHeight, sceneMinBound, sceneMaxBound, remesh, orthographic);

        // Apply billboard transform if this object has a center index.
        // In the v3d format, centerIndex == 0 means "no billboarding",
        // while centerIndex > 0 indexes into the centers array (1-based).
        UINT ci = object->centerIndex;
        if (ci > 0 && ci <= centers.size()) {
            glm::vec3 c = centers[ci - 1];

            // Transform material vertices added by this object.
            for (size_t i = matBefore; i < materialData.materialVertices.size(); ++i) {
                materialData.materialVertices[i].position = billboardTransform(c, materialData.materialVertices[i].position);
            }

            // Transform color vertices added by this object.
            for (size_t i = colBefore; i < colorData.colorVertices.size(); ++i) {
                colorData.colorVertices[i].position = billboardTransform(c, colorData.colorVertices[i].position);
            }

            // Transform line/curve vertices added by this object.
            for (size_t i = lineBefore; i < lineData.materialVertices.size(); ++i) {
                lineData.materialVertices[i].position = billboardTransform(c, lineData.materialVertices[i].position);
            }
        }
    }
}

Mesh V3dFile::GetMesh() {
    bool hasColorData = !colorData.colorVertices.empty();
    bool hasMaterialData = !materialData.materialVertices.empty();

    Mesh mesh{ };

    if (hasColorData && hasMaterialData) {
        mesh.pipelineMode = MeshPipelineMode::Mixed;

        std::vector<ColorVertex> vertices;
        vertices.reserve(materialData.materialVertices.size() + colorData.colorVertices.size());

        uint32_t colorOffset = static_cast<uint32_t>(materialData.materialVertices.size());

        for (const auto& mv : materialData.materialVertices) {
            ColorVertex v;
            v.position = mv.position;
            v.normal = mv.normal;
            v.material = mv.material + 1;
            v.color = glm::vec4{ 0.0f };
            vertices.push_back(v);
        }

        // Add color vertices with negative indices
        for (const auto& cv : colorData.colorVertices) {
            ColorVertex v;
            v.position = cv.position;
            v.normal = cv.normal;
            v.material = -cv.material - 1;
            v.color = cv.color;
            vertices.push_back(v);
        }

        // Remap color indices to account for the prepended material vertices
        auto colorIndices = colorData.indices;
        for (auto& idx : colorIndices) {
            idx += colorOffset;
        }

        mesh.vertices.resize(vertices.size() * sizeof(ColorVertex));
        std::memcpy((void*)mesh.vertices.data(), (void*)vertices.data(), mesh.vertices.size());
        mesh.indices = materialData.indices;
        mesh.indices.insert(mesh.indices.end(), colorIndices.begin(), colorIndices.end());

    } else if (hasColorData) {
        mesh.pipelineMode = MeshPipelineMode::ColorOnly;

        auto colorVerts = colorData.colorVertices;
        for (auto& v : colorVerts) {
            v.material = -v.material - 1;
        }
        mesh.vertices.resize(colorVerts.size() * (sizeof(ColorVertex)));
        std::memcpy((void*)mesh.vertices.data(), (void*)colorVerts.data(), mesh.vertices.size());
        mesh.indices = colorData.indices;
    } else if (hasMaterialData) {
        mesh.pipelineMode = MeshPipelineMode::MaterialOnly;

        mesh.vertices.resize(materialData.materialVertices.size() * sizeof(MaterialVertex));
        std::memcpy((void*)mesh.vertices.data(), (void*)materialData.materialVertices.data(), mesh.vertices.size());
        mesh.indices = materialData.indices;
    } else {
        std::cout << "ERROR: Model is made up entirely of objects that cannot currently give vertices. It wont be rendered." << std::endl;
    }

    return mesh;
}
