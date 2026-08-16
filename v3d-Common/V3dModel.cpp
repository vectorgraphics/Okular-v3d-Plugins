#include "V3dModel.h"

// glmCommon.h (via V3dObject.h -> render.h) defines GLM_FORCE_DEPTH_ZERO_TO_ONE
// before including glm.  Include it first to ensure our glm calls match vkrender.cc.

#include "Utility/Arcball.h"
#include "xstream.h"

V3dModel::V3dModel(const std::string& filePath, const glm::vec2& minBound, const glm::vec2& maxBound) 
    : minBound(minBound), maxBound(maxBound) {
        
    if (!filePath.empty()) {
        file = std::make_unique<V3dFile>(filePath);
        initProjection();
    }
}

V3dModel::V3dModel(xdr::memixstream& xdrFile, const glm::vec2& minBound, const glm::vec2& maxBound) 
    : minBound(minBound), maxBound(maxBound) {

    file = std::make_unique<V3dFile>(xdrFile);

    initProjection();
}

// Match Asymptote renderBase.cc constructor exactly:
//   Shift = args.shift / Zoom0;
//   H = orthographic ? 0.0 : -tan(0.5*Angle)*Zmax;
//   cz = 0.5*(Zmin+Zmax)
void V3dModel::initProjection() {
    H = -std::tan(0.5f * file->headerInfo.angleOfView) * file->headerInfo.maxBound.z;

    cx = 0.0;
    cy = 0.0;
    cz = 0.5 * (file->headerInfo.minBound.z + file->headerInfo.maxBound.z);

    Zoom = file->headerInfo.initialZoom;
    lastzoom = Zoom;
    Zoom0 = file->headerInfo.initialZoom;

    // Normalize shift by Zoom0, exactly like Asymptote: Shift = args.shift / Zoom0
    Xshift = file->headerInfo.viewportShift.x / Zoom0;
    Yshift = file->headerInfo.viewportShift.y / Zoom0;
}

void V3dModel::home() {
    cx = 0.0;
    cy = 0.0;

    Zoom = Zoom0;
    lastzoom = Zoom0;

    rotateMat = glm::dmat4{ 1.0 };

    Xshift = 0.0f;
    Yshift = 0.0f;

    m_HasChanged = true;
    remesh = true;
}

void V3dModel::cycleMode() {
    drawMode = DrawMode((drawMode + 1) % NUM_DRAW_MODES);
    m_HasChanged = true;
    remesh = true;
}

// Match Asymptote renderBase.cc setProjection():
//   setDimensions(Width, Height, X, Y);
//   ortho/frustum(xmin,xmax,ymin,ymax,-Zmax,-Zmin);
void V3dModel::setProjection(const glm::vec2& displayDimensions) {
    setDimensions(displayDimensions.x, displayDimensions.y, 0.0f, 0.0f);

    float Zmin = file->headerInfo.minBound.z;
    float Zmax = file->headerInfo.maxBound.z;

    if (file->headerInfo.orthographic) {
        projMat = glm::ortho(xmin, xmax, ymin, ymax, -Zmax, -Zmin);
    } else {
        projMat = glm::frustum(xmin, xmax, ymin, ymax, -Zmax, -Zmin);
    }

    updateViewMatrix();
}

// Match Asymptote renderBase.cc setDimensions() exactly.
// Variable names: Width, Height, X, Y, aspect, zoom, xshift, yshift, zoominv
void V3dModel::setDimensions(float Width, float Height, float X, float Y) {
    float aspect = Width / Height;

    // Asymptote: double zoom = Zoom * zoomFactor;  (zoomFactor defaults to 1.0)
    float zoom = Zoom;

    // Asymptote: Shift is already normalized by Zoom0 in constructor
    //            xshift = (X/Width + Shift.x * Xfactor) * zoom
    // We match this: Xshift = viewportShift.x / Zoom0, Xfactor=1.0
    float xshift = (X / Width + Xshift) * zoom;
    float yshift = (Y / Height + Yshift) * zoom;

    float zoominv = 1.0f / zoom;

    // Scene bounds (matches Asymptote: Xmin, Xmax, Ymin, Ymax)
    float Xmin = file->headerInfo.minBound.x;
    float Xmax = file->headerInfo.maxBound.x;
    float Ymin = file->headerInfo.minBound.y;
    float Ymax = file->headerInfo.maxBound.y;

    if (file->headerInfo.orthographic) {
        float xsize = Xmax - Xmin;
        float ysize = Ymax - Ymin;

        if (xsize < ysize * aspect) {
            float r = 0.5f * ysize * aspect * zoominv;

            float X0 = 2.0f * r * xshift;
            float Y0 = ysize * zoominv * yshift;

            xmin = -r - X0;
            xmax = r - X0;
            ymin = Ymin * zoominv - Y0;
            ymax = Ymax * zoominv - Y0;
        } else {
            float r = 0.5f * xsize * zoominv / aspect;

            float X0 = xsize * zoominv * xshift;
            float Y0 = 2.0f * r * yshift;

            xmin = Xmin * zoominv - X0;
            xmax = Xmax * zoominv - X0;
            ymin = -r - Y0;
            ymax = r - Y0;
        }
    } else {
        float r = H * zoominv;
        float rAspect = r * aspect;

        float X0 = 2.0f * rAspect * xshift;
        float Y0 = 2.0f * r * yshift;

        xmin = -rAspect - X0;
        xmax = rAspect - X0;
        ymin = -r - Y0;
        ymax = r - Y0;
    }
}

// Match Asymptote renderBase.cc update():
//   viewMat = translate(translate(dmat4(1.0), dvec3(cx, cy, cz)) * rotateMat, dvec3(0, 0, -cz));
// Match Asymptote renderBase.cc: normMat = dmat3(inverse(viewMat));
void V3dModel::updateViewMatrix() {
    glm::dvec3 c(cx, cy, cz);
    viewMat = glm::translate(glm::dmat4{ 1.0 }, c);
    viewMat = viewMat * rotateMat;
    viewMat = glm::translate(viewMat, glm::dvec3(0.0, 0.0, -cz));
    normMat = glm::dmat3(glm::inverse(viewMat));
    m_HasChanged = true;
}

void V3dModel::dragModeShift(const glm::vec2& normalizedMousePosition, const glm::vec2& lastNormalizedMousePosition, const glm::vec2& /*displayDimensions*/) {
    if (normalizedMousePosition == lastNormalizedMousePosition) return;
    // Match vkrender.cc shift(): X += dx * Zoominv, then xshift = (X/Width) * zoom.
    // Our inputs are already normalized [0,1] within the model area, so:
    //   deltaNorm * Width = pixel delta
    //   frustum_shift = (pixel_delta / Zoom) / Width * zoom = deltaNorm
    // The zoom factors cancel since setDimensions multiplies by zoom.
    // No displayDimensions scaling needed — that was causing "images fly off screen".
    Xshift += (normalizedMousePosition.x - lastNormalizedMousePosition.x);
    Yshift -= (normalizedMousePosition.y - lastNormalizedMousePosition.y);

    m_HasChanged = true;
}

void V3dModel::dragModeZoom(const glm::vec2& normalizedMousePosition, const glm::vec2& lastNormalizedMousePosition, const glm::vec2& pageViewSize) {
    if (normalizedMousePosition == lastNormalizedMousePosition) return;
    float diff = lastNormalizedMousePosition.y - normalizedMousePosition.y;

    float stepPower = file->headerInfo.zoomStep * (pageViewSize.y / 2.0f) * diff;
    const float limit = std::log(0.1f * std::numeric_limits<float>::max()) / std::log(file->headerInfo.zoomFactor);

    if (std::abs(stepPower) < limit) {
        Zoom *= std::pow(file->headerInfo.zoomFactor, stepPower);

        float maxZoom = std::sqrt(std::numeric_limits<float>::max());
        float minZoom = 1 / maxZoom;

        if (Zoom <= minZoom) {
            Zoom = minZoom;
        } else if (Zoom >= maxZoom) {
            Zoom = maxZoom;
        }

        m_HasChanged = true;
        remesh = true;
    }
}

void V3dModel::dragModePan(const glm::vec2& normalizedMousePosition, const glm::vec2& lastNormalizedMousePosition, const glm::vec2& pageViewSize) {
    if (normalizedMousePosition == lastNormalizedMousePosition) return;
    if (file->headerInfo.orthographic) {
        dragModeShift(normalizedMousePosition, lastNormalizedMousePosition, pageViewSize);
    } else {
        cx += (normalizedMousePosition.x - lastNormalizedMousePosition.x) * (xmax - xmin);
        cy -= (normalizedMousePosition.y - lastNormalizedMousePosition.y) * (ymax - ymin);
    }

    m_HasChanged = true;
}

void V3dModel::dragModeRotate(const glm::vec2& normalizedMousePosition, const glm::vec2& lastNormalizedMousePosition, const glm::vec2& pageViewSize) {
    (void)pageViewSize;
    float arcballFactor = 1.0f;

    if (normalizedMousePosition == lastNormalizedMousePosition) { return; }

    glm::vec2 delta = normalizedMousePosition - lastNormalizedMousePosition;
    if (glm::length(delta) < 0.001f) { return; }

    // Convert [0,1] mouse positions to NDC [-1,1] and flip Y to match the
    // reference arcball implementation in renderBase.cc:
    //   Arcball(xprev*2/Width - 1, 1 - yprev*2/Height, ...)
    glm::vec2 lastNDC = { lastNormalizedMousePosition.x * 2.0f - 1.0f,
                          1.0f - lastNormalizedMousePosition.y * 2.0f };
    glm::vec2 currNDC = { normalizedMousePosition.x * 2.0f - 1.0f,
                          1.0f - normalizedMousePosition.y * 2.0f };

    Arcball arcball{ lastNDC, currNDC };
    float angle = arcball.angle;
    glm::vec3 axis = arcball.axis;

    if (glm::length(axis) < 0.001f) { return; }

    float angleRadians = 2.0f * angle / Zoom * arcballFactor;
    glm::dmat4 temp = glm::rotate(glm::dmat4(1.0), static_cast<double>(angleRadians), glm::dvec3(axis.x, axis.y, axis.z));
    rotateMat = temp * rotateMat;

    m_HasChanged = true;
}

