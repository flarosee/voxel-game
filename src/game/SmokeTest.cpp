#include "SmokeTest.hpp"
#include "../Camera.hpp"
#include "../Renderer.hpp"
#include "../streaming/ChunkStreamer.hpp"
#include "../world/BlockTypes.hpp"
#include <stdexcept>
namespace voxel::game {
SmokeTest::SmokeTest(world::ChunkCoord center, glm::vec3 spawn, std::int64_t height)
    : initialCenter(center), smokeEdit(world::blockAt(center, {1, 1, 0})), spawn_(spawn), roofHeight(height) {
}
void SmokeTest::onFrame(GLFWwindow *window_, Camera &camera, Renderer &renderer,
                        streaming::ChunkStreamer &streamer) {
    ++renderedFrames;
    const auto resetCamera = [&] {
        camera.reset();
        camera.setPosition(spawn_);
    };
    if (renderer.meshGeneration() == streamer.revision()) {
        ++stableFrames;
        if (stableFrames >= 10 && smokeStage < 10) {
            stableFrames = 0;
            bool accepted = true;
            switch (smokeStage++) {
            case 0:
                accepted = streamer.edit(smokeEdit, streamer.world().getBlock(smokeEdit), 65535);
                break;
            case 1:
                accepted = streamer.edit(smokeEdit, 65535, world::Air);
                glfwSetWindowSize(window_, 960, 540);
                break;
            case 2:
                accepted = streamer.regenerate(initialCenter);
                renderer.setDebugView(1);
                break;
            case 3:
                camera.setWorldPosition({initialCenter.x + 1, initialCenter.y, initialCenter.z}, {8, 8, 12});
                break;
            case 4:
                camera.setWorldPosition({INT64_C(1) << 50, initialCenter.y, -(INT64_C(1) << 50)}, {8, 8, 12});
                renderer.setDebugView(2);
                break;
            case 5:
                resetCamera();
                glfwSetWindowSize(window_, 720, 720);
                break;
            case 6:
                accepted = streamer.exportChunk(initialCenter);
                renderer.setDebugView(3);
                break;
            case 7:
                accepted = streamer.importChunk();
                renderer.setDebugView(0);
                break;
            case 8:
                accepted =
                    streamer.edit({16, roofHeight - 2, 6}, streamer.world().getBlock({16, roofHeight - 2, 6}),
                                  world::blocks::Lamp);
                renderer.setDebugView(4);
                break;
            case 9:
                accepted = streamer.edit({16, roofHeight - 2, 6}, world::blocks::Lamp, world::Air);
                renderer.setDebugView(0);
                break;
            }
            if (!accepted)
                throw std::runtime_error("Smoke command rejected");
        }
        if (smokeStage == 10 && stableFrames >= 10 && renderedFrames >= 100)
            glfwSetWindowShouldClose(window_, GLFW_TRUE);
    }
}
void SmokeTest::checkTimeout() const {
    if (std::chrono::steady_clock::now() - start_ > std::chrono::seconds(12))
        throw std::runtime_error("Streaming smoke test timed out");
}
void SmokeTest::verifyComplete() const {
    if (smokeStage != 10 || stableFrames < 10)
        throw std::runtime_error("Streaming smoke test interrupted");
}
} // namespace voxel::game
