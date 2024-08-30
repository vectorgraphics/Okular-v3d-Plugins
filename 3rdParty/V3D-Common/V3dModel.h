#pragma once

#include <string>
#include <memory>

#include <glm/glm.hpp>

#include "V3dFile/V3dFile.h"

class V3dModelManager;

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

    void setDimensions(float width, float height, float X, float Y);
    void updateViewMatrix();

    void dragModeShift  (const glm::vec2& normalizedMousePosition, const glm::vec2& lastNormalizedMousePosition, const glm::vec2& pageViewSize);
    void dragModeZoom   (const glm::vec2& normalizedMousePosition, const glm::vec2& lastNormalizedMousePosition, const glm::vec2& pageViewSize);
    void dragModePan    (const glm::vec2& normalizedMousePosition, const glm::vec2& lastNormalizedMousePosition, const glm::vec2& pageViewSize);
    void dragModeRotate (const glm::vec2& normalizedMousePosition, const glm::vec2& lastNormalizedMousePosition, const glm::vec2& pageViewSize);

    glm::vec2 minBound; // Normalized [0.0, 1.0] bounds of the model on its page
    glm::vec2 maxBound;

    float zoom{ 1.0f };
    float lastZoom{ zoom };

    glm::mat4 rotationMatrix{ 1.0f };
    glm::mat4 viewMatrix{ 1.0f };
    glm::mat4 projectionMatrix{ 1.0f };

    float xShift;
    float yShift;

    float h{ };
    glm::vec3 center{ };
    glm::vec2 shift{ };

    struct ViewParam {
        glm::vec3 minValues{ };
        glm::vec3 maxValues{ };
    } viewParam;

    std::unique_ptr<V3dFile> file{ };

private:
    bool m_HasChanged{ true };
};