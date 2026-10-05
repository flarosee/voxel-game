#pragma once

#include <volk.h>
#include <GLFW/glfw3.h>
#include <cstdint>

namespace voxel {

// Owns resources in creation order; cleanup reverses that order, including on failure.
class Application final {
public:
    Application() = default;
    ~Application();
    Application(const Application&) = delete;
    Application& operator=(const Application&) = delete;

    void run(bool smokeTest, std::uint64_t seed, bool sunlightDemo = false, bool blockLightDemo = false, bool transparencyDemo = false);

private:
    void initializeWindow();
    void initializeVulkan();
    void createDevice();

    bool glfwInitialized_ = false;
    GLFWwindow* window_ = nullptr;
    VkInstance instance_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkPhysicalDevice physicalDevice_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkQueue graphicsQueue_ = VK_NULL_HANDLE;
    VkQueue presentQueue_ = VK_NULL_HANDLE;
    std::uint32_t graphicsFamily_ = 0;
    std::uint32_t presentFamily_ = 0;
};

} // namespace voxel
