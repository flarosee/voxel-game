#include "InputController.hpp"
#include "../Camera.hpp"
#include "../Console.hpp"
#include "../Renderer.hpp"
#include "../world/BlockTypes.hpp"
#include "../world/Raycast.hpp"
#include <array>
#include <cmath>
#include <optional>
namespace {
std::optional<voxel::world::RayHit> aimedBlock(const voxel::world::World &world,
                                               const voxel::Camera &camera) {
    const auto position = camera.position();
    const auto direction = camera.forward();
    const std::array<double, 3> integral{std::floor(static_cast<double>(position.x)),
                                         std::floor(static_cast<double>(position.y)),
                                         std::floor(static_cast<double>(position.z))};
    for (double value : integral) {
        if (!std::isfinite(value) || value < -9223372036854775808.0 || value >= 9223372036854775808.0)
            return std::nullopt;
    }
    return voxel::world::raycast(
        world,
        voxel::world::blockAt(camera.chunk(), {static_cast<int>(integral[0]), static_cast<int>(integral[1]),
                                               static_cast<int>(integral[2])}),
        {position.x - integral[0], position.y - integral[1], position.z - integral[2]},
        {direction.x, direction.y, direction.z}, 8.0);
}

} // namespace
namespace voxel::game {
InputController::InputController(GLFWwindow *window, bool selectLamp)
    : window_(window), selectedBlock(selectLamp ? world::blocks::Lamp : 1) {}
InputController::~InputController() {
    glfwSetInputMode(window_, GLFW_CURSOR, GLFW_CURSOR_NORMAL);
    if (glfwRawMouseMotionSupported())
        glfwSetInputMode(window_, GLFW_RAW_MOUSE_MOTION, GLFW_FALSE);
}
void InputController::update(Camera &camera, Renderer &renderer, streaming::ChunkStreamer &streamer,
                             glm::vec3 spawn, float seconds, bool enabled) {
    const bool focused = glfwGetWindowAttrib(window_, GLFW_FOCUSED) == GLFW_TRUE;
    const bool consoleKey = glfwGetKey(window_, GLFW_KEY_GRAVE_ACCENT) == GLFW_PRESS;
    if (focused && enabled && consoleKey && !previousConsoleKey)
        renderer.toggleConsole();
    previousConsoleKey = consoleKey;
    const bool gameInput = focused && enabled && !renderer.consoleVisible();
    const bool wantsLook = gameInput && glfwGetMouseButton(window_, GLFW_MOUSE_BUTTON_RIGHT) == GLFW_PRESS;
    updateMouseLook(camera, wantsLook);
    updateGameplay(camera, renderer, streamer, spawn, seconds, gameInput);
}

void InputController::updateMouseLook(Camera &camera, bool wantsLook) {
    if (wantsLook != looking) {
        looking = wantsLook;
        glfwSetInputMode(window_, GLFW_CURSOR, looking ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
        if (glfwRawMouseMotionSupported())
            glfwSetInputMode(window_, GLFW_RAW_MOUSE_MOTION, looking);
        glfwGetCursorPos(window_, &previousX, &previousY);
    }
    if (looking) {
        double x = 0;
        double y = 0;
        glfwGetCursorPos(window_, &x, &y);
        camera.rotate(static_cast<float>(previousX - x) * 0.0025F,
                      static_cast<float>(previousY - y) * 0.0025F);
        previousX = x;
        previousY = y;
    }
}

void InputController::updateGameplay(Camera &camera, Renderer &renderer, streaming::ChunkStreamer &streamer,
                                     glm::vec3 spawn, float seconds, bool gameInput) {
    if (!gameInput) {
        previousPlace = previousRemove = previousSave = previousLoad = previousRegenerate = previousView =
            false;
        return;
    }
    const auto key = [this](int code) { return glfwGetKey(window_, code) == GLFW_PRESS ? 1.0F : 0.0F; };
    camera.move({key(GLFW_KEY_D) - key(GLFW_KEY_A), key(GLFW_KEY_SPACE) - key(GLFW_KEY_LEFT_CONTROL),
                 key(GLFW_KEY_W) - key(GLFW_KEY_S)},
                seconds, key(GLFW_KEY_LEFT_SHIFT) > 0);
    if (key(GLFW_KEY_R) > 0) {
        camera.reset();
        camera.setPosition(spawn);
    }
    const bool place = key(GLFW_KEY_E) > 0, remove = key(GLFW_KEY_Q) > 0, save = key(GLFW_KEY_F5) > 0,
               load = key(GLFW_KEY_F9) > 0, regenerate = key(GLFW_KEY_G) > 0,
               changeView = key(GLFW_KEY_V) > 0;
    if (changeView && !previousView)
        renderer.setDebugView(++debugView % 5);
    if (key(GLFW_KEY_1) > 0)
        selectedBlock = 1;
    if (key(GLFW_KEY_2) > 0)
        selectedBlock = world::blocks::Lamp;
    if (key(GLFW_KEY_3) > 0)
        selectedBlock = world::blocks::Water;
    if (key(GLFW_KEY_4) > 0)
        selectedBlock = world::blocks::Glass;
    if (key(GLFW_KEY_5) > 0)
        selectedBlock = world::blocks::Leaves;
    const auto hit = aimedBlock(streamer.world(), camera);
    bool accepted = true;
    if (hit && remove && !previousRemove)
        accepted = streamer.edit(hit->block, hit->id, world::Air);
    if (hit && hit->adjacent && place && !previousPlace)
        accepted = streamer.edit(*hit->adjacent, world::Air, selectedBlock) && accepted;
    if (hit && regenerate && !previousRegenerate)
        accepted = streamer.regenerate(world::addressOf(hit->block).chunk) && accepted;
    if (hit && save && !previousSave)
        accepted = streamer.exportChunk(world::addressOf(hit->block).chunk) && accepted;
    if (load && !previousLoad)
        accepted = streamer.importChunk() && accepted;
    if (!accepted)
        console::warning("Streaming command queue full; action was not accepted.");
    previousPlace = place;
    previousRemove = remove;
    previousSave = save;
    previousLoad = load;
    previousRegenerate = regenerate;
    previousView = changeView;
}

} // namespace voxel::game