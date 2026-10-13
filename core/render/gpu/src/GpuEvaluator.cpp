#include "archmage/gpu/GpuEvaluator.h"

#include "GpuImpl.h"
#include "source_over.comp.spv.h"

#include "archmage/render/Graph.h"
#include "archmage/tiles/ResourceCoordinator.h"

#include <algorithm>
#include <bit>
#include <cstring>
#include <optional>

namespace archmage::gpu {
namespace {

using tiles::kTilePixels;

constexpr std::size_t kBlockFloats = kTilePixels * 4;        // one tile of straight RGBA
constexpr std::size_t kMaxBlocksPerBatch = 256;              // 64 MiB of samples per submission
constexpr std::size_t kMaxTilesPerBatch = 256;               // 64 MiB of results
constexpr std::uint64_t kFenceTimeoutNs = 30'000'000'000ull; // 30 s: anything slower is a lost device

struct Source {
    const document::Layer *layer = nullptr;
    tiles::SampleType type = tiles::SampleType::UInt8;
    base::IRect bounds;
};

// The layer sources of a composite of layer sources, or of one layer source; nullopt for any other graph.
std::optional<std::vector<Source>> layerSources(const render::GraphNode &root) {
    std::vector<Source> sources;
    const auto add = [&](const render::GraphNode &node) {
        const auto *layer = dynamic_cast<const render::LayerSourceNode *>(&node);
        if (layer == nullptr) {
            return false;
        }
        sources.push_back({layer->layer().get(), layer->sampleType(), layer->bounds()});
        return true;
    };
    if (dynamic_cast<const render::CompositeNode *>(&root) != nullptr) {
        for (const auto &input : root.inputs()) {
            if (!add(*input)) {
                return std::nullopt;
            }
        }
        return sources;
    }
    if (add(root)) {
        return sources;
    }
    return std::nullopt;
}

// Straight RGBA samples of a layer over one document tile, converted to float exactly as LayerSourceNode
// converts them; pixels without content stay transparent and are skipped by the shader as by the CPU.
base::Status gather(const Source &source, tiles::TileCoord coord, float *out) {
    std::fill(out, out + kBlockFloats, 0.0f);
    const document::Layer &layer = *source.layer;
    const base::IRect target = tiles::tilePixelRect(coord);
    const base::IRect local = target.translated(-layer.offset.x, -layer.offset.y);
    const tiles::TileMap &map = layer.content->tiles;
    const tiles::ChannelLayout &layout = map.layout();
    const std::size_t planes[4] = {
        *layout.planeOf(tiles::ChannelSemantic::Red), *layout.planeOf(tiles::ChannelSemantic::Green),
        *layout.planeOf(tiles::ChannelSemantic::Blue), *layout.planeOf(tiles::ChannelSemantic::Alpha)};
    const std::size_t bytes = tiles::bytesPerSample(source.type);
    const base::IRect range = tiles::tileRangeCovering(local);
    for (std::int64_t ty = range.y0; ty < range.y1; ++ty) {
        for (std::int64_t tx = range.x0; tx < range.x1; ++tx) {
            const tiles::TileCoord layerTile{tx, ty};
            const auto tile = map.find(layerTile);
            if (!tile) {
                continue;
            }
            auto pinned = tile->pin();
            if (!pinned) {
                return base::unexpected(std::move(pinned).error());
            }
            const base::IRect tileRect = tiles::tilePixelRect(layerTile);
            const base::IRect part = tileRect.intersected(local);
            for (std::int64_t y = part.y0; y < part.y1; ++y) {
                for (std::int64_t x = part.x0; x < part.x1; ++x) {
                    const auto inTile = static_cast<std::size_t>((y - tileRect.y0) * tiles::kTileSize + (x - tileRect.x0));
                    const auto outIndex = static_cast<std::size_t>(
                        ((y + layer.offset.y - target.y0) * tiles::kTileSize + (x + layer.offset.x - target.x0)) * 4);
                    for (std::size_t c = 0; c < 4; ++c) {
                        const std::byte *sample = tile->isUniform() ? tile->uniformSample(planes[c]).data()
                                                                    : tile->plane(planes[c]).data() + inTile * bytes;
                        out[outIndex + c] = tiles::loadSample(source.type, sample);
                    }
                }
            }
        }
    }
    return {};
}

base::Unexpected<base::Error> vulkanFailure(const char *what, VkResult result) {
    if (result == VK_ERROR_DEVICE_LOST) {
        return base::fail(base::ErrorCategory::ProviderUnavailable, "gpu.device_lost", "The GPU stopped responding",
                          what);
    }
    if (result == VK_ERROR_OUT_OF_DEVICE_MEMORY || result == VK_ERROR_OUT_OF_HOST_MEMORY) {
        return base::fail(base::ErrorCategory::ResourceExhausted, "gpu.out_of_memory", "The GPU ran out of memory",
                          std::string(what) + ": " + std::to_string(result));
    }
    return base::fail(base::ErrorCategory::ProviderUnavailable, "gpu.failed", "GPU evaluation failed",
                      std::string(what) + ": " + std::to_string(result));
}

} // namespace

// The compute pipeline and its buffers. Buffers are host-visible and coherent: the slice exists to compare
// results, not to be fast (ADR-0012).
struct GpuEvaluator::Pipeline {
    struct Buffer {
        VkBuffer buffer = VK_NULL_HANDLE;
        VkDeviceMemory memory = VK_NULL_HANDLE;
        void *mapped = nullptr;
        VkDeviceSize size = 0;
    };

    explicit Pipeline(GpuContext::Impl &impl) : gpu(impl) {}
    Pipeline(const Pipeline &) = delete;
    Pipeline &operator=(const Pipeline &) = delete;

    ~Pipeline() {
        VkDevice device = gpu.device;
        vkDeviceWaitIdle(device);
        for (Buffer *buffer : {&samples, &entries, &result}) {
            release(*buffer);
        }
        if (fence != VK_NULL_HANDLE) {
            vkDestroyFence(device, fence, nullptr);
        }
        if (commands != VK_NULL_HANDLE) {
            vkFreeCommandBuffers(device, gpu.commandPool, 1, &commands);
        }
        if (pool != VK_NULL_HANDLE) {
            vkDestroyDescriptorPool(device, pool, nullptr);
        }
        if (pipeline != VK_NULL_HANDLE) {
            vkDestroyPipeline(device, pipeline, nullptr);
        }
        if (layout != VK_NULL_HANDLE) {
            vkDestroyPipelineLayout(device, layout, nullptr);
        }
        if (setLayout != VK_NULL_HANDLE) {
            vkDestroyDescriptorSetLayout(device, setLayout, nullptr);
        }
        if (shader != VK_NULL_HANDLE) {
            vkDestroyShaderModule(device, shader, nullptr);
        }
    }

    base::Status create() {
        VkDevice device = gpu.device;
        VkShaderModuleCreateInfo shaderInfo{};
        shaderInfo.sType = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
        shaderInfo.codeSize = sizeof(kSpirv_source_over_comp);
        shaderInfo.pCode = kSpirv_source_over_comp;
        if (VkResult r = vkCreateShaderModule(device, &shaderInfo, nullptr, &shader); r != VK_SUCCESS) {
            return vulkanFailure("vkCreateShaderModule", r);
        }
        VkDescriptorSetLayoutBinding bindings[3]{};
        for (std::uint32_t i = 0; i < 3; ++i) {
            bindings[i].binding = i;
            bindings[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            bindings[i].descriptorCount = 1;
            bindings[i].stageFlags = VK_SHADER_STAGE_COMPUTE_BIT;
        }
        VkDescriptorSetLayoutCreateInfo setInfo{};
        setInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        setInfo.bindingCount = 3;
        setInfo.pBindings = bindings;
        if (VkResult r = vkCreateDescriptorSetLayout(device, &setInfo, nullptr, &setLayout); r != VK_SUCCESS) {
            return vulkanFailure("vkCreateDescriptorSetLayout", r);
        }
        VkPipelineLayoutCreateInfo layoutInfo{};
        layoutInfo.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        layoutInfo.setLayoutCount = 1;
        layoutInfo.pSetLayouts = &setLayout;
        if (VkResult r = vkCreatePipelineLayout(device, &layoutInfo, nullptr, &layout); r != VK_SUCCESS) {
            return vulkanFailure("vkCreatePipelineLayout", r);
        }
        VkComputePipelineCreateInfo pipelineInfo{};
        pipelineInfo.sType = VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO;
        pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
        pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
        pipelineInfo.stage.module = shader;
        pipelineInfo.stage.pName = "main";
        pipelineInfo.layout = layout;
        if (VkResult r = vkCreateComputePipelines(device, VK_NULL_HANDLE, 1, &pipelineInfo, nullptr, &pipeline);
            r != VK_SUCCESS) {
            return vulkanFailure("vkCreateComputePipelines", r);
        }
        VkDescriptorPoolSize poolSize{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 3};
        VkDescriptorPoolCreateInfo poolInfo{};
        poolInfo.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        poolInfo.maxSets = 1;
        poolInfo.poolSizeCount = 1;
        poolInfo.pPoolSizes = &poolSize;
        if (VkResult r = vkCreateDescriptorPool(device, &poolInfo, nullptr, &pool); r != VK_SUCCESS) {
            return vulkanFailure("vkCreateDescriptorPool", r);
        }
        VkDescriptorSetAllocateInfo allocateSet{};
        allocateSet.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        allocateSet.descriptorPool = pool;
        allocateSet.descriptorSetCount = 1;
        allocateSet.pSetLayouts = &setLayout;
        if (VkResult r = vkAllocateDescriptorSets(device, &allocateSet, &set); r != VK_SUCCESS) {
            return vulkanFailure("vkAllocateDescriptorSets", r);
        }
        VkCommandBufferAllocateInfo allocateCommands{};
        allocateCommands.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        allocateCommands.commandPool = gpu.commandPool;
        allocateCommands.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        allocateCommands.commandBufferCount = 1;
        if (VkResult r = vkAllocateCommandBuffers(device, &allocateCommands, &commands); r != VK_SUCCESS) {
            return vulkanFailure("vkAllocateCommandBuffers", r);
        }
        VkFenceCreateInfo fenceInfo{};
        fenceInfo.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        if (VkResult r = vkCreateFence(device, &fenceInfo, nullptr, &fence); r != VK_SUCCESS) {
            return vulkanFailure("vkCreateFence", r);
        }
        return {};
    }

    void release(Buffer &buffer) {
        if (buffer.mapped != nullptr) {
            vkUnmapMemory(gpu.device, buffer.memory);
        }
        if (buffer.buffer != VK_NULL_HANDLE) {
            vkDestroyBuffer(gpu.device, buffer.buffer, nullptr);
        }
        if (buffer.memory != VK_NULL_HANDLE) {
            vkFreeMemory(gpu.device, buffer.memory, nullptr);
            tiles::ResourceCoordinator::global().release(tiles::Budget::Vram, buffer.size);
        }
        buffer = Buffer{};
    }

    // Host-visible, coherent storage buffer of at least `size` bytes, charged to the VRAM budget.
    base::Status ensure(Buffer &buffer, VkDeviceSize size) {
        if (buffer.size >= size) {
            return {};
        }
        release(buffer);
        if (!tiles::ResourceCoordinator::global().makeRoom(tiles::Budget::Vram, size)) {
            return base::fail(base::ErrorCategory::ResourceExhausted, "gpu.budget",
                              "The GPU memory budget is exhausted", std::to_string(size) + " bytes");
        }
        VkBufferCreateInfo bufferInfo{};
        bufferInfo.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bufferInfo.size = size;
        bufferInfo.usage = VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
        bufferInfo.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        if (VkResult r = vkCreateBuffer(gpu.device, &bufferInfo, nullptr, &buffer.buffer); r != VK_SUCCESS) {
            return vulkanFailure("vkCreateBuffer", r);
        }
        VkMemoryRequirements requirements{};
        vkGetBufferMemoryRequirements(gpu.device, buffer.buffer, &requirements);
        const std::uint32_t type = gpu.memoryType(
            requirements.memoryTypeBits, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
        if (type == UINT32_MAX) {
            return base::fail(base::ErrorCategory::ProviderUnavailable, "gpu.memory_type",
                              "The GPU has no host-visible memory", "");
        }
        VkMemoryAllocateInfo allocateInfo{};
        allocateInfo.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        allocateInfo.allocationSize = requirements.size;
        allocateInfo.memoryTypeIndex = type;
        if (VkResult r = vkAllocateMemory(gpu.device, &allocateInfo, nullptr, &buffer.memory); r != VK_SUCCESS) {
            return vulkanFailure("vkAllocateMemory", r);
        }
        buffer.size = size;
        tiles::ResourceCoordinator::global().charge(tiles::Budget::Vram, size);
        if (VkResult r = vkBindBufferMemory(gpu.device, buffer.buffer, buffer.memory, 0); r != VK_SUCCESS) {
            return vulkanFailure("vkBindBufferMemory", r);
        }
        if (VkResult r = vkMapMemory(gpu.device, buffer.memory, 0, VK_WHOLE_SIZE, 0, &buffer.mapped);
            r != VK_SUCCESS) {
            return vulkanFailure("vkMapMemory", r);
        }
        return {};
    }

    // Records and runs one dispatch over `tileCount` tiles; waits for it.
    base::Status run(std::uint32_t tileCount) {
        VkDevice device = gpu.device;
        VkDescriptorBufferInfo infos[3] = {{samples.buffer, 0, VK_WHOLE_SIZE},
                                           {entries.buffer, 0, VK_WHOLE_SIZE},
                                           {result.buffer, 0, VK_WHOLE_SIZE}};
        VkWriteDescriptorSet writes[3]{};
        for (std::uint32_t i = 0; i < 3; ++i) {
            writes[i].sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
            writes[i].dstSet = set;
            writes[i].dstBinding = i;
            writes[i].descriptorCount = 1;
            writes[i].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
            writes[i].pBufferInfo = &infos[i];
        }
        vkUpdateDescriptorSets(device, 3, writes, 0, nullptr);
        vkResetCommandBuffer(commands, 0);
        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        vkBeginCommandBuffer(commands, &begin);
        vkCmdBindPipeline(commands, VK_PIPELINE_BIND_POINT_COMPUTE, pipeline);
        vkCmdBindDescriptorSets(commands, VK_PIPELINE_BIND_POINT_COMPUTE, layout, 0, 1, &set, 0, nullptr);
        vkCmdDispatch(commands, tiles::kTileSize / 16, tiles::kTileSize / 16, tileCount);
        VkBufferMemoryBarrier toHost{};
        toHost.sType = VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER;
        toHost.srcAccessMask = VK_ACCESS_SHADER_WRITE_BIT;
        toHost.dstAccessMask = VK_ACCESS_HOST_READ_BIT;
        toHost.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toHost.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
        toHost.buffer = result.buffer;
        toHost.size = VK_WHOLE_SIZE;
        vkCmdPipelineBarrier(commands, VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT, VK_PIPELINE_STAGE_HOST_BIT, 0, 0,
                             nullptr, 1, &toHost, 0, nullptr);
        if (VkResult r = vkEndCommandBuffer(commands); r != VK_SUCCESS) {
            return vulkanFailure("vkEndCommandBuffer", r);
        }
        vkResetFences(device, 1, &fence);
        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.commandBufferCount = 1;
        submit.pCommandBuffers = &commands;
        if (VkResult r = vkQueueSubmit(gpu.queue, 1, &submit, fence); r != VK_SUCCESS) {
            return vulkanFailure("vkQueueSubmit", r);
        }
        if (VkResult r = vkWaitForFences(device, 1, &fence, VK_TRUE, kFenceTimeoutNs); r != VK_SUCCESS) {
            return vulkanFailure("vkWaitForFences", r == VK_TIMEOUT ? VK_ERROR_DEVICE_LOST : r);
        }
        return {};
    }

    GpuContext::Impl &gpu;
    VkShaderModule shader = VK_NULL_HANDLE;
    VkDescriptorSetLayout setLayout = VK_NULL_HANDLE;
    VkPipelineLayout layout = VK_NULL_HANDLE;
    VkPipeline pipeline = VK_NULL_HANDLE;
    VkDescriptorPool pool = VK_NULL_HANDLE;
    VkDescriptorSet set = VK_NULL_HANDLE;
    VkCommandBuffer commands = VK_NULL_HANDLE;
    VkFence fence = VK_NULL_HANDLE;
    Buffer samples, entries, result;
};

GpuEvaluator::GpuEvaluator(std::shared_ptr<GpuContext> context, render::CpuEvaluator &fallback)
    : context_(std::move(context)), fallback_(fallback) {
    if (context_ && !context_->lost()) {
        auto pipeline = std::make_unique<Pipeline>(context_->impl());
        if (pipeline->create()) {
            pipeline_ = std::move(pipeline);
        }
    }
}

GpuEvaluator::~GpuEvaluator() = default;

bool GpuEvaluator::supports(const render::GraphNode &root) { return layerSources(root).has_value(); }

GpuEvaluator::Stats GpuEvaluator::stats() const {
    const std::lock_guard lock(mutex_);
    return stats_;
}

std::vector<base::Result<std::shared_ptr<const render::ImageBuffer>>>
GpuEvaluator::renderTiles(const render::GraphNode &root, std::span<const tiles::TileCoord> coords,
                          const render::EvaluationContext &context) {
    const std::lock_guard lock(mutex_);
    std::vector<base::Result<std::shared_ptr<const render::ImageBuffer>>> results;
    results.reserve(coords.size());
    const auto onCpu = [&](std::span<const tiles::TileCoord> rest) {
        for (auto &tile : fallback_.renderTiles(root, rest, context)) {
            results.push_back(std::move(tile));
        }
        stats_.cpuTiles += rest.size();
    };
    const auto sources = layerSources(root);
    if (!pipeline_ || !context_ || context_->lost() || !sources) {
        stats_.deviceLost = context_ && context_->lost();
        onCpu(coords);
        return results;
    }

    std::size_t next = 0;
    while (next < coords.size()) {
        if (context.stop.stop_requested()) {
            while (results.size() < coords.size()) {
                results.emplace_back(base::fail(base::ErrorCategory::Cancelled, "render.cancelled",
                                                "Rendering was cancelled"));
            }
            return results;
        }
        // A batch: tiles until the sample blocks or the tile count reach their limits.
        std::vector<std::vector<std::size_t>> contributing;
        std::size_t blocks = 0;
        std::size_t end = next;
        while (end < coords.size() && end - next < kMaxTilesPerBatch) {
            const base::IRect rect = tiles::tilePixelRect(coords[end]);
            std::vector<std::size_t> layers;
            for (std::size_t s = 0; s < sources->size(); ++s) {
                if ((*sources)[s].bounds.intersects(rect) && (*sources)[s].layer->opacity != 0.0f) {
                    layers.push_back(s);
                }
            }
            if (end > next && blocks + layers.size() > kMaxBlocksPerBatch) {
                break;
            }
            blocks += layers.size();
            contributing.push_back(std::move(layers));
            ++end;
        }
        const std::size_t tileCount = end - next;

        base::Status status;
        const VkDeviceSize sampleBytes = std::max<std::size_t>(blocks, 1) * kBlockFloats * sizeof(float);
        const VkDeviceSize entryBytes = (tileCount + blocks) * 4 * sizeof(std::uint32_t);
        const VkDeviceSize resultBytes = tileCount * kBlockFloats * sizeof(float);
        for (auto [buffer, size] : {std::pair{&pipeline_->samples, sampleBytes}, std::pair{&pipeline_->entries, entryBytes},
                                    std::pair{&pipeline_->result, resultBytes}}) {
            if (status) {
                status = pipeline_->ensure(*buffer, size);
            }
        }
        if (status) {
            auto *samples = static_cast<float *>(pipeline_->samples.mapped);
            auto *entries = static_cast<std::uint32_t *>(pipeline_->entries.mapped);
            std::size_t block = 0;
            std::size_t entry = tileCount;
            for (std::size_t t = 0; t < tileCount && status; ++t) {
                entries[t * 4 + 0] = static_cast<std::uint32_t>(entry);
                entries[t * 4 + 1] = static_cast<std::uint32_t>(contributing[t].size());
                entries[t * 4 + 2] = 0;
                entries[t * 4 + 3] = 0;
                for (const std::size_t s : contributing[t]) {
                    const Source &source = (*sources)[s];
                    status = gather(source, coords[next + t], samples + block * kBlockFloats);
                    if (!status) {
                        break;
                    }
                    entries[entry * 4 + 0] = static_cast<std::uint32_t>(block * kTilePixels);
                    entries[entry * 4 + 1] = std::bit_cast<std::uint32_t>(source.layer->opacity);
                    entries[entry * 4 + 2] = 0;
                    entries[entry * 4 + 3] = 0;
                    ++block;
                    ++entry;
                }
            }
        }
        if (status) {
            status = pipeline_->run(static_cast<std::uint32_t>(tileCount));
            ++stats_.batches;
        }
        if (!status) {
            if (status.error().code == "gpu.device_lost") {
                context_->markLost();
                stats_.deviceLost = true;
            }
            // Spec §7.5: the document is untouched; this batch and the rest are computed on the CPU.
            onCpu(coords.subspan(next));
            return results;
        }
        const auto *output = static_cast<const float *>(pipeline_->result.mapped);
        for (std::size_t t = 0; t < tileCount; ++t) {
            auto buffer = std::make_shared<render::ImageBuffer>(tiles::tilePixelRect(coords[next + t]));
            const base::IRect &region = buffer->region();
            for (std::int64_t y = region.y0; y < region.y1; ++y) {
                const std::span<float> row = buffer->row(y);
                std::memcpy(row.data(),
                            output + t * kBlockFloats + static_cast<std::size_t>(y - region.y0) * row.size(),
                            row.size() * sizeof(float));
            }
            results.emplace_back(std::shared_ptr<const render::ImageBuffer>(std::move(buffer)));
        }
        stats_.gpuTiles += tileCount;
        next = end;
    }
    return results;
}

} // namespace archmage::gpu
