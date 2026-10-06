#include "Types.hpp"
#include "Application.hpp"
#include "Camera.hpp"
#include "Renderer.hpp"
#include "world/World.hpp"
#include "world/ChunkCodec.hpp"
#include "world/Raycast.hpp"
#include "streaming/ChunkStreamer.hpp"
#include "world/BlockTypes.hpp"

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstring>
#include "Console.hpp"
#include <optional>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>
#include <cmath>
#include <filesystem>
#include <fstream>

namespace {

std::optional<voxel::world::RayHit> aimedBlock(const voxel::world::World& world, const voxel::Camera& camera) {
    const auto position = camera.position();
    const auto direction = camera.forward();
    const std::array<double, 3> integral{std::floor(static_cast<double>(position.x)),
        std::floor(static_cast<double>(position.y)), std::floor(static_cast<double>(position.z))};
    for (double value : integral) {
        if (!std::isfinite(value) || value < -9223372036854775808.0 || value >= 9223372036854775808.0)
            return std::nullopt;
    }
    return voxel::world::raycast(world,
        voxel::world::blockAt(camera.chunk(), {static_cast<int>(integral[0]), static_cast<int>(integral[1]), static_cast<int>(integral[2])}),
        {position.x - integral[0], position.y - integral[1], position.z - integral[2]},
        {direction.x, direction.y, direction.z}, 8.0);
}

void check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(operation) + " failed (VkResult " +
                                 std::to_string(result) + ")");
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
    check(vkEnumerateDeviceExtensionProperties(device, nullptr, &count, nullptr),
          "Count device extensions");
    std::vector<VkExtensionProperties> extensions(count);
    check(vkEnumerateDeviceExtensionProperties(device, nullptr, &count, extensions.data()),
          "Read device extensions");
    bool found = false;
    for (const auto& extension : extensions) {
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
    check(vkGetPhysicalDeviceSurfaceFormatsKHR(device, surface, &formats, nullptr),
          "Query surface formats");
    check(vkGetPhysicalDeviceSurfacePresentModesKHR(device, surface, &modes, nullptr),
          "Query present modes");
    return formats > 0 && modes > 0;
}

} // namespace

namespace voxel {

Application::~Application() {
    // The local Renderer in run() has already waited and released its resources.
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

void Application::run(bool smokeTest, UInt64 terrainSeed, bool sunlightDemo, bool blockLightDemo, bool transparencyDemo) {
    initializeWindow();
    initializeVulkan();
    Renderer renderer;
    renderer.initialize(window_, instance_, physicalDevice_, device_, surface_, graphicsQueue_,
                        presentQueue_, graphicsFamily_, presentFamily_);
    Camera camera;
    const terrain::TerrainGenerator generator(terrainSeed);
    const auto ground = generator.sampleColumn(8,12).surfaceHeight;
    const bool roofFixture = sunlightDemo || blockLightDemo || smokeTest || transparencyDemo;
    const bool lampFixture = blockLightDemo || smokeTest;
    std::int64_t roofHeight = ground + 4;
    for (int z=4;z<8;++z) for (int x=14;x<18;++x)
        roofHeight=std::max(roofHeight,generator.sampleColumn(x,z).surfaceHeight+4);
    const glm::vec3 spawn = roofFixture ? glm::vec3{20.0F,static_cast<float>(roofHeight)+(transparencyDemo || smokeTest ? 4.0F : -1.0F),13.0F}
                                       : glm::vec3{8.0F,static_cast<float>(ground)+8.0F,12.0F};
    const auto resetCamera = [&] { camera.reset(); camera.setPosition(spawn); };
    resetCamera();
    const auto initialCenter = camera.chunk();
    const auto stamp = std::to_string(std::chrono::steady_clock::now().time_since_epoch().count());
    streaming::ChunkStreamer streamer(terrainSeed, roofFixture ? std::filesystem::path("build")/("sunlight-demo-"+stamp)
                                                           : std::filesystem::path("saves/streamed"), 2);
    streamer.request(initialCenter);
    const auto smokeEdit = world::blockAt(initialCenter,{1,1,0});
    const auto title = std::string("VoxelGame | Seed ") + std::to_string(terrainSeed) + " | 1 grass - 2 lamp - 3 water - 4 glass - 5 leaves | E place - Q remove - V views";
    glfwSetWindowTitle(window_,title.c_str());
    console::info("Terrain seed: ", terrainSeed, " | Generation worker -> meshing worker -> budgeted main upload\n"
                 "RMB: look | WASD: move | Space/Ctrl: up/down | Shift: fast | R: reset | Esc: exit\n"
                 "E: place | Q: remove | G: regenerate aimed chunk | F5: export | F9: import export\n"
                 "1: grass | 2: lamp | 3: water | 4: glass | 5: leaves | V: materials / UV / normals / sunlight / block light.");
    bool previousPlace=false, previousRemove=false, previousSave=false, previousLoad=false, previousRegenerate=false, previousView=false;
    std::uint32_t debugView=0;
    world::BlockId selectedBlock=blockLightDemo ? world::blocks::Lamp : 1;
    auto previous=std::chrono::steady_clock::now();
    const auto start=previous;
    bool looking=false;
    if (smokeTest) renderer.toggleConsole(); // Exercise UI rendering and swapchain recreation.
    bool previousConsoleKey=false;
    double previousX=0,previousY=0;
    unsigned renderedFrames=0, smokeStage=0, stableFrames=0;
    bool roofPlaced=false;
    while (!glfwWindowShouldClose(window_)) {
        glfwPollEvents();
        const auto now=std::chrono::steady_clock::now();
        const float seconds=std::min(std::chrono::duration<float>(now-previous).count(),0.1F);
        previous=now;
        const bool focused=glfwGetWindowAttrib(window_,GLFW_FOCUSED)==GLFW_TRUE;
        const bool consoleKey=glfwGetKey(window_,GLFW_KEY_GRAVE_ACCENT)==GLFW_PRESS;
        if (focused && !smokeTest && consoleKey && !previousConsoleKey) renderer.toggleConsole();
        previousConsoleKey=consoleKey;
        const bool gameInput=focused && !smokeTest && !renderer.consoleVisible();
        const bool wantsLook=gameInput && glfwGetMouseButton(window_,GLFW_MOUSE_BUTTON_RIGHT)==GLFW_PRESS;
        if (wantsLook!=looking) {
            looking=wantsLook;
            glfwSetInputMode(window_,GLFW_CURSOR,looking ? GLFW_CURSOR_DISABLED : GLFW_CURSOR_NORMAL);
            if (glfwRawMouseMotionSupported()) glfwSetInputMode(window_,GLFW_RAW_MOUSE_MOTION,looking);
            glfwGetCursorPos(window_,&previousX,&previousY);
        }
        if (looking) {
            double x=0,y=0; glfwGetCursorPos(window_,&x,&y);
            camera.rotate(static_cast<float>(previousX-x)*0.0025F,static_cast<float>(previousY-y)*0.0025F);
            previousX=x; previousY=y;
        }
        if (gameInput) {
            const auto key=[this](int code){return glfwGetKey(window_,code)==GLFW_PRESS ? 1.0F : 0.0F;};
            camera.move({key(GLFW_KEY_D)-key(GLFW_KEY_A),key(GLFW_KEY_SPACE)-key(GLFW_KEY_LEFT_CONTROL),
                         key(GLFW_KEY_W)-key(GLFW_KEY_S)},seconds,key(GLFW_KEY_LEFT_SHIFT)>0);
            if (key(GLFW_KEY_R)>0) resetCamera();
            const bool place=key(GLFW_KEY_E)>0, remove=key(GLFW_KEY_Q)>0, save=key(GLFW_KEY_F5)>0,
                       load=key(GLFW_KEY_F9)>0, regenerate=key(GLFW_KEY_G)>0, changeView=key(GLFW_KEY_V)>0;
            if (changeView && !previousView) renderer.setDebugView(++debugView%5);
            if (key(GLFW_KEY_1)>0) selectedBlock=1;
            if (key(GLFW_KEY_2)>0) selectedBlock=world::blocks::Lamp;
            if (key(GLFW_KEY_3)>0) selectedBlock=world::blocks::Water;
            if (key(GLFW_KEY_4)>0) selectedBlock=world::blocks::Glass;
            if (key(GLFW_KEY_5)>0) selectedBlock=world::blocks::Leaves;
            const auto hit=aimedBlock(streamer.world(),camera);
            bool accepted=true;
            if (hit && remove && !previousRemove) accepted=streamer.edit(hit->block,hit->id,world::Air);
            if (hit && hit->adjacent && place && !previousPlace) accepted=streamer.edit(*hit->adjacent,world::Air,selectedBlock) && accepted;
            if (hit && regenerate && !previousRegenerate) accepted=streamer.regenerate(world::addressOf(hit->block).chunk) && accepted;
            if (hit && save && !previousSave) accepted=streamer.exportChunk(world::addressOf(hit->block).chunk) && accepted;
            if (load && !previousLoad) accepted=streamer.importChunk() && accepted;
            if (!accepted) console::warning("Streaming command queue full; action was not accepted.");
            previousPlace=place; previousRemove=remove; previousSave=save; previousLoad=load;
            previousRegenerate=regenerate; previousView=changeView;
        } else previousPlace=previousRemove=previousSave=previousLoad=previousRegenerate=previousView=false;
        streamer.request(camera.chunk()); // Coalesced, no job queue growth while travelling.
        if (!renderer.uploading()) {
            if (auto scene=streamer.takeReady()) {
                if (roofFixture && !roofPlaced) {
                    // Isolated test world: a roof spanning x=16, with a skylight hole.
                    for (int z=4;z<8;++z) for (int x=14;x<18;++x) {
                        if (!blockLightDemo && x==15 && z==5) continue;
                        if (!streamer.edit({x,roofHeight,z},world::Air,3)) throw std::runtime_error("Roof fixture queue full");
                    }
                    if (lampFixture && !streamer.edit({16,roofHeight-2,6},world::Air,world::blocks::Lamp))
                        throw std::runtime_error("Lamp fixture queue full");
                    if (transparencyDemo || smokeTest) {
                        // Three overlapping rows, each spanning the x=16 chunk seam.
                        for (int row=0;row<3;++row) for (int y=1;y<=2;++y) for (int x=15;x<=16;++x) {
                            const world::BlockId material=row==0 ? world::blocks::Glass : row==1 ? world::blocks::Water : world::blocks::Leaves;
                            if (!streamer.edit({x,roofHeight+y,8-row*2},world::Air,material))
                                throw std::runtime_error("Transparency fixture queue full");
                        }
                    }
                    roofPlaced=true;
                } else renderer.queueScene(std::move(scene));
            }
        }
        if (renderer.draw(camera,streamer.revision())) {
            ++renderedFrames;
            if (smokeTest && renderer.meshGeneration()==streamer.revision()) {
                ++stableFrames;
                if (stableFrames>=10 && smokeStage<10) {
                    stableFrames=0;
                    bool accepted=true;
                    switch (smokeStage++) {
                    case 0: accepted=streamer.edit(smokeEdit,streamer.world().getBlock(smokeEdit),65535); break;
                    case 1: accepted=streamer.edit(smokeEdit,65535,world::Air); glfwSetWindowSize(window_,960,540); break;
                    case 2: accepted=streamer.regenerate(initialCenter); renderer.setDebugView(1); break;
                    case 3: camera.setWorldPosition({initialCenter.x+1,initialCenter.y,initialCenter.z},{8,8,12}); break;
                    case 4: camera.setWorldPosition({INT64_C(1)<<50,initialCenter.y,-(INT64_C(1)<<50)},{8,8,12}); renderer.setDebugView(2); break;
                    case 5: resetCamera(); glfwSetWindowSize(window_,720,720); break;
                    case 6: accepted=streamer.exportChunk(initialCenter); renderer.setDebugView(3); break;
                    case 7: accepted=streamer.importChunk(); renderer.setDebugView(0); break;
                    case 8: accepted=streamer.edit({16,roofHeight-2,6},streamer.world().getBlock({16,roofHeight-2,6}),world::blocks::Lamp); renderer.setDebugView(4); break;
                    case 9: accepted=streamer.edit({16,roofHeight-2,6},world::blocks::Lamp,world::Air); renderer.setDebugView(0); break;
                    }
                    if (!accepted) throw std::runtime_error("Smoke command rejected");
                }
                if (smokeStage==10 && stableFrames>=10 && renderedFrames>=100) glfwSetWindowShouldClose(window_,GLFW_TRUE);
            }
        } else glfwWaitEventsTimeout(0.001); // Never wait for worker completion or GPU fences.
        if (smokeTest && now-start>std::chrono::seconds(12)) throw std::runtime_error("Streaming smoke test timed out");
    }
    streamer.close();
    if (smokeTest && (smokeStage!=10 || stableFrames<10)) throw std::runtime_error("Streaming smoke test interrupted");
    console::info("Presented ", renderedFrames, " frames; published scene ", renderer.meshGeneration(), ".");
}
void Application::initializeWindow() {
    glfwSetErrorCallback([](int code, const char* description) {
        console::error("GLFW ", code, ": ", description);
    });
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
    glfwSetKeyCallback(window_, [](GLFWwindow* window, int key, int, int action, int) {
        if (key == GLFW_KEY_ESCAPE && action == GLFW_PRESS) {
            glfwSetWindowShouldClose(window, GLFW_TRUE);
        }
    });
}

void Application::initializeVulkan() {
    std::uint32_t extensionCount = 0;
    const char** extensions = glfwGetRequiredInstanceExtensions(&extensionCount);
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
    check(glfwCreateWindowSurface(instance_, window_, nullptr, &surface_),
          "Create window surface");
    createDevice();
}

void Application::createDevice() {
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
        const int score = properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU ? 2 :
                          properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU ? 1 : 0;
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
    const char* extension = VK_KHR_SWAPCHAIN_EXTENSION_NAME;
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
    const std::string title = std::string("VoxelGame | Chunks | E place - Q remove - F5 save - F9 load | ") + properties.deviceName;
    glfwSetWindowTitle(window_, title.c_str());
}

} // namespace voxel
