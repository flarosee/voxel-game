#include "GameSession.hpp"
#include "../Console.hpp"
#include "../Renderer.hpp"
#include "../world/BlockTypes.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <string>

namespace voxel::game {

GameSession::GameSession(const ApplicationOptions &options, GLFWwindow *window, Renderer &renderer)
    : options_(options), window_(window), renderer_(renderer), input_(window, options.blockLightDemo) {
    const terrain::TerrainGenerator generator(options.seed);
    const auto ground = generator.sampleColumn(8, 12).surfaceHeight;
    roofFixture_ =
        options.sunlightDemo || options.blockLightDemo || options.smokeTest || options.transparencyDemo;
    roofHeight_ = ground + 4;
    for (int z = 4; z < 8; ++z) {
        for (int x = 14; x < 18; ++x) {
            roofHeight_ = std::max(roofHeight_, generator.sampleColumn(x, z).surfaceHeight + 4);
        }
    }
    spawn_ = roofFixture_ ? glm::vec3{20.0F,
                                      static_cast<float>(roofHeight_) +
                                          (options.transparencyDemo || options.smokeTest ? 4.0F : -1.0F),
                                      13.0F}
                          : glm::vec3{8.0F, static_cast<float>(ground) + 8.0F, 12.0F};
    camera_.reset();
    camera_.setPosition(spawn_);
    const auto stamp = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    const auto directory = roofFixture_ ? std::filesystem::path("build") / ("sunlight-demo-" + stamp)
                                        : std::filesystem::path("saves/streamed");
    streamer_ = std::make_unique<streaming::ChunkStreamer>(options.seed, directory, 2);
    streamer_->request(camera_.chunk());
    if (options.smokeTest) {
        smokeTest_.emplace(camera_.chunk(), spawn_, roofHeight_);
        renderer_.toggleConsole();
    }
    const auto title = std::string("VoxelGame | Seed ") + std::to_string(options.seed) +
                       " | 1 grass - 2 lamp - 3 water - 4 glass - 5 leaves | E place - Q remove - V views";
    glfwSetWindowTitle(window_, title.c_str());
    console::info("Terrain seed: ", options.seed,
                  " | Generation worker -> meshing worker -> budgeted main upload\n"
                  "RMB: look | WASD: move | Space/Ctrl: up/down | Shift: fast | R: reset | Esc: exit\n"
                  "E: place | Q: remove | G: regenerate aimed chunk | F5: export | F9: import export\n"
                  "1: grass | 2: lamp | 3: water | 4: glass | 5: leaves | V: materials / UV / normals / "
                  "sunlight / block light.");
}

void GameSession::update(float seconds) {
    input_.update(camera_, renderer_, *streamer_, spawn_, seconds, !options_.smokeTest);
    streamer_->request(camera_.chunk());
    updateScene();
    if (smokeTest_)
        smokeTest_->checkTimeout();
}

void GameSession::updateScene() {
    if (renderer_.uploading())
        return;
    auto scene = streamer_->takeReady();
    if (!scene)
        return;
    if (roofFixture_ && !roofPlaced_) {
        createDemoFixture();
        roofPlaced_ = true;
    } else {
        renderer_.queueScene(std::move(scene));
    }
}

bool GameSession::render() {
    if (!renderer_.draw(camera_, streamer_->revision()))
        return false;
    ++renderedFrames_;
    if (smokeTest_)
        smokeTest_->onFrame(window_, camera_, renderer_, *streamer_);
    return true;
}

void GameSession::close() {
    streamer_->close();
    if (smokeTest_)
        smokeTest_->verifyComplete();
    console::info("Presented ", renderedFrames_, " frames; published scene ", renderer_.meshGeneration(),
                  ".");
}

void GameSession::createDemoFixture() {
    // Isolated test world: a roof spanning x=16, with a skylight hole.
    for (int z = 4; z < 8; ++z)
        for (int x = 14; x < 18; ++x) {
            if (!options_.blockLightDemo && x == 15 && z == 5)
                continue;
            if (!streamer_->edit({x, roofHeight_, z}, world::Air, 3))
                throw std::runtime_error("Roof fixture queue full");
        }
    if ((options_.blockLightDemo || options_.smokeTest) &&
        !streamer_->edit({16, roofHeight_ - 2, 6}, world::Air, world::blocks::Lamp))
        throw std::runtime_error("Lamp fixture queue full");
    if (options_.transparencyDemo || options_.smokeTest) {
        // Three overlapping rows, each spanning the x=16 chunk seam.
        for (int row = 0; row < 3; ++row)
            for (int y = 1; y <= 2; ++y)
                for (int x = 15; x <= 16; ++x) {
                    const world::BlockId material = row == 0   ? world::blocks::Glass
                                                    : row == 1 ? world::blocks::Water
                                                               : world::blocks::Leaves;
                    if (!streamer_->edit({x, roofHeight_ + y, 8 - row * 2}, world::Air, material))
                        throw std::runtime_error("Transparency fixture queue full");
                }
    }
}

} // namespace voxel::game
