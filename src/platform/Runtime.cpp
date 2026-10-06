#include "Runtime.hpp"
#include "../Console.hpp"
#include "../Renderer.hpp"
#include <cstring>
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
void check(VkResult result, const char *operation) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(operation) + " failed (VkResult " + std::to_string(result) +
                                 ")");
    }
}

struct QueueFamilies {
    std::optional<std::uint32_t> graphics;
    std::optional<std::uint32_t> present;
};

QueueFamilies findQueues(VkPhysicalDevice device, VkSurfaceKHR surface) {
    std::uint32_t count = 0;
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, nullptr);
    std::vector<VkQueueFamilyProperties> properties(count);
    vkGetPhysicalDeviceQueueFamilyProperties(device, &count, properties.data());
    QueueFamilies queues;
    for (std::uint32_t i = 0; i < count; ++i) {
        if (properties[i].queueCount == 0) {
            continue;
        }
        VkBool32 present = VK_FALSE;
        check(vkGetPhysicalDeviceSurfaceSupportKHR(device, i, surface, &present),
              "Query presentation support");
        const bool graphics = (properties[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) != 0;
        if (graphics && present) {
            return {i, i};
        }
        if (graphics) {
            queues.graphics = i;
        }
        if (present) {
            queues.present = i;
        }
    }
    return queues;
}

bool supportsSwapchain(VkPhysicalDevice device, VkSurfaceKHR surface) {
    std::uint32_t count = 0;
    check(vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr), "Count device extensions");
    std::vector<VkExtensionProperties> extensions(count);
    check(vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data()),
          "Read device extensions");
    bool found = false;
    for (const auto &extension : extensions) {
        if (std::strcmp(extension.extensionName, VK_KHR_SWAPCHAIN_EXTENSION_NAME) == 0) {
            found = true;
            break;
        }
    }
    if (!found) {
        return false;
    }
    std::uint32_t formats = 0;
    std::uint32_t modes = 0;
    check(vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formats, nullptr), "Query surface formats");
    check(vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &modes, nullptr), "Query present modes");
    return formats > 0 && modes > 0;
}

} // namespace
namespace voxel::platform {
Runtime::~Runtime() {
    // Application destroys the renderer before releasing this device and window.
    if (device_ != VK_NULL_HANDLE) {
        vkDestroyDevice(device_, nullptr);
    }
    if (surface_ != VK_NULL_HANDLE) {
        vkDestroySurfaceKHR(instance_, surface_, nullptr);
    }
    if (instance_ != VK_NULL_HANDLE) {
        vkDestroyInstance(instance_, nullptr);
    }
    if (window_ != nullptr) {
        glfwDestroyWindow(window_);
    }
    if (glfwInitialized_) {
        glfwTerminate();
    }
}

void Runtime::initialize() {
    initializeWindow();
    initializeVulkan();
}
void Runtime::initializeRenderer(Renderer &renderer) const {
    renderer.initialize(window_, instance_, physicalDevice_, device_, surface_, graphicsQueue_, presentQueue_,
                        graphicsFamily_, presentFamily_);
}
void Runtime::initializeWindow() {
    glfwSetErrorCallback(
        [](int code, const char *description) { console::error("GLFW ", code, ": ", description); });
    if (!glfwInit()) {
        throw std::runtime_error("Cannot initialize GLFW. Check your desktop/display session.");
    }
    glfwInitialized_ = true;
    check(volkInitialize(), "Load Vulkan runtime (check your graphics driver)");
    glfwInitVulkanLoader(vkGetInstanceProcAddr);
    if (!glfwVulkanSupported()) {
        throw std::runtime_error("Vulkan is unavailable. Install a Vulkan-capable graphics driver.");
    }
    glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API);
    glfwWindowHint(GLFW_RESIZABLE, GLFW_TRUE);
    window_ = glfwCreateWindow(1280, 720, "VoxelGame | Vulkan", nullptr, nullptr);
    if (window_ == nullptr) {
        throw std::runtime_error("Cannot create the application window.");
    }
    glfwSetInputMode(window_, GLFW_STICKY_KEYS, GLFW_TRUE);
    glfwSetKeyCallback(window_, [](GLFWwindow *window, int key, int, int action, int) {
        if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
    });
}

void Runtime::initializeVulkan() {
    std::uint32_t extensionCount = 0;
    const char **extensions = glfwGetRequiredInstanceExtensions(&extensionCount);
    if (extensions == nullptr || extensionCount == 0) {
        throw std::runtime_error("No Vulkan window-system extensions are available.");
    }
    VkApplicationInfo application{};
    application.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO;
    application.pApplicationName = "VoxelGame";
    application.applicationVersion = VK_MAKE_VERSION(0, 1, 0);
    application.pEngineName = "VoxelGame";
    application.engineVersion = VK_MAKE_VERSION(0, 1, 0);
    application.apiVersion = VK_API_VERSION_1_0;

    VkInstanceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO;
    info.pApplicationInfo = &application;
    info.enabledExtensionCount = extensionCount;
    info.ppEnabledExtensionNames = extensions;
    check(vkCreateInstance(&info, nullptr, &instance_), "Create Vulkan instance");
    volkLoadInstance(instance_);
    check(glfwCreateWindowSurface(instance_, window_, nullptr, &surface_), "Create window surface");
    createDevice();
}

void Runtime::createDevice() {
    std::uint32_t count = 0;
    check(vkEnumeratePhysicalDevices(instance_, &count, nullptr), "Count graphics devices");
    if (count == 0) {
        throw std::runtime_error("No Vulkan graphics devices found.");
    }
    std::vector<VkPhysicalDevice> devices(count);
    check(vkEnumeratePhysicalDevices(instance_, &count, devices.data()), "Read graphics devices");

    QueueFamilies selectedQueues;
    int bestScore = -1;
    for (VkPhysicalDevice candidate : devices) {
        const auto queues = findQueues(candidate, surface_);
        if (!queues.graphics || !queues.present || !supportsSwapchain(candidate, surface_)) {
            continue;
        }
        VkPhysicalDeviceProperties properties{};
        vkGetPhysicalDeviceProperties(candidate, &properties);
        const int score = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU     ? 2
                          : properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 1
                                                                                            : 0;
        if (score > bestScore) {
            bestScore = score;
            physicalDevice_ = candidate;
            selectedQueues = queues;
        }
    }
    if (physicalDevice_ == VK_NULL_HANDLE) {
        throw std::runtime_error("No Vulkan device supports graphics and presentation to this window.");
    }

    const std::set<std::uint32_t> families = {*selectedQueues.graphics, *selectedQueues.present};
    graphicsFamily_ = *selectedQueues.graphics;
    presentFamily_ = *selectedQueues.present;
    const float priority = 1.0F;
    std::vector<VkDeviceQueueCreateInfo> queueInfos;
    for (const auto family : families) {
        VkDeviceQueueCreateInfo queueInfo{};
        queueInfo.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO;
        queueInfo.queueFamilyIndex = family;
        queueInfo.queueCount = 1;
        queueInfo.pQueuePriorities = &priority;
        queueInfos.push_back(queueInfo);
    }
    const char *extension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
    VkDeviceCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO;
    info.queueCreateInfoCount = static_cast<std::uint32_t>(queueInfos.size());
    info.pQueueCreateInfos = queueInfos.data();
    info.enabledExtensionCount = 1;
    info.ppEnabledExtensionNames = &extension;
    check(vkCreateDevice(physicalDevice_, &info, nullptr, &device_), "Create logical device");
    volkLoadDevice(device_);
    vkGetDeviceQueue(device_, *selectedQueues.graphics, 0, &graphicsQueue_);
    vkGetDeviceQueue(device_, *selectedQueues.present, 0, &presentQueue_);

    VkPhysicalDeviceProperties properties{};
    vkGetPhysicalDeviceProperties(physicalDevice_, &properties);
    console::info("Graphics device: ", properties.deviceName);
    const std::string title =
        std::string("VoxelGame | Chunks | E place - Q remove - F5 save - F9 load | ") + properties.deviceName;
    glfwSetWindowTitle(window_, title.c_str());
}

} // namespace voxel::platform
