#pragma once

#include <vector>

enum class MeshPipelineMode {
    MaterialOnly,
    ColorOnly,
    Mixed
};

struct Mesh {
    std::vector<unsigned char> vertices{ };
    std::vector<unsigned int> indices{ };
    MeshPipelineMode pipelineMode{ MeshPipelineMode::MaterialOnly };
};
