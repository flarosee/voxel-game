#pragma once

#include <volk.h>
#include <GLFW/glfw3.h>
#include <glm/glm.hpp>
#include <cstdint>
#include <vector>
#include <span>
#include "world/Chunk.hpp"
#include "streaming/ChunkStreamer.hpp"

namespace voxel {
class Camera;

// Main-thread-only renderer: one frame in flight, two fixed scene buffers, and
// a depth attachment per swapchain image. The application owns the Vulkan device.
class Renderer final {
public:
    Renderer() = default;
    ~Renderer();
    Renderer(const Renderer&) = delete;
    Renderer& operator=(const Renderer&) = delete;

    void initialize(GLFWwindow* window, VkPhysicalDevice physical, VkDevice device,
                    VkSurfaceKHR surface, VkQueue graphics, VkQueue present,
                    std::uint32_t graphicsFamily, std::uint32_t presentFamily);
    bool draw(const Camera& camera, std::uint64_t wantedRevision);
    void queueScene(std::shared_ptr<const streaming::Scene> scene);
    bool uploading() const noexcept { return incoming_ != nullptr; }

    void setDebugView(std::uint32_t view) noexcept { debugView_ = view % 5; }
    std::uint64_t meshGeneration() const noexcept { return meshGeneration_; }

private:
    struct Target {
        VkImageView color = VK_NULL_HANDLE;
        VkImage depth = VK_NULL_HANDLE;
        VkDeviceMemory depthMemory = VK_NULL_HANDLE;
        VkImageView depthView = VK_NULL_HANDLE;
        VkFramebuffer framebuffer = VK_NULL_HANDLE;
        VkSemaphore finished = VK_NULL_HANDLE;
    };
    void createMeshBuffers();
    void uploadStep(std::uint64_t wantedRevision);
    void createSwapchain();
    void destroySwapchain();
    void createPipeline();
    std::uint32_t memoryType(std::uint32_t bits, VkMemoryPropertyFlags flags) const;

    GLFWwindow* window_ = nullptr;
    VkPhysicalDevice physical_ = VK_NULL_HANDLE;
    VkDevice device_ = VK_NULL_HANDLE;
    VkSurfaceKHR surface_ = VK_NULL_HANDLE;
    VkQueue graphics_ = VK_NULL_HANDLE;
    VkQueue present_ = VK_NULL_HANDLE;
    std::uint32_t graphicsFamily_ = 0;
    std::uint32_t presentFamily_ = 0;
    VkSwapchainKHR swapchain_ = VK_NULL_HANDLE;
    VkExtent2D extent_{};
    int framebufferWidth_ = 0;
    int framebufferHeight_ = 0;
    bool recreate_ = false;
    VkFormat colorFormat_ = VK_FORMAT_UNDEFINED;
    VkFormat depthFormat_ = VK_FORMAT_UNDEFINED;
    std::vector<Target> targets_;
    VkRenderPass renderPass_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkPipeline cutoutPipeline_ = VK_NULL_HANDLE, blendPipeline_ = VK_NULL_HANDLE;
    struct MeshBuffer { VkBuffer buffer=VK_NULL_HANDLE; VkDeviceMemory memory=VK_NULL_HANDLE; void* mapped=nullptr; };
    std::array<MeshBuffer,2> meshBuffers_{};
    unsigned activeBuffer_ = 0;
    std::shared_ptr<const streaming::Scene> incoming_;
    std::shared_ptr<const streaming::Scene> activeScene_;
    std::size_t uploadedBytes_ = 0;
    world::ChunkCoord meshOrigin_{};
    float fogDistance_ = 0;
    std::uint32_t vertexCount_ = 0, debugView_ = 0;
    std::uint64_t meshGeneration_ = 0;
    VkCommandPool commandPool_ = VK_NULL_HANDLE;
    VkCommandBuffer command_ = VK_NULL_HANDLE;
    VkSemaphore acquired_ = VK_NULL_HANDLE;
    VkFence frameFence_ = VK_NULL_HANDLE;
};

} // namespace voxel

