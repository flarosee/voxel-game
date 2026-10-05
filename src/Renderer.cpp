#include "Renderer.hpp"
#include "Camera.hpp"

#include <glm/gtc/matrix_transform.hpp>
#include "mesh/GreedyMesher.hpp"
#include <type_traits>
#include <algorithm>
#include <array>
#include <cstddef>
#include <cstring>
#include <limits>
#include <stdexcept>
#include <string>
#include "cube_vert.hpp"
#include "cube_frag.hpp"

namespace {
void check(VkResult result, const char* operation) {
    if (result != VK_SUCCESS) {
        throw std::runtime_error(std::string(operation) + " failed (VkResult " +
                                 std::to_string(result) + ")");
    }
}

using Vertex = voxel::mesh::Vertex;
static_assert(std::is_standard_layout_v<Vertex> && sizeof(Vertex) == 44);
struct PushConstants {
    glm::mat4 mvp;
    std::uint32_t debugView;
    std::array<std::uint32_t,3> padding{};
    glm::vec4 eyeAndFog;
};
static_assert(offsetof(PushConstants, debugView) == 64 && offsetof(PushConstants, eyeAndFog) == 80 && sizeof(PushConstants) == 96);
} // namespace

namespace voxel {

Renderer::~Renderer() {
    if (!device_) return;
    vkDeviceWaitIdle(device_);
    destroySwapchain();
    for (auto& mesh : meshBuffers_) {
        if (mesh.mapped) vkUnmapMemory(device_, mesh.memory);
        vkDestroyBuffer(device_, mesh.buffer, nullptr);
        vkFreeMemory(device_, mesh.memory, nullptr);
    }
    vkDestroyFence(device_, frameFence_, nullptr);
    vkDestroySemaphore(device_, acquired_, nullptr);
    vkDestroyCommandPool(device_, commandPool_, nullptr);
}

void Renderer::initialize(GLFWwindow* window, VkPhysicalDevice physical, VkDevice device,
                          VkSurfaceKHR surface, VkQueue graphics, VkQueue present,
                          std::uint32_t graphicsFamily, std::uint32_t presentFamily) {
    window_ = window;
    physical_ = physical;
    device_ = device;
    surface_ = surface;
    graphics_ = graphics;
    present_ = present;
    graphicsFamily_ = graphicsFamily;
    presentFamily_ = presentFamily;

    for (auto format : {VK_FORMAT_D32_SFLOAT, VK_FORMAT_D24_UNORM_S8_UINT, VK_FORMAT_D16_UNORM}) {
        VkFormatProperties properties{};
        vkGetPhysicalDeviceFormatProperties(physical_, format, &properties);
        if (properties.optimalTilingFeatures & VK_FORMAT_FEATURE_DEPTH_STENCIL_ATTACHMENT_BIT) {
            depthFormat_ = format;
            break;
        }
    }
    if (depthFormat_ == VK_FORMAT_UNDEFINED) throw std::runtime_error("No supported depth format");

    VkCommandPoolCreateInfo pool{};
    pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
    pool.queueFamilyIndex = graphicsFamily_;
    check(vkCreateCommandPool(device_, &pool, nullptr, &commandPool_), "Create command pool");
    VkCommandBufferAllocateInfo allocate{};
    allocate.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
    allocate.commandPool = commandPool_;
    allocate.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
    allocate.commandBufferCount = 1;
    check(vkAllocateCommandBuffers(device_, &allocate, &command_), "Allocate command buffer");
    VkSemaphoreCreateInfo semaphore{};
    semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    check(vkCreateSemaphore(device_, &semaphore, nullptr, &acquired_), "Create acquire semaphore");
    VkFenceCreateInfo fence{};
    fence.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fence.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    check(vkCreateFence(device_, &fence, nullptr, &frameFence_), "Create frame fence");
    createMeshBuffers();
    // Swapchain creation is deferred to draw(), allowing startup while minimized.
}

std::uint32_t Renderer::memoryType(std::uint32_t bits, VkMemoryPropertyFlags flags) const {
    VkPhysicalDeviceMemoryProperties properties{};
    vkGetPhysicalDeviceMemoryProperties(physical_, &properties);
    for (std::uint32_t i = 0; i < properties.memoryTypeCount; ++i) {
        if ((bits & (1U << i)) && (properties.memoryTypes[i].propertyFlags & flags) == flags) return i;
    }
    throw std::runtime_error("No compatible GPU memory type");
}

void Renderer::createMeshBuffers() {
    // Allocate once at startup. No driver allocations on chunk transitions.
    for (auto& mesh : meshBuffers_) {
        VkBufferCreateInfo buffer{};
        buffer.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        buffer.size = streaming::MaxSceneVertices * sizeof(Vertex);
        buffer.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
        buffer.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        check(vkCreateBuffer(device_, &buffer, nullptr, &mesh.buffer), "Create streaming buffer");
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(device_, mesh.buffer, &requirements);
        VkMemoryAllocateInfo allocate{};
        allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocate.allocationSize = requirements.size;
        allocate.memoryTypeIndex = memoryType(requirements.memoryTypeBits,
            VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        check(vkAllocateMemory(device_, &allocate, nullptr, &mesh.memory), "Allocate streaming memory");
        check(vkBindBufferMemory(device_, mesh.buffer, mesh.memory, 0), "Bind streaming memory");
        check(vkMapMemory(device_, mesh.memory, 0, buffer.size, 0, &mesh.mapped), "Map streaming buffer");
    }
}
void Renderer::queueScene(std::shared_ptr<const streaming::Scene> scene) {
    if (!scene || scene->revision <= meshGeneration_) return;
    if (scene->vertices.size() > streaming::MaxSceneVertices) throw std::length_error("Mesh upload exceeds budget");
    incoming_ = std::move(scene);
    uploadedBytes_ = 0;
}
void Renderer::uploadStep(std::uint64_t wantedRevision) {
    if (incoming_ && incoming_->revision != wantedRevision) incoming_.reset();
    if (!incoming_) return;
    const auto total = incoming_->vertices.size() * sizeof(Vertex);
    const auto bytes = std::min<std::size_t>(512 * 1024, total - uploadedBytes_);
    if (bytes) std::memcpy(static_cast<std::byte*>(meshBuffers_[1-activeBuffer_].mapped) + uploadedBytes_,
        reinterpret_cast<const std::byte*>(incoming_->vertices.data()) + uploadedBytes_, bytes);
    uploadedBytes_ += bytes;
    if (uploadedBytes_ != total) return;
    // Previous frame is complete. Publish the entire seam-consistent batch together.
    activeBuffer_ = 1-activeBuffer_;
    vertexCount_ = static_cast<std::uint32_t>(incoming_->vertices.size());
    meshOrigin_ = incoming_->origin;
    fogDistance_ = incoming_->fogDistance;
    meshGeneration_ = incoming_->revision;
    activeScene_ = std::move(incoming_);
}
void Renderer::destroySwapchain() {
    for (auto& target : targets_) {
        vkDestroyFramebuffer(device_, target.framebuffer, nullptr);
        vkDestroyImageView(device_, target.depthView, nullptr);
        vkDestroyImage(device_, target.depth, nullptr);
        vkFreeMemory(device_, target.depthMemory, nullptr);
        vkDestroyImageView(device_, target.color, nullptr);
        vkDestroySemaphore(device_, target.finished, nullptr);
    }
    targets_.clear();
    vkDestroyPipeline(device_, pipeline_, nullptr);
    vkDestroyPipeline(device_, cutoutPipeline_, nullptr);
    vkDestroyPipeline(device_, blendPipeline_, nullptr);
    vkDestroyPipelineLayout(device_, pipelineLayout_, nullptr);
    vkDestroyRenderPass(device_, renderPass_, nullptr);
    vkDestroySwapchainKHR(device_, swapchain_, nullptr);
    pipeline_ = VK_NULL_HANDLE;
    cutoutPipeline_ = blendPipeline_ = VK_NULL_HANDLE;
    pipelineLayout_ = VK_NULL_HANDLE;
    renderPass_ = VK_NULL_HANDLE;
    swapchain_ = VK_NULL_HANDLE;
}

void Renderer::createSwapchain() {
    check(vkDeviceWaitIdle(device_), "Wait before swapchain recreation");
    destroySwapchain();
    VkSurfaceCapabilitiesKHR capabilities{};
    check(vkGetPhysicalDeviceSurfaceCapabilitiesKHR(physical_, surface_, &capabilities), "Query surface");
    std::uint32_t count = 0;
    check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &count, nullptr), "Count surface formats");
    if (count == 0) throw std::runtime_error("Surface has no formats");
    std::vector<VkSurfaceFormatKHR> formats(count);
    check(vkGetPhysicalDeviceSurfaceFormatsKHR(physical_, surface_, &count, formats.data()), "Read surface formats");
    auto format = formats.front();
    if (format.format == VK_FORMAT_UNDEFINED) format.format = VK_FORMAT_B8G8R8A8_SRGB;
    for (const auto& candidate : formats) {
        if (candidate.format == VK_FORMAT_B8G8R8A8_SRGB && candidate.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
            format = candidate;
            break;
        }
    }
    colorFormat_ = format.format;
    glfwGetFramebufferSize(window_, &framebufferWidth_, &framebufferHeight_);
    if (capabilities.currentExtent.width != std::numeric_limits<std::uint32_t>::max()) {
        extent_ = capabilities.currentExtent;
    } else {
        extent_.width = std::clamp(static_cast<std::uint32_t>(framebufferWidth_),
                                  capabilities.minImageExtent.width, capabilities.maxImageExtent.width);
        extent_.height = std::clamp(static_cast<std::uint32_t>(framebufferHeight_),
                                   capabilities.minImageExtent.height, capabilities.maxImageExtent.height);
    }
    std::uint32_t imageCount = capabilities.minImageCount + 1;
    if (capabilities.maxImageCount > 0) imageCount = std::min(imageCount, capabilities.maxImageCount);
    VkCompositeAlphaFlagBitsKHR alpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
    for (auto value : {VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR, VK_COMPOSITE_ALPHA_PRE_MULTIPLIED_BIT_KHR,
                       VK_COMPOSITE_ALPHA_POST_MULTIPLIED_BIT_KHR, VK_COMPOSITE_ALPHA_INHERIT_BIT_KHR}) {
        if (capabilities.supportedCompositeAlpha & value) { alpha = value; break; }
    }
    if (!(capabilities.supportedUsageFlags & VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT))
        throw std::runtime_error("Surface cannot be a color attachment");
    const std::array families = {graphicsFamily_, presentFamily_};
    VkSwapchainCreateInfoKHR swapchain{};
    swapchain.sType = VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR;
    swapchain.surface = surface_;
    swapchain.minImageCount = imageCount;
    swapchain.imageFormat = colorFormat_;
    swapchain.imageColorSpace = format.colorSpace;
    swapchain.imageExtent = extent_;
    swapchain.imageArrayLayers = 1;
    swapchain.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
    swapchain.imageSharingMode = graphicsFamily_ == presentFamily_ ? VK_SHARING_MODE_EXCLUSIVE : VK_SHARING_MODE_CONCURRENT;
    if (graphicsFamily_ != presentFamily_) {
        swapchain.queueFamilyIndexCount = static_cast<std::uint32_t>(families.size());
        swapchain.pQueueFamilyIndices = families.data();
    }
    swapchain.preTransform = capabilities.currentTransform;
    swapchain.compositeAlpha = alpha;
    swapchain.presentMode = VK_PRESENT_MODE_FIFO_KHR; // Guaranteed, vsync-paced presentation.
    swapchain.clipped = VK_TRUE;
    check(vkCreateSwapchainKHR(device_, &swapchain, nullptr, &swapchain_), "Create swapchain");
    check(vkGetSwapchainImagesKHR(device_, swapchain_, &count, nullptr), "Count swapchain images");
    std::vector<VkImage> images(count);
    check(vkGetSwapchainImagesKHR(device_, swapchain_, &count, images.data()), "Read swapchain images");

    std::array<VkAttachmentDescription, 2> attachments{};
    attachments[0].format = colorFormat_;
    attachments[0].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[0].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[0].storeOp = VK_ATTACHMENT_STORE_OP_STORE;
    attachments[0].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[0].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[0].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[0].finalLayout = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;
    attachments[1].format = depthFormat_;
    attachments[1].samples = VK_SAMPLE_COUNT_1_BIT;
    attachments[1].loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR;
    attachments[1].storeOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    attachments[1].stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    attachments[1].initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    attachments[1].finalLayout = VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL;
    VkAttachmentReference color{0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
    VkAttachmentReference depth{1, VK_IMAGE_LAYOUT_DEPTH_STENCIL_ATTACHMENT_OPTIMAL};
    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments = &color;
    subpass.pDepthStencilAttachment = &depth;
    VkSubpassDependency dependency{};
    dependency.srcSubpass = VK_SUBPASS_EXTERNAL;
    dependency.dstSubpass = 0;
    dependency.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT | VK_PIPELINE_STAGE_EARLY_FRAGMENT_TESTS_BIT;
    dependency.dstStageMask = dependency.srcStageMask;
    dependency.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT;
    VkRenderPassCreateInfo pass{};
    pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    pass.attachmentCount = static_cast<std::uint32_t>(attachments.size());
    pass.pAttachments = attachments.data();
    pass.subpassCount = 1;
    pass.pSubpasses = &subpass;
    pass.dependencyCount = 1;
    pass.pDependencies = &dependency;
    check(vkCreateRenderPass(device_, &pass, nullptr, &renderPass_), "Create render pass");
    createPipeline();

    targets_.resize(images.size());
    for (std::size_t i = 0; i < images.size(); ++i) {
        auto& target = targets_[i];
        VkImageViewCreateInfo view{};
        view.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        view.image = images[i];
        view.viewType = VK_IMAGE_VIEW_TYPE_2D;
        view.format = colorFormat_;
        view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
        check(vkCreateImageView(device_, &view, nullptr, &target.color), "Create color view");
        VkImageCreateInfo image{};
        image.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        image.imageType = VK_IMAGE_TYPE_2D;
        image.format = depthFormat_;
        image.extent = {extent_.width, extent_.height, 1};
        image.mipLevels = 1;
        image.arrayLayers = 1;
        image.samples = VK_SAMPLE_COUNT_1_BIT;
        image.tiling = VK_IMAGE_TILING_OPTIMAL;
        image.usage = VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
        image.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        check(vkCreateImage(device_, &image, nullptr, &target.depth), "Create depth image");
        VkMemoryRequirements requirements{};
        vkGetImageMemoryRequirements(device_, target.depth, &requirements);
        VkMemoryAllocateInfo allocate{};
        allocate.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocate.allocationSize = requirements.size;
        allocate.memoryTypeIndex = memoryType(requirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
        check(vkAllocateMemory(device_, &allocate, nullptr, &target.depthMemory), "Allocate depth memory");
        check(vkBindImageMemory(device_, target.depth, target.depthMemory, 0), "Bind depth memory");
        view.image = target.depth;
        view.format = depthFormat_;
        view.subresourceRange.aspectMask = VK_IMAGE_ASPECT_DEPTH_BIT;
        check(vkCreateImageView(device_, &view, nullptr, &target.depthView), "Create depth view");
        const std::array views = {target.color, target.depthView};
        VkFramebufferCreateInfo framebuffer{};
        framebuffer.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        framebuffer.renderPass = renderPass_;
        framebuffer.attachmentCount = static_cast<std::uint32_t>(views.size());
        framebuffer.pAttachments = views.data();
        framebuffer.width = extent_.width;
        framebuffer.height = extent_.height;
        framebuffer.layers = 1;
        check(vkCreateFramebuffer(device_, &framebuffer, nullptr, &target.framebuffer), "Create framebuffer");
        // Present semaphores belong to images, not frame fences: a fence only covers rendering.
        VkSemaphoreCreateInfo semaphore{};
        semaphore.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        check(vkCreateSemaphore(device_, &semaphore, nullptr, &target.finished), "Create present semaphore");
    }
    recreate_ = false;
}

void Renderer::createPipeline() {
    VkPushConstantRange transform{VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(PushConstants)};
    VkPipelineLayoutCreateInfo layout{};
    layout.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    layout.pushConstantRangeCount = 1;
    layout.pPushConstantRanges = &transform;
    check(vkCreatePipelineLayout(device_, &layout, nullptr, &pipelineLayout_), "Create pipeline layout");
    VkShaderModule vertex = VK_NULL_HANDLE;
    VkShaderModule fragment = VK_NULL_HANDLE;
    try {
        VkShaderModuleCreateInfo module{};
        module.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        module.codeSize = sizeof(cube_vert);
        module.pCode = cube_vert;
        check(vkCreateShaderModule(device_, &module, nullptr, &vertex), "Create vertex shader");
        module.codeSize = sizeof(cube_frag);
        module.pCode = cube_frag;
        check(vkCreateShaderModule(device_, &module, nullptr, &fragment), "Create fragment shader");
        std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
        for (auto& stage : stages) {
            stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
            stage.pName = "main";
        }
        stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
        stages[0].module = vertex;
        stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
        stages[1].module = fragment;
        VkVertexInputBindingDescription binding{0, sizeof(Vertex), VK_VERTEX_INPUT_RATE_VERTEX};
        const std::array<VkVertexInputAttributeDescription, 6> attributes = {{
            {0, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, position)},
            {1, 0, VK_FORMAT_R32G32B32_SFLOAT, offsetof(Vertex, normal)},
            {2, 0, VK_FORMAT_R32G32_SFLOAT, offsetof(Vertex, uv)},
            {3, 0, VK_FORMAT_R32_UINT, offsetof(Vertex, material)},
            {4, 0, VK_FORMAT_R32_UINT, offsetof(Vertex, sunlight)},
            {5, 0, VK_FORMAT_R32_UINT, offsetof(Vertex, blockLight)}
        }};
        VkPipelineVertexInputStateCreateInfo input{};
        input.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
        input.vertexBindingDescriptionCount = 1;
        input.pVertexBindingDescriptions = &binding;
        input.vertexAttributeDescriptionCount = static_cast<std::uint32_t>(attributes.size());
        input.pVertexAttributeDescriptions = attributes.data();
        VkPipelineInputAssemblyStateCreateInfo assembly{};
        assembly.sType = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
        assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
        VkPipelineViewportStateCreateInfo viewport{};
        viewport.sType = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
        viewport.viewportCount = 1;
        viewport.scissorCount = 1;
        VkPipelineRasterizationStateCreateInfo raster{};
        raster.sType = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
        raster.polygonMode = VK_POLYGON_MODE_FILL;
        raster.cullMode = VK_CULL_MODE_BACK_BIT;
        raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
        raster.lineWidth = 1.0F;
        VkPipelineMultisampleStateCreateInfo samples{};
        samples.sType = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
        samples.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
        VkPipelineDepthStencilStateCreateInfo depth{};
        depth.sType = VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO;
        depth.depthTestEnable = VK_TRUE;
        depth.depthWriteEnable = VK_TRUE;
        depth.depthCompareOp = VK_COMPARE_OP_LESS;
        VkPipelineColorBlendAttachmentState color{};
        color.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                               VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
        VkPipelineColorBlendStateCreateInfo blend{};
        blend.sType = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
        blend.attachmentCount = 1;
        blend.pAttachments = &color;
        const std::array dynamicStates = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
        VkPipelineDynamicStateCreateInfo dynamic{};
        dynamic.sType = VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO;
        dynamic.dynamicStateCount = static_cast<std::uint32_t>(dynamicStates.size());
        dynamic.pDynamicStates = dynamicStates.data();
        VkGraphicsPipelineCreateInfo pipeline{};
        pipeline.sType = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
        pipeline.stageCount = static_cast<std::uint32_t>(stages.size());
        pipeline.pStages = stages.data();
        pipeline.pVertexInputState = &input;
        pipeline.pInputAssemblyState = &assembly;
        pipeline.pViewportState = &viewport;
        pipeline.pRasterizationState = &raster;
        pipeline.pMultisampleState = &samples;
        pipeline.pDepthStencilState = &depth;
        pipeline.pColorBlendState = &blend;
        pipeline.pDynamicState = &dynamic;
        pipeline.layout = pipelineLayout_;
        pipeline.renderPass = renderPass_;
        check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipeline, nullptr, &pipeline_), "Create graphics pipeline");
        raster.cullMode = VK_CULL_MODE_NONE; // Leaves and blended surfaces have visible interiors.
        check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipeline, nullptr, &cutoutPipeline_), "Create cutout pipeline");
        depth.depthWriteEnable = VK_FALSE;
        color.blendEnable = VK_TRUE;
        color.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
        color.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        color.colorBlendOp = VK_BLEND_OP_ADD;
        color.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
        color.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
        color.alphaBlendOp = VK_BLEND_OP_ADD;
        check(vkCreateGraphicsPipelines(device_, VK_NULL_HANDLE, 1, &pipeline, nullptr, &blendPipeline_), "Create transparent pipeline");
    } catch (...) {
        vkDestroyShaderModule(device_, fragment, nullptr);
        vkDestroyShaderModule(device_, vertex, nullptr);
        throw;
    }
    vkDestroyShaderModule(device_, fragment, nullptr);
    vkDestroyShaderModule(device_, vertex, nullptr);
}

bool Renderer::draw(const Camera& camera, std::uint64_t wantedRevision) {
    int width = 0, height = 0;
    glfwGetFramebufferSize(window_, &width, &height);
    if (width == 0 || height == 0) return false;
    if (!swapchain_ || recreate_ || width != framebufferWidth_ || height != framebufferHeight_) createSwapchain();
    const auto fenceStatus = vkGetFenceStatus(device_, frameFence_);
    if (fenceStatus == VK_NOT_READY) return false;
    check(fenceStatus, "Check frame fence");
    uploadStep(wantedRevision);
    std::uint32_t imageIndex = 0;
    const auto acquire = vkAcquireNextImageKHR(device_, swapchain_, 0, acquired_, VK_NULL_HANDLE, &imageIndex);
    if (acquire == VK_NOT_READY || acquire == VK_TIMEOUT) return false;
    if (acquire == VK_ERROR_OUT_OF_DATE_KHR) { recreate_ = true; return false; }
    if (acquire != VK_SUBOPTIMAL_KHR) check(acquire, "Acquire image");
    auto& target = targets_[imageIndex];
    check(vkResetCommandBuffer(command_, 0), "Reset commands");
    VkCommandBufferBeginInfo begin{};
    begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
    begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
    check(vkBeginCommandBuffer(command_, &begin), "Begin commands");
    std::array<VkClearValue, 2> clear{};
    clear[0].color = {{0.018F, 0.027F, 0.045F, 1.0F}};
    clear[1].depthStencil = {1.0F, 0};
    VkRenderPassBeginInfo pass{};
    pass.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
    pass.renderPass = renderPass_;
    pass.framebuffer = target.framebuffer;
    pass.renderArea.extent = extent_;
    pass.clearValueCount = static_cast<std::uint32_t>(clear.size());
    pass.pClearValues = clear.data();
    vkCmdBeginRenderPass(command_, &pass, VK_SUBPASS_CONTENTS_INLINE);
    vkCmdBindPipeline(command_, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    VkViewport viewport{0, 0, static_cast<float>(extent_.width), static_cast<float>(extent_.height), 0, 1};
    VkRect2D scissor{{0, 0}, extent_};
    vkCmdSetViewport(command_, 0, 1, &viewport);
    vkCmdSetScissor(command_, 0, 1, &scissor);
    auto projection = glm::perspectiveRH_ZO(glm::radians(60.0F),
        static_cast<float>(extent_.width) / static_cast<float>(extent_.height), 0.05F, 500.0F);
    projection[1][1] *= -1.0F; // Vulkan framebuffer Y is down; depth is already [0, 1].
    if (vertexCount_ != 0) {
        const VkDeviceSize offset = 0;
        vkCmdBindVertexBuffers(command_, 0, 1, &meshBuffers_[activeBuffer_].buffer, &offset);
        const auto origin = camera.chunk();
        const auto eye = camera.position() + glm::vec3(static_cast<float>(origin.x-meshOrigin_.x)*16.0F,
            static_cast<float>(origin.y-meshOrigin_.y)*16.0F,static_cast<float>(origin.z-meshOrigin_.z)*16.0F);
        const PushConstants constants{projection * camera.viewRelativeTo(meshOrigin_), debugView_, {}, {eye,fogDistance_}};
        vkCmdPushConstants(command_, pipelineLayout_, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
                           0, sizeof(constants), &constants);
        vkCmdDraw(command_, activeScene_->opaqueCount, 1, 0, 0);
        vkCmdBindPipeline(command_, VK_PIPELINE_BIND_POINT_GRAPHICS, cutoutPipeline_);
        vkCmdDraw(command_, activeScene_->cutoutCount, 1, activeScene_->opaqueCount, 0);
        vkCmdBindPipeline(command_, VK_PIPELINE_BIND_POINT_GRAPHICS, blendPipeline_);
        for (const auto range:activeScene_->transparency.backToFront({eye.x,eye.y,eye.z}))
            vkCmdDraw(command_, range.count, 1, range.first, 0);
    }
    vkCmdEndRenderPass(command_);
    check(vkEndCommandBuffer(command_), "End commands");
    const VkPipelineStageFlags waitStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    VkSubmitInfo submit{};
    submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
    submit.waitSemaphoreCount = 1;
    submit.pWaitSemaphores = &acquired_;
    submit.pWaitDstStageMask = &waitStage;
    submit.commandBufferCount = 1;
    submit.pCommandBuffers = &command_;
    submit.signalSemaphoreCount = 1;
    submit.pSignalSemaphores = &target.finished;
    // Reset only after a successful acquire, avoiding deadlock on an out-of-date image.
    check(vkResetFences(device_, 1, &frameFence_), "Reset frame fence");
    check(vkQueueSubmit(graphics_, 1, &submit, frameFence_), "Submit frame");
    VkPresentInfoKHR present{};
    present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
    present.waitSemaphoreCount = 1;
    present.pWaitSemaphores = &target.finished;
    present.swapchainCount = 1;
    present.pSwapchains = &swapchain_;
    present.pImageIndices = &imageIndex;
    const auto result = vkQueuePresentKHR(present_, &present);
    if (result != VK_ERROR_OUT_OF_DATE_KHR && result != VK_SUBOPTIMAL_KHR) check(result, "Present frame");
    if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || acquire == VK_SUBOPTIMAL_KHR) {
        recreate_ = true;
    }
    return true;
}

} // namespace voxel


