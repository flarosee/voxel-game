#pragma once

#include "../world/Coordinates.hpp"
#include <glm/vec3.hpp>
struct GLFWwindow;

namespace voxel {
class Camera;
class Renderer;
namespace streaming {
class ChunkStreamer;
}
namespace game {

// Borrows the native window; owns input edges and mouse-capture state only.
class InputController final {
  public:
    explicit InputController(GLFWwindow *window, bool selectLamp = false);
    ~InputController();
    InputController(const InputController &) = delete;
    InputController &operator=(const InputController &) = delete;
    void update(Camera &camera, Renderer &renderer, streaming::ChunkStreamer &streamer, glm::vec3 spawn,
                float seconds, bool enabled);

  private:
    void updateMouseLook(Camera &camera, bool wantsLook);
    void updateGameplay(Camera &camera, Renderer &renderer, streaming::ChunkStreamer &streamer,
                        glm::vec3 spawn, float seconds, bool gameInput);
    GLFWwindow *window_;
    bool previousPlace = false, previousRemove = false, previousSave = false, previousLoad = false;
    bool previousRegenerate = false, previousView = false, previousConsoleKey = false;
    bool looking = false;
    double previousX = 0, previousY = 0;
    std::uint32_t debugView = 0;
    world::BlockId selectedBlock = 1;
};
} // namespace game
} // namespace voxel
