// The processing graph (spec §7.2, D-09): the layer stack compiled into evaluation nodes.
//
// Every node declares its type and algorithm version, operation class, bounds, the input region it needs
// for a requested output region, its halo and a cache key covering every rendering input. Evaluation pulls
// regions: a node writes premultiplied RGBA float pixels for exactly the region of the buffer it is given.
// Stage 1 nodes: layer sources and a source-over composite; S1-6 adds a blur with a halo.
//
// Thread safety: nodes are immutable after construction; evaluate() is safe from several threads at once.
#pragma once

#include "archmage/base/Error.h"
#include "archmage/base/Geometry.h"
#include "archmage/document/Document.h"
#include "archmage/render/ImageBuffer.h"

#include <atomic>
#include <cstdint>
#include <memory>
#include <span>
#include <stop_token>
#include <string_view>
#include <vector>

namespace archmage::render {

struct RationalTime {
    std::int64_t numerator = 0;
    std::int64_t denominator = 1;

    friend constexpr bool operator==(const RationalTime &, const RationalTime &) = default;
};

enum class Quality : std::uint8_t { Preview, Final };
enum class OperationClass : std::uint8_t {
    Local,
    BoundedNeighborhood,
    GlobalReduction,
    Multiscale,
    Temporal,
    ModelBased
};

struct EvaluationContext {
    RationalTime time;
    Quality quality = Quality::Final;
    std::stop_token stop;
    std::atomic<std::uint64_t> *nonFiniteSamples = nullptr; // optional diagnostics counter
};

/// Accumulates a 64-bit XXH3 cache key from typed fields.
class KeyBuilder {
public:
    KeyBuilder();
    ~KeyBuilder();
    KeyBuilder(const KeyBuilder &) = delete;
    KeyBuilder &operator=(const KeyBuilder &) = delete;

    KeyBuilder &add(std::string_view text);
    KeyBuilder &add(std::uint64_t value);
    KeyBuilder &add(std::int64_t value);
    KeyBuilder &add(double value);
    [[nodiscard]] std::uint64_t finish() const;

private:
    void *state_; // XXH3_state_t
};

class GraphNode {
public:
    virtual ~GraphNode() = default;

    [[nodiscard]] virtual std::string_view typeId() const = 0;
    [[nodiscard]] virtual int algorithmVersion() const = 0;
    [[nodiscard]] virtual OperationClass operationClass() const = 0;
    /// Document rectangle outside which the node's output is transparent.
    [[nodiscard]] virtual base::IRect bounds() const = 0;
    [[nodiscard]] virtual std::span<const std::shared_ptr<const GraphNode>> inputs() const { return {}; }
    /// Region of input `index` needed to produce `output`; local operations need exactly `output`.
    [[nodiscard]] virtual base::IRect requiredInputRegion(const base::IRect &output, std::size_t index) const;
    /// Extra border a neighborhood operation reads around its output.
    [[nodiscard]] virtual std::int64_t halo() const { return 0; }
    /// Hash of every input that affects the output: type, version, parameters, sources and inputs' keys.
    [[nodiscard]] virtual std::uint64_t cacheKey() const = 0;

    /// Writes the node's output for out.region() into `out`, which arrives transparent.
    virtual base::Status evaluate(const EvaluationContext &context, ImageBuffer &out) const = 0;
    /// Composites the node's output for dst.region() over `dst` with source-over. The default evaluates
    /// into a scratch buffer; sources override it to composite in place.
    virtual base::Status evaluateOver(const EvaluationContext &context, ImageBuffer &dst) const;
};

/// One raster layer: its straight samples, offset and opacity (as coverage), premultiplied.
class LayerSourceNode final : public GraphNode {
public:
    LayerSourceNode(std::shared_ptr<const document::Layer> layer, tiles::SampleType sampleType);

    [[nodiscard]] std::string_view typeId() const override { return "archmage.render.layerSource"; }
    [[nodiscard]] int algorithmVersion() const override { return 1; }
    [[nodiscard]] OperationClass operationClass() const override { return OperationClass::Local; }
    [[nodiscard]] base::IRect bounds() const override { return bounds_; }
    [[nodiscard]] std::uint64_t cacheKey() const override { return key_; }
    base::Status evaluate(const EvaluationContext &context, ImageBuffer &out) const override;
    base::Status evaluateOver(const EvaluationContext &context, ImageBuffer &dst) const override;

private:
    std::shared_ptr<const document::Layer> layer_;
    tiles::SampleType sampleType_;
    base::IRect bounds_;
    std::uint64_t key_;
};

/// Source-over of its inputs, bottom to top (spec §7.4 equations; encoded blend policy, D-09).
class CompositeNode final : public GraphNode {
public:
    explicit CompositeNode(std::vector<std::shared_ptr<const GraphNode>> bottomToTop);

    [[nodiscard]] std::string_view typeId() const override { return "archmage.render.composite"; }
    [[nodiscard]] int algorithmVersion() const override { return 1; }
    [[nodiscard]] OperationClass operationClass() const override { return OperationClass::Local; }
    [[nodiscard]] base::IRect bounds() const override { return bounds_; }
    [[nodiscard]] std::span<const std::shared_ptr<const GraphNode>> inputs() const override {
        return inputs_;
    }
    [[nodiscard]] std::uint64_t cacheKey() const override { return key_; }
    base::Status evaluate(const EvaluationContext &context, ImageBuffer &out) const override;

private:
    std::vector<std::shared_ptr<const GraphNode>> inputs_;
    base::IRect bounds_;
    std::uint64_t key_;
};

/// The document's layer stack as a graph: visible layers with nonzero opacity, bottom to top, under one
/// composite node. Stage 1 documents use the encoded blend policy (D-09).
[[nodiscard]] std::shared_ptr<const GraphNode> compileGraph(const document::DocumentState &document);

} // namespace archmage::render
