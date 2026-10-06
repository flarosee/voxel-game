#include "Overlay.hpp"
#include "Theme.hpp"
#include <imgui_impl_glfw.h>
#include <imgui_impl_vulkan.h>
#include <stdexcept>

namespace voxel::ui {

Overlay::~Overlay() {
    if (!context_) return;
    ImGui::SetCurrentContext(context_);
    shutdownVulkan();
    if (platformReady_) ImGui_ImplGlfw_Shutdown();
    ImGui::DestroyContext(context_);
}

void Overlay::initialize(GLFWwindow* window) {
    window_ = window;
    IMGUI_CHECKVERSION();
    context_ = ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr; // Keep layout in memory without writing beside the executable.
    io.ConfigWindowsMoveFromTitleBarOnly = true;
    io.ConfigWindowsResizeFromEdges = false; // Use the visible bottom-right handle.
    applyTheme();
    platformReady_ = ImGui_ImplGlfw_InitForVulkan(window, true);
    if (!platformReady_) throw std::runtime_error("Cannot initialize UI window backend");
}

void Overlay::initializeVulkan(VkInstance instance, VkPhysicalDevice physical, VkDevice device,
                              unsigned queueFamily, VkQueue queue, VkRenderPass pass, unsigned imageCount) {
    ImGui_ImplVulkan_InitInfo info{};
    info.ApiVersion = VK_API_VERSION_1_0;
    info.Instance = instance;
    info.PhysicalDevice = physical;
    info.Device = device;
    info.QueueFamily = queueFamily;
    info.Queue = queue;
    info.DescriptorPoolSize = 32;
    info.MinImageCount = 2;
    info.ImageCount = imageCount;
    info.PipelineInfoMain.RenderPass = pass;
    info.PipelineInfoMain.MSAASamples = VK_SAMPLE_COUNT_1_BIT;
    info.CheckVkResultFn = [](VkResult result) {
        if (result != VK_SUCCESS) throw std::runtime_error("UI Vulkan operation failed");
    };
    vulkanReady_ = ImGui_ImplVulkan_Init(&info);
    if (!vulkanReady_) throw std::runtime_error("Cannot initialize UI renderer backend");
    if (!platformReady_) {
        platformReady_ = ImGui_ImplGlfw_InitForVulkan(window_, true);
        if (!platformReady_) throw std::runtime_error("Cannot restore UI window backend");
    }
}

void Overlay::shutdownVulkan() {
    if (!vulkanReady_) return;
    // Vulkan shutdown destroys viewport handles too. Recreate both backends
    // together on resize so GLFW never sees a cleared main-window handle.
    if (platformReady_) {
        ImGui_ImplGlfw_Shutdown();
        platformReady_ = false;
    }
    ImGui_ImplVulkan_Shutdown();
    vulkanReady_ = false;
}

void Overlay::render(VkCommandBuffer command) {
    ImGui_ImplVulkan_NewFrame();
    ImGui_ImplGlfw_NewFrame();
    ImGui::NewFrame();
    console.draw();
    ImGui::Render();
    ImGui_ImplVulkan_RenderDrawData(ImGui::GetDrawData(), command);
}

} // namespace voxel::ui
