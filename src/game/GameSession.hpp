#pragma once

#include "../ApplicationOptions.hpp"
#include "../Camera.hpp"
#include "../streaming/ChunkStreamer.hpp"
#include "InputController.hpp"
#include "SmokeTest.hpp"
#include <memory>
#include <optional>

namespace voxel {
class Renderer;
namespace game {
// Owns camera, world streaming, controls, and optional demo/test state.
// Borrows the renderer and window; both must outlive this session.
class GameSession final {
  public:
    GameSession(const ApplicationOptions &options, GLFWwindow *window, Renderer &renderer);
    void update(float seconds);
    bool render();
    void close();

  private:
    void updateScene();
    void createDemoFixture();
    ApplicationOptions options_;
    GLFWwindow *window_;
    Renderer &renderer_;
    Camera camera_;
    glm::vec3 spawn_{};
    std::int64_t roofHeight_ = 0;
    bool roofFixture_ = false;
    bool roofPlaced_ = false;
    std::unique_ptr<streaming::ChunkStreamer> streamer_;
    InputController input_;
    std::optional<SmokeTest> smokeTest_;
    unsigned renderedFrames_ = 0;
};
} // namespace game
} // namespace voxel
