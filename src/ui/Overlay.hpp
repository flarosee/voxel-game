#pragma once

#include "ConsolePanel.hpp"
#include <volk.h>
struct GLFWwindow;
struct ImGuiContext;

namespace voxel::ui {

// Main-thread UI owner. Renderer supplies the Vulkan lifetime and render pass.
class Overlay final {
public:
    Overlay() = default;
    ~Overlay();
    Overlay(const Overlay&) = delete;
    Overlay& operator=(const Overlay&) = delete;
    void initialize(GLFWwindow* window);
    void initializeVulkan(VkInstance instance, VkPhysicalDevice physical, VkDevice device,
                          unsigned queueFamily, VkQueue queue, VkRenderPass pass, unsigned imageCount);
    void shutdownVulkan();
    void render(VkCommandBuffer command);
    ConsolePanel console;

private:
    ImGuiContext* context_ = nullptr;
    GLFWwindow* window_ = nullptr;
    bool platformReady_ = false;
    bool vulkanReady_ = false;
};

} // namespace voxel::ui
