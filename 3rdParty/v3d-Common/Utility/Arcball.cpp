#include "Arcball.h"

#include <glm/ext/scalar_constants.hpp>

Arcball::Arcball(const glm::vec2& lastMousePosition, const glm::vec2& mousePosition) {
    glm::vec3 v0 = norm(lastMousePosition);
    glm::vec3 v1 = norm(mousePosition);
    float Dot = glm::dot(v0, v1);
    angle = Dot > 1.0f ? 0.0f : Dot < -1.0f ? glm::pi<float>() : std::acos(Dot);

    axis = glm::normalize(glm::cross(v0, v1));
}

glm::vec3 Arcball::norm(glm::vec2 v) {
    float norm = std::hypot(v.x, v.y);
    if (norm > 1.0) {
        float denom = 1.0f/norm;
        v.x *= denom;
        v.y *= denom;
    }
    return glm::vec3{ v.x, v.y, std::sqrt(std::max(1.0f - v.x * v.x - v.y * v.y, 0.0f))};
}