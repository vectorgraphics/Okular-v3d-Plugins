#pragma once

#include <vector>

struct Mesh {
    std::vector<unsigned char> vertices{ };
    std::vector<unsigned int> indices{ };
};
