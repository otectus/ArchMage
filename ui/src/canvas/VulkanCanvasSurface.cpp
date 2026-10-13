#include "archmage/ui/canvas/VulkanCanvasSurface.h"

#include "archmage/ui/canvas/VulkanSupport.h"

#include "present.frag.spv.h"
#include "present.vert.spv.h"

#include <QEvent>
#include <QVulkanDeviceFunctions>
#include <QVulkanFunctions>
#include <QVulkanWindow>
#include <QWidget>

#include <array>
#include <cstring>
#include <vector>

namespace archmage::ui::canvas {

namespace {

/// Zero-initialized Vulkan structure with its sType set.
template <typename T> T vkStruct(VkStructureType type) {
    T value{};
    value.sType = type;
    return value;
}

} // namespace

class VulkanCanvasRenderer;

class VulkanCanvasWindow final : public QVulkanWindow {
public:
    explicit VulkanCanvasWindow(VulkanCanvasSurface *surface) : surface_(surface) {
        setPreferredColorFormats({VK_FORMAT_B8G8R8A8_UNORM, VK_FORMAT_R8G8B8A8_UNORM});
    }

    QVulkanWindowRenderer *createRenderer() override;

    void detach() { surface_ = nullptr; }
    [[nodiscard]] VulkanCanvasSurface *surface() const { return surface_; }

protected:
    bool event(QEvent *event) override {
        if (surface_ != nullptr && CanvasSurface::isCanvasInputEvent(event) &&
            surface_->deliverInput(event)) {
            return true;
        }
        return QVulkanWindow::event(event);
    }

private:
    VulkanCanvasSurface *surface_;
};

class VulkanCanvasRenderer final : public QVulkanWindowRenderer {
public:
    explicit VulkanCanvasRenderer(VulkanCanvasWindow *window) : window_(window) {}

    void initResources() override;
    void initSwapChainResources() override;
    void releaseSwapChainResources() override;
    void releaseResources() override;
    void startNextFrame() override;
    void logicalDeviceLost() override { report(QStringLiteral("logical device lost")); }
    void physicalDeviceLost() override { report(QStringLiteral("physical device lost")); }

private:
    struct FrameSlot {
        VkImage image = VK_NULL_HANDLE;
        VkDeviceMemory imageMemory = VK_NULL_HANDLE;
        VkImageView view = VK_NULL_HANDLE;
        VkBuffer staging = VK_NULL_HANDLE;
        VkDeviceMemory stagingMemory = VK_NULL_HANDLE;
        void *mapped = nullptr;
        VkDescriptorSet descriptorSet = VK_NULL_HANDLE;
        bool initialized = false;
    };

    void report(const QString &reason) {
        if (VulkanCanvasSurface *s = window_->surface()) {
            s->reportDeviceLost(reason);
        }
    }
    uint32_t memoryType(uint32_t allowedTypes, VkMemoryPropertyFlags properties) const;
    VkShaderModule shaderModule(const uint32_t *code, size_t bytes) const;
    void createSlot(FrameSlot &slot);
    void destroySlot(FrameSlot &slot);

    VulkanCanvasWindow *window_;
    QVulkanDeviceFunctions *df_ = nullptr;
    VkSampler sampler_ = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout_ = VK_NULL_HANDLE;
    VkPipelineLayout pipelineLayout_ = VK_NULL_HANDLE;
    VkPipeline pipeline_ = VK_NULL_HANDLE;
    VkDescriptorPool pool_ = VK_NULL_HANDLE;
    std::vector<FrameSlot> slots_;
    QSize size_;
};

QVulkanWindowRenderer *VulkanCanvasWindow::createRenderer() { return new VulkanCanvasRenderer(this); }

uint32_t VulkanCanvasRenderer::memoryType(uint32_t allowedTypes, VkMemoryPropertyFlags properties) const {
    VkPhysicalDeviceMemoryProperties memory{};
    window_->vulkanInstance()->functions()->vkGetPhysicalDeviceMemoryProperties(window_->physicalDevice(),
                                                                                &memory);
    for (uint32_t i = 0; i < memory.memoryTypeCount; ++i) {
        if ((allowedTypes & (1u << i)) != 0 &&
            (memory.memoryTypes[i].propertyFlags & properties) == properties) {
            return i;
        }
    }
    qFatal("VulkanCanvasRenderer: no memory type with properties 0x%x", properties);
}

VkShaderModule VulkanCanvasRenderer::shaderModule(const uint32_t *code, size_t bytes) const {
    auto info = vkStruct<VkShaderModuleCreateInfo>(VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO);
    info.codeSize = bytes;
    info.pCode = code;
    VkShaderModule module = VK_NULL_HANDLE;
    if (df_->vkCreateShaderModule(window_->device(), &info, nullptr, &module) != VK_SUCCESS) {
        qFatal("VulkanCanvasRenderer: vkCreateShaderModule failed");
    }
    return module;
}

void VulkanCanvasRenderer::initResources() {
    VkDevice device = window_->device();
    df_ = window_->vulkanInstance()->deviceFunctions(device);

    auto sampler = vkStruct<VkSamplerCreateInfo>(VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO);
    sampler.magFilter = VK_FILTER_NEAREST;
    sampler.minFilter = VK_FILTER_NEAREST;
    sampler.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sampler.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sampler.maxAnisotropy = 1.0f;
    df_->vkCreateSampler(device, &sampler, nullptr, &sampler_);

    VkDescriptorSetLayoutBinding binding{};
    binding.binding = 1;
    binding.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    binding.descriptorCount = 1;
    binding.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
    auto layout =
        vkStruct<VkDescriptorSetLayoutCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO);
    layout.bindingCount = 1;
    layout.pBindings = &binding;
    df_->vkCreateDescriptorSetLayout(device, &layout, nullptr, &setLayout_);

    auto pipelineLayout = vkStruct<VkPipelineLayoutCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO);
    pipelineLayout.setLayoutCount = 1;
    pipelineLayout.pSetLayouts = &setLayout_;
    df_->vkCreatePipelineLayout(device, &pipelineLayout, nullptr, &pipelineLayout_);

    VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER,
                                  static_cast<uint32_t>(QVulkanWindow::MAX_CONCURRENT_FRAME_COUNT)};
    auto pool = vkStruct<VkDescriptorPoolCreateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO);
    pool.maxSets = static_cast<uint32_t>(QVulkanWindow::MAX_CONCURRENT_FRAME_COUNT);
    pool.poolSizeCount = 1;
    pool.pPoolSizes = &poolSize;
    df_->vkCreateDescriptorPool(device, &pool, nullptr, &pool_);

    const VkShaderModule vertex = shaderModule(kSpirv_present_vert, sizeof(kSpirv_present_vert));
    const VkShaderModule fragment = shaderModule(kSpirv_present_frag, sizeof(kSpirv_present_frag));
    std::array<VkPipelineShaderStageCreateInfo, 2> stages{};
    stages[0] =
        vkStruct<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
    stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
    stages[0].module = vertex;
    stages[0].pName = "main";
    stages[1] =
        vkStruct<VkPipelineShaderStageCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO);
    stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
    stages[1].module = fragment;
    stages[1].pName = "main";

    auto vertexInput = vkStruct<VkPipelineVertexInputStateCreateInfo>(
        VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO);
    auto assembly = vkStruct<VkPipelineInputAssemblyStateCreateInfo>(
        VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO);
    assembly.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
    auto viewport =
        vkStruct<VkPipelineViewportStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO);
    viewport.viewportCount = 1;
    viewport.scissorCount = 1;
    auto raster = vkStruct<VkPipelineRasterizationStateCreateInfo>(
        VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO);
    raster.polygonMode = VK_POLYGON_MODE_FILL;
    raster.cullMode = VK_CULL_MODE_NONE;
    raster.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE;
    raster.lineWidth = 1.0f;
    auto multisample = vkStruct<VkPipelineMultisampleStateCreateInfo>(
        VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO);
    multisample.rasterizationSamples = window_->sampleCountFlagBits();
    auto depth = vkStruct<VkPipelineDepthStencilStateCreateInfo>(
        VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO);
    VkPipelineColorBlendAttachmentState attachment{};
    attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT |
                                VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
    auto blend = vkStruct<VkPipelineColorBlendStateCreateInfo>(
        VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO);
    blend.attachmentCount = 1;
    blend.pAttachments = &attachment;
    const std::array<VkDynamicState, 2> dynamicStates{VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR};
    auto dynamic =
        vkStruct<VkPipelineDynamicStateCreateInfo>(VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO);
    dynamic.dynamicStateCount = static_cast<uint32_t>(dynamicStates.size());
    dynamic.pDynamicStates = dynamicStates.data();

    auto pipeline = vkStruct<VkGraphicsPipelineCreateInfo>(VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO);
    pipeline.stageCount = static_cast<uint32_t>(stages.size());
    pipeline.pStages = stages.data();
    pipeline.pVertexInputState = &vertexInput;
    pipeline.pInputAssemblyState = &assembly;
    pipeline.pViewportState = &viewport;
    pipeline.pRasterizationState = &raster;
    pipeline.pMultisampleState = &multisample;
    pipeline.pDepthStencilState = &depth;
    pipeline.pColorBlendState = &blend;
    pipeline.pDynamicState = &dynamic;
    pipeline.layout = pipelineLayout_;
    pipeline.renderPass = window_->defaultRenderPass();
    if (df_->vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &pipeline, nullptr, &pipeline_) !=
        VK_SUCCESS) {
        qFatal("VulkanCanvasRenderer: vkCreateGraphicsPipelines failed");
    }
    df_->vkDestroyShaderModule(device, vertex, nullptr);
    df_->vkDestroyShaderModule(device, fragment, nullptr);
}

void VulkanCanvasRenderer::createSlot(FrameSlot &slot) {
    VkDevice device = window_->device();
    const auto width = static_cast<uint32_t>(size_.width());
    const auto height = static_cast<uint32_t>(size_.height());

    auto image = vkStruct<VkImageCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO);
    image.imageType = VK_IMAGE_TYPE_2D;
    image.format = VK_FORMAT_R8G8B8A8_UNORM;
    image.extent = {width, height, 1};
    image.mipLevels = 1;
    image.arrayLayers = 1;
    image.samples = VK_SAMPLE_COUNT_1_BIT;
    image.tiling = VK_IMAGE_TILING_OPTIMAL;
    image.usage = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
    image.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
    df_->vkCreateImage(device, &image, nullptr, &slot.image);
    VkMemoryRequirements imageRequirements{};
    df_->vkGetImageMemoryRequirements(device, slot.image, &imageRequirements);
    auto imageAllocation = vkStruct<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
    imageAllocation.allocationSize = imageRequirements.size;
    imageAllocation.memoryTypeIndex =
        memoryType(imageRequirements.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
    df_->vkAllocateMemory(device, &imageAllocation, nullptr, &slot.imageMemory);
    df_->vkBindImageMemory(device, slot.image, slot.imageMemory, 0);

    auto view = vkStruct<VkImageViewCreateInfo>(VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO);
    view.image = slot.image;
    view.viewType = VK_IMAGE_VIEW_TYPE_2D;
    view.format = VK_FORMAT_R8G8B8A8_UNORM;
    view.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    df_->vkCreateImageView(device, &view, nullptr, &slot.view);

    auto buffer = vkStruct<VkBufferCreateInfo>(VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO);
    buffer.size = static_cast<VkDeviceSize>(width) * height * 4;
    buffer.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
    buffer.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    df_->vkCreateBuffer(device, &buffer, nullptr, &slot.staging);
    VkMemoryRequirements bufferRequirements{};
    df_->vkGetBufferMemoryRequirements(device, slot.staging, &bufferRequirements);
    auto bufferAllocation = vkStruct<VkMemoryAllocateInfo>(VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO);
    bufferAllocation.allocationSize = bufferRequirements.size;
    bufferAllocation.memoryTypeIndex =
        memoryType(bufferRequirements.memoryTypeBits,
                   VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    df_->vkAllocateMemory(device, &bufferAllocation, nullptr, &slot.stagingMemory);
    df_->vkBindBufferMemory(device, slot.staging, slot.stagingMemory, 0);
    df_->vkMapMemory(device, slot.stagingMemory, 0, VK_WHOLE_SIZE, 0, &slot.mapped);

    auto allocation = vkStruct<VkDescriptorSetAllocateInfo>(VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO);
    allocation.descriptorPool = pool_;
    allocation.descriptorSetCount = 1;
    allocation.pSetLayouts = &setLayout_;
    df_->vkAllocateDescriptorSets(device, &allocation, &slot.descriptorSet);
    VkDescriptorImageInfo imageInfo{sampler_, slot.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
    auto write = vkStruct<VkWriteDescriptorSet>(VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET);
    write.dstSet = slot.descriptorSet;
    write.dstBinding = 1;
    write.descriptorCount = 1;
    write.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
    write.pImageInfo = &imageInfo;
    df_->vkUpdateDescriptorSets(device, 1, &write, 0, nullptr);
    slot.initialized = false;
}

void VulkanCanvasRenderer::destroySlot(FrameSlot &slot) {
    VkDevice device = window_->device();
    if (slot.mapped != nullptr) {
        df_->vkUnmapMemory(device, slot.stagingMemory);
    }
    df_->vkDestroyBuffer(device, slot.staging, nullptr);
    df_->vkFreeMemory(device, slot.stagingMemory, nullptr);
    df_->vkDestroyImageView(device, slot.view, nullptr);
    df_->vkDestroyImage(device, slot.image, nullptr);
    df_->vkFreeMemory(device, slot.imageMemory, nullptr);
    slot = FrameSlot{};
}

void VulkanCanvasRenderer::initSwapChainResources() {
    size_ = window_->swapChainImageSize();
    slots_.resize(static_cast<std::size_t>(window_->concurrentFrameCount()));
    for (FrameSlot &slot : slots_) {
        createSlot(slot);
    }
}

void VulkanCanvasRenderer::releaseSwapChainResources() {
    for (FrameSlot &slot : slots_) {
        destroySlot(slot);
    }
    slots_.clear();
    df_->vkResetDescriptorPool(window_->device(), pool_, 0);
}

void VulkanCanvasRenderer::releaseResources() {
    VkDevice device = window_->device();
    df_->vkDestroyPipeline(device, pipeline_, nullptr);
    df_->vkDestroyPipelineLayout(device, pipelineLayout_, nullptr);
    df_->vkDestroyDescriptorPool(device, pool_, nullptr);
    df_->vkDestroyDescriptorSetLayout(device, setLayout_, nullptr);
    df_->vkDestroySampler(device, sampler_, nullptr);
    pipeline_ = VK_NULL_HANDLE;
    pipelineLayout_ = VK_NULL_HANDLE;
    pool_ = VK_NULL_HANDLE;
    setLayout_ = VK_NULL_HANDLE;
    sampler_ = VK_NULL_HANDLE;
}

void VulkanCanvasRenderer::startNextFrame() {
    VulkanCanvasSurface *surface = window_->surface();
    FrameSlot &slot = slots_[static_cast<std::size_t>(window_->currentFrame())];
    VkCommandBuffer cb = window_->currentCommandBuffer();
    const auto width = static_cast<uint32_t>(size_.width());
    const auto height = static_cast<uint32_t>(size_.height());

    if (surface != nullptr) {
        const QImage &frame = surface->produceFrame(size_, window_->devicePixelRatio());
        const auto rowBytes = static_cast<std::size_t>(width) * 4;
        if (static_cast<std::size_t>(frame.bytesPerLine()) == rowBytes) {
            std::memcpy(slot.mapped, frame.constBits(), rowBytes * height);
        } else {
            for (uint32_t y = 0; y < height; ++y) {
                std::memcpy(static_cast<char *>(slot.mapped) + y * rowBytes,
                            frame.constScanLine(static_cast<int>(y)), rowBytes);
            }
        }
    } else {
        std::memset(slot.mapped, 0, static_cast<std::size_t>(width) * height * 4);
    }

    auto toTransfer = vkStruct<VkImageMemoryBarrier>(VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER);
    toTransfer.srcAccessMask = slot.initialized ? VK_ACCESS_SHADER_READ_BIT : 0;
    toTransfer.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toTransfer.oldLayout =
        slot.initialized ? VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_UNDEFINED;
    toTransfer.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toTransfer.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    toTransfer.image = slot.image;
    toTransfer.subresourceRange = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
    df_->vkCmdPipelineBarrier(
        cb, slot.initialized ? VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT : VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
        VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, nullptr, 0, nullptr, 1, &toTransfer);

    VkBufferImageCopy region{};
    region.imageSubresource = {VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1};
    region.imageExtent = {width, height, 1};
    df_->vkCmdCopyBufferToImage(cb, slot.staging, slot.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1,
                                &region);

    VkImageMemoryBarrier toShader = toTransfer;
    toShader.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    toShader.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    toShader.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
    toShader.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
    df_->vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0,
                              nullptr, 0, nullptr, 1, &toShader);
    slot.initialized = true;

    std::array<VkClearValue, 3> clear{};
    clear[0].color = {{0.0f, 0.0f, 0.0f, 1.0f}};
    clear[1].depthStencil = {1.0f, 0};
    clear[2].color = clear[0].color;
    auto pass = vkStruct<VkRenderPassBeginInfo>(VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO);
    pass.renderPass = window_->defaultRenderPass();
    pass.framebuffer = window_->currentFramebuffer();
    pass.renderArea.extent = {width, height};
    pass.clearValueCount = window_->sampleCountFlagBits() > VK_SAMPLE_COUNT_1_BIT ? 3 : 2;
    pass.pClearValues = clear.data();
    df_->vkCmdBeginRenderPass(cb, &pass, VK_SUBPASS_CONTENTS_INLINE);
    const VkViewport viewport{0.0f, 0.0f, static_cast<float>(width), static_cast<float>(height), 0.0f, 1.0f};
    df_->vkCmdSetViewport(cb, 0, 1, &viewport);
    const VkRect2D scissor{{0, 0}, {width, height}};
    df_->vkCmdSetScissor(cb, 0, 1, &scissor);
    df_->vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipeline_);
    df_->vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pipelineLayout_, 0, 1,
                                 &slot.descriptorSet, 0, nullptr);
    df_->vkCmdDraw(cb, 3, 1, 0, 0);
    df_->vkCmdEndRenderPass(cb);

    window_->frameReady();
    if (surface != nullptr) {
        surface->frameSubmitted();
    }
}

std::unique_ptr<VulkanCanvasSurface> VulkanCanvasSurface::create(QString *error) {
    QVulkanInstance *instance = VulkanSupport::instance(error);
    if (instance == nullptr) {
        return nullptr;
    }
    std::unique_ptr<VulkanCanvasSurface> surface{new VulkanCanvasSurface()};
    surface->window_->setVulkanInstance(instance);
    return surface;
}

VulkanCanvasSurface::VulkanCanvasSurface() : CanvasSurface(nullptr) {
    auto *window = new VulkanCanvasWindow(this);
    window_ = window;
    QWidget *container = QWidget::createWindowContainer(window);
    container->setObjectName(QStringLiteral("archmageCanvas"));
    container->setFocusPolicy(Qt::StrongFocus);
    container->setMinimumSize(64, 64);
    container_ = container;
}

VulkanCanvasSurface::~VulkanCanvasSurface() {
    if (window_ != nullptr) {
        window_->detach();
    }
    if (container_ != nullptr && container_->parent() == nullptr) {
        delete container_.data();
    }
}

QWidget *VulkanCanvasSurface::widget() { return container_; }

QVulkanWindow *VulkanCanvasSurface::vulkanWindow() const { return window_; }

QString VulkanCanvasSurface::backendDescription() const {
    if (window_ != nullptr && window_->isValid()) {
        const VkPhysicalDeviceProperties *properties = window_->physicalDeviceProperties();
        return QStringLiteral("QVulkanWindow in a window container, Vulkan %1.%2 on %3, swapchain format %4")
            .arg(VK_API_VERSION_MAJOR(properties->apiVersion))
            .arg(VK_API_VERSION_MINOR(properties->apiVersion))
            .arg(QString::fromUtf8(properties->deviceName))
            .arg(static_cast<int>(window_->colorFormat()));
    }
    return QStringLiteral("QVulkanWindow in a window container (not yet initialized)");
}

void VulkanCanvasSurface::requestFrame() {
    if (window_ != nullptr) {
        window_->requestUpdate();
    }
}

} // namespace archmage::ui::canvas
