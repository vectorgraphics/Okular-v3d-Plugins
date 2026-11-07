#include <iostream>
#include <fstream>
#include <queue>
#include <memory>

#include "V3dFile.h"

#include "xstream.h"

#include "V3dUtil.h"

#include "rgba.h"
#include "bezierpatch.h"
#include "beziercurve.h"

// V3dObjects.cpp declares camp::materials; bring the declaration into scope.
struct V3dMaterial;
namespace camp { extern const std::vector<V3dMaterial>* materials; }
namespace camp { extern double pixelsPerBp; }

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
    #define PRINT_OBJECT_TYPE(t) do { if (std::getenv("OKULAR_V3D_DEBUG")) std::cout << #t << std::endl; } while(0)
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

namespace {
// A real v3d HEADER object holds a couple dozen entries (canvas size,
// bounds, lights, ...). An absurd headerEntryCount on a truncated stream
// would make the entry loop spin on fast-failing reads for up to 2^32
// iterations -- tens of seconds of CPU on the main thread. Cap it.
constexpr unsigned kMaxHeaderEntries = 10000;

// Hard cap on an unknown header entry's payload (in XDR words). Forward
// compatibility entries are small (the largest known one, IMAGE, is a name
// of at most 4096 bytes); when the stream's remaining size is measurable,
// the tighter of this cap and the remaining bytes is enforced.
constexpr uint64_t kMaxUnknownHeaderWords = 1000000;

// Bytes left in the stream from the current position, or -1 when the size
// cannot be measured (some in-memory streams ignore the seek direction).
// Only call on a good stream: the temporary seeks clear the stream's
// failure flags, so calling this on a failed stream would hide the error.
int64_t remainingBytes(xdr::ixstream& xdrFile) {
    if (!xdrFile) {
        return -1;
    }
    int64_t cur = xdrFile.tell();
    if (cur < 0) {
        return -1;
    }
    xdrFile.seek(0, xdr::xstream::end);
    int64_t size = xdrFile.tell();
    xdrFile.seek(cur);
    return (size >= cur) ? (size - cur) : -1;
}
}

V3dFile::V3dFile(const std::string& fileName) { 
    xdr::ixstream xdrFile{ fileName.c_str() };
    load(xdrFile);
}

V3dFile::V3dFile(xdr::memixstream& xdrFile) {
    load(xdrFile);
}

void V3dFile::load(xdr::ixstream& xdrFile) {
    // A failed read leaves the target UNINITIALIZED (xstream only sets the
    // stream's fail flag), so the stream state must be checked before any
    // value read this way is used.
    xdrFile >> versionNumber;
    if (!xdrFile) {
        std::cout << "ERROR: failed to read v3d version number (empty or corrupt file)" << std::endl;
        return;
    }
    if (versionNumber < 1) {
        std::cout << "ERROR: v3d file version " << versionNumber
                  << " is not supported (need >= 1)" << std::endl;
        return;
    }
    xdrFile >> doublePrecisionFlag;
    if (!xdrFile) {
        std::cout << "ERROR: failed to read v3d precision flag (corrupt file)" << std::endl;
        return;
    }

    // Set on any cap overrun or failed read: parsing stops entirely instead
    // of continuing on a misaligned stream, which would interpret the
    // remaining bytes as garbage objects.
    bool stopParsing = false;

    UINT objectType;
    while (!stopParsing && xdrFile >> objectType) {
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
            if (versionNumber >= 2)
                xdrFile >> material.lightOn;
            else
                material.lightOn = 1.0f;

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
            if (!(xdrFile >> centersLength)) {
                std::cout << "WARNING: truncated v3d CENTERS (count unreadable), stopping parse." << std::endl;
                stopParsing = true;
                break;
            }

            // Sanity cap to prevent bad_alloc from a crafted file. Stopping
            // the whole parse (rather than just this object) keeps the
            // stream from being read misaligned afterwards.
            if (centersLength > 1000000) {
                std::cout << "WARNING: centers count (" << centersLength << ") exceeds limit, stopping parse." << std::endl;
                stopParsing = true;
                break;
            }

            if (centersLength > 0) {
                centers.resize(centersLength);

                for (UINT i = 0; !stopParsing && i < centersLength; ++i) {
                    centers[i].x = readReal(xdrFile, doublePrecisionFlag);
                    centers[i].y = readReal(xdrFile, doublePrecisionFlag);
                    centers[i].z = readReal(xdrFile, doublePrecisionFlag);
                    if (!xdrFile) {
                        std::cout << "WARNING: truncated v3d CENTERS, stopping parse." << std::endl;
                        stopParsing = true;
                    }
                }
            }

            break;

        case ObjectTypes::HEADER:
            PRINT_OBJECT_TYPE(HEADER);
            UINT headerEntryCount;
            if (!(xdrFile >> headerEntryCount)) {
                std::cout << "WARNING: truncated v3d HEADER (entry count unreadable), stopping parse." << std::endl;
                stopParsing = true;
                break;
            }

            // A crafted (or corrupt) file may claim billions of entries; on a
            // truncated stream each of those reads fails instantly but the
            // loop would still spin 2^32 times, freezing the main thread.
            if (headerEntryCount > kMaxHeaderEntries) {
                std::cout << "WARNING: v3d HEADER entry count (" << headerEntryCount
                          << ") exceeds limit of " << kMaxHeaderEntries << ", stopping parse." << std::endl;
                stopParsing = true;
                break;
            }

            for (UINT i = 0; !stopParsing && i < headerEntryCount; ++i) {
                UINT headerKey;
                if (!(xdrFile >> headerKey)) {
                    std::cout << "WARNING: truncated v3d HEADER (entry key unreadable), stopping parse." << std::endl;
                    stopParsing = true;
                    break;
                }

                UINT length;
                if (!(xdrFile >> length)) {
                    std::cout << "WARNING: truncated v3d HEADER (entry length unreadable), stopping parse." << std::endl;
                    stopParsing = true;
                    break;
                }

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

                    case LIGHT: {
                        V3dHeaderInfo::Light l{};
                        l.direction.x = readReal(xdrFile, doublePrecisionFlag);
                        l.direction.y = readReal(xdrFile, doublePrecisionFlag); 
                        l.direction.z = readReal(xdrFile, doublePrecisionFlag);

                        xdrFile >> l.color.r;
                        xdrFile >> l.color.g;       
                        xdrFile >> l.color.b;
                        headerInfo.lights.push_back(l);
                        break;
                    }       

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

                    case IMAGE: {
                        uint64_t strLen;
                        if (!(xdrFile >> strLen)) {
                            std::cout << "WARNING: truncated v3d HEADER IMAGE entry, stopping parse." << std::endl;
                            stopParsing = true;
                            break;
                        }

                        // Sanity cap to prevent bad_alloc / a huge allocation from a
                        // crafted file. An IBL name is a short relative path (PATH_MAX
                        // is 4096); longer than that it can't be a valid path anyway.
                        // Stopping the whole parse (not just this entry) keeps the
                        // stream from being read misaligned afterwards.
                        if (strLen > 4096) {
                            std::cout << "WARNING: IMAGE name length (" << strLen
                                      << ") exceeds limit, stopping parse." << std::endl;
                            stopParsing = true;
                            break;
                        }

                        std::string s;
                        s.resize(strLen);
                        bool truncated = false;
                        for (uint64_t k = 0; k < strLen; ++k) {
                            xdr::xbyte c;
                            xdrFile >> c;
                            if (!xdrFile) {
                                truncated = true;
                                break;
                            }
                            s[k] = static_cast<char>(c);
                        }
                        if (!truncated) {
                            // Advance remaining words to match getWordSize: (strLen+3)/4 data words
                            uint64_t totalDataWords = (strLen + 3) / 4;
                            for (uint64_t k = strLen; k < totalDataWords * 4; ++k) {
                                xdr::xbyte dummy;
                                xdrFile >> dummy;
                                if (!xdrFile) {
                                    truncated = true;
                                    break;
                                }
                            }
                        }
                        if (truncated) {
                            std::cout << "WARNING: truncated v3d HEADER IMAGE name, stopping parse." << std::endl;
                            stopParsing = true;
                            break;
                        }
                        headerInfo.imageName = std::move(s);
                        break;
                    }    

                    default: {
                        // Skip unknown header entries by consuming 'length' XDR words.
                        // This prevents stream corruption when newer v3d files contain
                        // header keys that this version doesn't recognize.
                        //
                        // 'length' comes straight from the file and is untrusted: a
                        // corrupt value such as 0x7FFFFFFF would spin the loop below
                        // for billions of iterations. Cap it against the data actually
                        // remaining in the stream (when its size is measurable) and a
                        // hard limit; anything larger is corrupt, so stop parsing.
                        uint64_t maxWords = kMaxUnknownHeaderWords;
                        int64_t remaining = remainingBytes(xdrFile);
                        if (remaining >= 0 && static_cast<uint64_t>(remaining / 4) < maxWords) {
                            maxWords = static_cast<uint64_t>(remaining / 4);
                        }
                        if (static_cast<uint64_t>(length) > maxWords) {
                            std::cout << "WARNING: unknown v3d HEADER key (" << headerKey
                                      << ") claims " << length << " words but at most "
                                      << maxWords << " are available, stopping parse." << std::endl;
                            stopParsing = true;
                            break;
                        }

                        for (UINT j = 0; j < length; ++j) {
                            uint32_t skipWord;
                            xdrFile >> skipWord;
                            if (!xdrFile) {
                                std::cout << "WARNING: truncated v3d HEADER (unknown-key payload unreadable), stopping parse." << std::endl;
                                stopParsing = true;
                                break;
                            }
                        }
                        break;
                    }
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

void V3dFile::QueueMesh(int imageWidth, int imageHeight, triple sceneMinBound, triple sceneMaxBound, bool remesh, bool orthographic, DrawMode drawMode) {
    // Set global materials pointer so V3dObject::QueueMesh can check material alpha.
    // Matches Asymptote: drawBezierPatch::render() reads diffuse.A from the current
    // material to set transparent = (diffuse.A < 1.0).
    camp::materials = &materials;

    // Set the current pixels-per-bp scale: the render image is the v3d canvas
    // (headerInfo.canvasWidth bp) at imageWidth px. Mirrors Asymptote's
    // camp::pixelsPerBp(), used by drawPixel::bounds() to convert the pixel's
    // device-pixel width to PostScript units for the offscreen cull box.
    camp::pixelsPerBp = headerInfo.canvasWidth > 0
        ? imageWidth / (double)headerInfo.canvasWidth
        : 1.0;

    for (auto& object : objects) {
        // Record buffer sizes before this object adds its vertices.
        size_t matBefore = materialData.materialVertices.size();
        size_t colBefore = colorData.colorVertices.size();
        size_t lineBefore = lineData.materialVertices.size();

        object->QueueMesh(imageWidth, imageHeight, sceneMinBound, sceneMaxBound, remesh, orthographic, drawMode);

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

