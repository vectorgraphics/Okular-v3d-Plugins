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

V3dFile::V3dFile(const std::string& fileName) { 
    xdr::ixstream xdrFile{ fileName.c_str() };

    // Measure the file's total size once, before any read, so load() can
    // validate the file's own length/count claims against what it
    // physically contains. The stream is freshly opened at position 0, so
    // the temporary seek cannot clobber any read state.
    int64_t totalBytes = 0;
    if (xdrFile) {
        xdrFile.seek(0, xdr::xstream::end);
        totalBytes = xdrFile.tell();
        xdrFile.seek(0);
    }

    load(xdrFile, totalBytes);
}

V3dFile::V3dFile(xdr::memixstream& xdrFile, std::size_t totalBytes) {
    load(xdrFile, static_cast<int64_t>(totalBytes));
}

void V3dFile::load(xdr::ixstream& xdrFile, int64_t totalBytes) {
    // A stream that failed to open has no initialized XDR buffer at all:
    // reading from it would call through a garbage function pointer, so
    // refuse to touch it before the first read.
    if (!xdrFile) {
        std::cout << "ERROR: v3d stream is not readable" << std::endl;
        return;
    }

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

    // Set on any failed read or a length/count claim the file cannot
    // physically satisfy: parsing stops entirely instead of continuing on a
    // misaligned stream, which would interpret the remaining bytes as
    // garbage objects.
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

        case ObjectTypes::CENTERS: {
            PRINT_OBJECT_TYPE(CENTERS);
            UINT centersLength;
            if (!(xdrFile >> centersLength)) {
                std::cout << "WARNING: truncated v3d CENTERS (count unreadable), stopping parse." << std::endl;
                stopParsing = true;
                break;
            }

            // The count is untrusted and is allocated before any read can
            // fail, so validate it against what the file physically
            // contains: a real file with N centers holds at least 3 reals
            // per center of center data. Stopping the whole parse (rather
            // than just this object) keeps the stream from being read
            // misaligned afterwards.
            int64_t realBytes = doublePrecisionFlag ? 8 : 4;
            uint64_t centerBytes = static_cast<uint64_t>(centersLength) * 3u * static_cast<uint64_t>(realBytes);
            if (centerBytes > static_cast<uint64_t>(bytesRemaining(xdrFile, totalBytes))) {
                std::cout << "WARNING: centers count (" << centersLength
                          << ") claims more data than the file contains, stopping parse." << std::endl;
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
        }

        case ObjectTypes::HEADER:
            PRINT_OBJECT_TYPE(HEADER);
            UINT headerEntryCount;
            if (!(xdrFile >> headerEntryCount)) {
                std::cout << "WARNING: truncated v3d HEADER (entry count unreadable), stopping parse." << std::endl;
                stopParsing = true;
                break;
            }

            // No cap on the count: each successful iteration consumes at
            // least 8 bytes (key + length), so the number of iterations is
            // bounded by the file's size, and the first failed read (EOF)
            // stops the loop immediately instead of spinning on
            // fast-failing reads.
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

                        // The length is untrusted and the string is
                        // allocated before any read can fail, so validate
                        // it against what the file physically contains.
                        // Stopping the whole parse (not just this entry)
                        // keeps the stream from being read misaligned
                        // afterwards.
                        if (strLen > static_cast<uint64_t>(bytesRemaining(xdrFile, totalBytes))) {
                            std::cout << "WARNING: IMAGE name length (" << strLen
                                      << ") exceeds the data the file contains, stopping parse." << std::endl;
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
                        // 'length' comes straight from the file and is
                        // untrusted: a corrupt value such as 0x7FFFFFFF
                        // would consume (or, on a truncated stream, spin on
                        // fast-failing reads through) billions of words.
                        // Validate it against the data the file physically
                        // still contains; anything larger is corrupt, so
                        // stop parsing.
                        int64_t remaining = bytesRemaining(xdrFile, totalBytes);
                        if (static_cast<uint64_t>(length) * 4u > static_cast<uint64_t>(remaining)) {
                            std::cout << "WARNING: unknown v3d HEADER key (" << headerKey
                                      << ") claims " << length << " words but the file only contains "
                                      << remaining / 4 << " more, stopping parse." << std::endl;
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
            objects.push_back(std::move(std::make_unique<V3dTriangleGroup>(xdrFile, doublePrecisionFlag, totalBytes, stopParsing)));
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

