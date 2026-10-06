#pragma once

#include "../world/Coordinates.hpp"
#include <chrono>
#include <glm/vec3.hpp>
struct GLFWwindow;
namespace voxel {
class Camera;
class Renderer;
namespace streaming {
class ChunkStreamer;
}
namespace game {
// Scripted verification, separate from normal player controls and game rules.
class SmokeTest final {
  public:
    SmokeTest(world::ChunkCoord center, glm::vec3 spawn, std::int64_t roofHeight);
    void onFrame(GLFWwindow *window, Camera &camera, Renderer &renderer, streaming::ChunkStreamer &streamer);
    void checkTimeout() const;
    void verifyComplete() const;

  private:
    world::ChunkCoord initialCenter;
    world::BlockCoord smokeEdit;
    glm::vec3 spawn_;
    std::int64_t roofHeight;
    unsigned renderedFrames = 0, smokeStage = 0, stableFrames = 0;
    std::chrono::steady_clock::time_point start_ = std::chrono::steady_clock::now();
};
} // namespace game
} // namespace voxel
