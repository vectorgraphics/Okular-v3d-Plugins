#pragma once

#include "V3dFile/V3dFile.h"

// Variable names match Asymptote renderBase.cc exactly.

struct V3dModel {
    friend class V3dModelManager;

    V3dModel(const std::string& filePath, const glm::vec2& minBound = { 0.0f, 0.0f }, const glm::vec2& maxBound = { 1.0f, 1.0f });
    V3dModel(xdr::memixstream& xdrFile, const glm::vec2& minBound = { 0.0f, 0.0f }, const glm::vec2& maxBound = { 1.0f, 1.0f });
    V3dModel(const V3dModel& other) = default;
    V3dModel(V3dModel&& other) noexcept = default;
    V3dModel& operator=(const V3dModel& other) = default;
    V3dModel& operator=(V3dModel&& other) noexcept = default;
    ~V3dModel() = default;

    void initProjection();
    void setProjection(const glm::vec2& displayDimensions);

    // Match Asymptote: setDimensions(Width, Height, X, Y)
    void setDimensions(float Width, float Height, float X, float Y);
    void updateViewMatrix();
    void home();
    void cycleMode();

    void dragModeShift  (const glm::vec2& normalizedMousePosition, const glm::vec2& lastNormalizedMousePosition, const glm::vec2& pageViewSize);
    void dragModeZoom   (const glm::vec2& normalizedMousePosition, const glm::vec2& lastNormalizedMousePosition, const glm::vec2& pageViewSize);
    void dragModePan    (const glm::vec2& normalizedMousePosition, const glm::vec2& lastNormalizedMousePosition, const glm::vec2& pageViewSize);
    void dragModeRotate (const glm::vec2& normalizedMousePosition, const glm::vec2& lastNormalizedMousePosition, const glm::vec2& pageViewSize);

    glm::vec2 minBound; // Normalized [0.0, 1.0] bounds of the model on its page
    glm::vec2 maxBound;

    // Zoom state (matches Asymptote renderBase.cc naming)
    float Zoom{ 1.0f };
    float lastzoom{ 1.0f };
    float Zoom0{ 1.0f };

    // Transform matrices (matches Asymptote naming)
    glm::dmat4 rotateMat{ 1.0 };
    glm::dmat4 viewMat{ 1.0 };
    glm::dmat4 projMat{ 1.0 };
    glm::dmat3 normMat{ 1.0 };

    // Frustum bounds computed by setDimensions (matches Asymptote naming)
    float xmin{ 0.0f }, xmax{ 0.0f };
    float ymin{ 0.0f }, ymax{ 0.0f };

    // Perspective half-height (matches Asymptote: H = -tan(0.5*Angle)*Zmax)
    float H{ 0.0f };

    // Scene center: only cz is non-zero initially; cx,cy shift via pan
    double cx{ 0.0 }, cy{ 0.0 }, cz{ 0.0 };

    // Shift state for drag-mode panning (normalized by Zoom0, like Asymptote's Shift)
    float Xshift{ 0.0f }, Yshift{ 0.0f };

    std::unique_ptr<V3dFile> file{ };

    bool remesh{ true };
    bool initialized{ false };

    DrawMode drawMode{ DRAWMODE_NORMAL };

private:
    bool m_HasChanged{ true };
};

