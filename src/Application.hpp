#pragma once
#include "ApplicationOptions.hpp"

namespace voxel {
// Composition root: constructs systems in lifetime order and coordinates frames.
class Application final {
  public:
    void run(const ApplicationOptions &options);
};
} // namespace voxel