#pragma once

// Vulkan types must exist before GLFW declares its Vulkan helper functions.
// clang-format off
#include <volk.h>
#include <GLFW/glfw3.h>
// clang-format on
#include <cstdint>

namespace voxel {
class Renderer;
namespace platform {

// Owns the native window and Vulkan device. Must outlive the renderer.
class Runtime final {
  public:
    Runtime() = default;
    ~Runtime();
    Runtime(const Runtime &) = delete;
    Runtime &operator=(const Runtime &) = delete;
    void initialize();
    void initializeRenderer(Renderer &renderer) const;
    GLFWwindow *window() const noexcept {
        return window_;
    }

  private:
    void initializeWindow();
    void initializeVulkan();
    void createDevice();
    bool glfwInitialized_ = false;
    GLFWwindow *window_ = nullptr;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    VkQueue presentQueue_ = VK_NULL_HANDLE;
    std::uint32_t graphicsFamily_ = 0;
    std::uint32_t presentFamily_ = 0;
};
} // namespace platform
} // namespace voxel
