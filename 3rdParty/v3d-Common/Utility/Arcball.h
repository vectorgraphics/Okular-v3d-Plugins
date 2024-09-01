#pragma once

#include <glm/glm.hpp>

struct Arcball {
    float angle;
    glm::vec3 axis;

    Arcball(const glm::vec2& lastMousePosition, const glm::vec2& mousePosition);

    glm::vec3 norm(glm::vec2 v);
};