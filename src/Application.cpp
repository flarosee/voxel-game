#include "Application.hpp"
#include "Renderer.hpp"
#include "game/GameSession.hpp"
#include "platform/Runtime.hpp"
#include <algorithm>
#include <chrono>

namespace voxel {
void Application::run(const ApplicationOptions &options) {
    // Reverse destruction: session/workers, renderer/UI, device/window.
    platform::Runtime runtime;
    runtime.initialize();
    Renderer renderer;
    runtime.initializeRenderer(renderer);
    game::GameSession session(options, runtime.window(), renderer);

    auto previous = std::chrono::steady_clock::now();
    while (!glfwWindowShouldClose(runtime.window())) {
        glfwPollEvents();
        const auto now = std::chrono::steady_clock::now();
        const float seconds = std::min(std::chrono::duration<float>(now - previous).count(), 0.1F);
        previous = now;
        session.update(seconds);
        if (!session.render()) {
            // Let events arrive without blocking on workers or GPU fences.
            glfwWaitEventsTimeout(0.001);
        }
    }
    session.close();
}
} // namespace voxel