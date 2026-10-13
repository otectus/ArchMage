#include "archmage/render/Graph.h"

#include "archmage/render/Kernels.h"

#include <xxhash.h>

#include <array>
#include <bit>
#include <cstring>

namespace archmage::render {
namespace {

base::Status cancelled() {
    return base::fail(base::ErrorCategory::Cancelled, "render.cancelled", "Rendering was cancelled");
}

// A layer whose absent tiles are not transparent covers everything; Stage 1 layers default to zero.
base::IRect layerBounds(const document::Layer &layer) {
    const tiles::Tile &fallback = *layer.content->tiles.defaultTile();
    const std::size_t alphaPlane =
        layer.content->tiles.layout().planeOf(tiles::ChannelSemantic::Alpha).value_or(3);
    if (fallback.sample(alphaPlane, 0, 0) != 0.0f) {
        constexpr std::int64_t kHuge = std::int64_t{1} << 40;
        return {-kHuge, -kHuge, kHuge, kHuge};
    }
    return layer.content->tiles.pixelBounds().translated(layer.offset.x, layer.offset.y);
}

// 256-entry table converting uint8 samples to normalized floats.
const std::array<float, 256> &uint8ToFloat() {
    static const std::array<float, 256> table = [] {
        std::array<float, 256> t{};
        for (std::size_t i = 0; i < t.size(); ++i) {
            t[i] = static_cast<float>(i) * (1.0f / 255.0f);
        }
        return t;
    }();
    return table;
}

} // namespace

KeyBuilder::KeyBuilder() : state_(XXH3_createState()) {
    XXH3_64bits_reset(static_cast<XXH3_state_t *>(state_));
}
KeyBuilder::~KeyBuilder() { XXH3_freeState(static_cast<XXH3_state_t *>(state_)); }

KeyBuilder &KeyBuilder::add(std::string_view text) {
    add(static_cast<std::uint64_t>(text.size()));
    XXH3_64bits_update(static_cast<XXH3_state_t *>(state_), text.data(), text.size());
    return *this;
}

KeyBuilder &KeyBuilder::add(std::uint64_t value) {
    XXH3_64bits_update(static_cast<XXH3_state_t *>(state_), &value, sizeof value);
    return *this;
}

KeyBuilder &KeyBuilder::add(std::int64_t value) { return add(static_cast<std::uint64_t>(value)); }
KeyBuilder &KeyBuilder::add(double value) { return add(std::bit_cast<std::uint64_t>(value)); }

std::uint64_t KeyBuilder::finish() const {
    return XXH3_64bits_digest(static_cast<const XXH3_state_t *>(state_));
}

base::IRect GraphNode::requiredInputRegion(const base::IRect &output, std::size_t) const { return output; }

base::Status GraphNode::evaluateOver(const EvaluationContext &context, ImageBuffer &dst) const {
    ImageBuffer scratch(dst.region());
    if (auto status = evaluate(context, scratch); !status) {
        return status;
    }
    for (std::int64_t y = dst.region().y0; y < dst.region().y1; ++y) {
        sourceOverRow(scratch.row(y), dst.row(y));
    }
    return {};
}

LayerSourceNode::LayerSourceNode(std::shared_ptr<const document::Layer> layer, tiles::SampleType sampleType)
    : layer_(std::move(layer)), sampleType_(sampleType), bounds_(layerBounds(*layer_)) {
    KeyBuilder key;
    key.add(typeId())
        .add(static_cast<std::uint64_t>(algorithmVersion()))
        .add(layer_->offset.x)
        .add(layer_->offset.y)
        .add(static_cast<double>(layer_->opacity))
        .add(static_cast<std::uint64_t>(sampleType_))
        .add(layer_->content->tiles.defaultTile()->version());
    parameterKey_ = key.finish();
}

std::uint64_t LayerSourceNode::cacheKey(const base::IRect &region) const {
    // The tiles this region reads, by content version (0 for tiles reading as the default).
    KeyBuilder key;
    key.add(parameterKey_);
    const base::IRect local = region.intersected(bounds_).translated(-layer_->offset.x, -layer_->offset.y);
    const base::IRect range = tiles::tileRangeCovering(local);
    for (std::int64_t ty = range.y0; ty < range.y1; ++ty) {
        for (std::int64_t tx = range.x0; tx < range.x1; ++tx) {
            const auto tile = layer_->content->tiles.find({tx, ty});
            key.add(tile ? tile->version() : std::uint64_t{0});
        }
    }
    return key.finish();
}

base::Status LayerSourceNode::evaluate(const EvaluationContext &context, ImageBuffer &out) const {
    return evaluateOver(context, out); // `out` arrives transparent, so compositing over it is writing
}

base::Status LayerSourceNode::evaluateOver(const EvaluationContext &context, ImageBuffer &dst) const {
    const base::IRect target = dst.region().intersected(bounds_);
    if (target.empty() || layer_->opacity == 0.0f) {
        return {};
    }
    const tiles::TileMap &map = layer_->content->tiles;
    const tiles::ChannelLayout &layout = map.layout();
    const std::size_t planes[4] = {
        *layout.planeOf(tiles::ChannelSemantic::Red), *layout.planeOf(tiles::ChannelSemantic::Green),
        *layout.planeOf(tiles::ChannelSemantic::Blue), *layout.planeOf(tiles::ChannelSemantic::Alpha)};
    const float coverage = layer_->opacity;
    const base::IRect local = target.translated(-layer_->offset.x, -layer_->offset.y);
    const base::IRect range = tiles::tileRangeCovering(local);
    std::uint64_t nonFinite = 0;
    std::array<std::vector<float>, 4> rowSamples;
    for (auto &row : rowSamples) {
        row.resize(tiles::kTileSize);
    }
    const auto &table = uint8ToFloat();

    for (std::int64_t ty = range.y0; ty < range.y1; ++ty) {
        for (std::int64_t tx = range.x0; tx < range.x1; ++tx) {
            if (context.stop.stop_requested()) {
                return cancelled();
            }
            const tiles::TileCoord coord{tx, ty};
            const std::shared_ptr<const tiles::Tile> tile = map.find(coord);
            const base::IRect tileRect = tiles::tilePixelRect(coord);
            const base::IRect part = tileRect.intersected(local);
            if (!tile) {
                continue; // default tiles are transparent (bounds_ excludes the rest)
            }
            const auto count = static_cast<std::size_t>(part.width());
            for (std::int64_t y = part.y0; y < part.y1; ++y) {
                float *out = dst.pixel(part.x0 + layer_->offset.x, y + layer_->offset.y);
                if (tile->isUniform()) {
                    float values[4];
                    for (std::size_t c = 0; c < 4; ++c) {
                        values[c] = tiles::loadSample(sampleType_, tile->uniformSample(planes[c]).data());
                    }
                    premultiplyOverUniform(values[0], values[1], values[2], values[3], count, coverage, out,
                                           nonFinite);
                    continue;
                }
                const std::size_t first =
                    static_cast<std::size_t>((y - tileRect.y0) * tiles::kTileSize + (part.x0 - tileRect.x0));
                for (std::size_t c = 0; c < 4; ++c) {
                    const std::byte *source = tile->plane(planes[c]).data();
                    float *row = rowSamples[c].data();
                    if (sampleType_ == tiles::SampleType::UInt8) {
                        for (std::size_t i = 0; i < count; ++i) {
                            row[i] = table[static_cast<std::uint8_t>(source[first + i])];
                        }
                    } else if (sampleType_ == tiles::SampleType::Float32) {
                        std::memcpy(row, source + first * 4, count * 4);
                    } else {
                        const std::size_t bytes = tiles::bytesPerSample(sampleType_);
                        for (std::size_t i = 0; i < count; ++i) {
                            row[i] = tiles::loadSample(sampleType_, source + (first + i) * bytes);
                        }
                    }
                }
                premultiplyOverRow(rowSamples[0].data(), rowSamples[1].data(), rowSamples[2].data(),
                                   rowSamples[3].data(), count, coverage, out, nonFinite);
            }
        }
    }
    if (nonFinite > 0 && context.nonFiniteSamples != nullptr) {
        context.nonFiniteSamples->fetch_add(nonFinite, std::memory_order_relaxed);
    }
    return {};
}

CompositeNode::CompositeNode(std::vector<std::shared_ptr<const GraphNode>> bottomToTop)
    : inputs_(std::move(bottomToTop)) {
    for (const auto &input : inputs_) {
        bounds_ = bounds_.united(input->bounds());
    }
}

std::uint64_t CompositeNode::cacheKey(const base::IRect &region) const {
    KeyBuilder key;
    key.add(typeId()).add(static_cast<std::uint64_t>(algorithmVersion()));
    for (const auto &input : inputs_) {
        if (input->bounds().intersects(region)) { // inputs outside the region cannot affect it
            key.add(input->cacheKey(region));
        }
    }
    return key.finish();
}

base::Status CompositeNode::evaluate(const EvaluationContext &context, ImageBuffer &out) const {
    for (const auto &input : inputs_) {
        if (context.stop.stop_requested()) {
            return cancelled();
        }
        if (!input->bounds().intersects(out.region())) {
            continue;
        }
        if (auto status = input->evaluateOver(context, out); !status) {
            return status;
        }
    }
    return {};
}

std::shared_ptr<const GraphNode> compileGraph(const document::DocumentState &document) {
    std::vector<std::shared_ptr<const GraphNode>> sources;
    sources.reserve(document.layers.size());
    for (const auto &layer : document.layers) {
        if (layer->visible && layer->opacity > 0.0f) {
            sources.push_back(std::make_shared<LayerSourceNode>(layer, document.sampleType));
        }
    }
    return std::make_shared<CompositeNode>(std::move(sources));
}

} // namespace archmage::render
