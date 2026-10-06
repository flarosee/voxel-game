#pragma once

#include "Types.hpp"

namespace voxel {
struct ApplicationOptions {
    UInt64 seed = 12345;
    bool smokeTest = false;
    bool sunlightDemo = false;
    bool blockLightDemo = false;
    bool transparencyDemo = false;
};
} // namespace voxel
