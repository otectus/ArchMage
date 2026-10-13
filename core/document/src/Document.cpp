#include "archmage/document/Document.h"

#include <cmath>
#include <unordered_set>

namespace archmage::document {
namespace {

using base::ErrorCategory;
using base::fail;

constexpr std::int64_t kMaxOffset = std::int64_t{1} << 40; // far beyond any canvas, far below overflow

} // namespace

std::string_view blendModeName(BlendMode mode) {
    switch (mode) {
    case BlendMode::Normal:
        return "normal";
    }
    return "normal";
}

std::optional<BlendMode> blendModeFromName(std::string_view name) {
    if (name == "normal") {
        return BlendMode::Normal;
    }
    return std::nullopt;
}

std::string_view lengthUnitName(LengthUnit unit) {
    switch (unit) {
    case LengthUnit::Pixel:
        return "px";
    case LengthUnit::Inch:
        return "in";
    case LengthUnit::Millimeter:
        return "mm";
    case LengthUnit::Centimeter:
        return "cm";
    case LengthUnit::Point:
        return "pt";
    case LengthUnit::Pica:
        return "pc";
    }
    return "px";
}

std::optional<LengthUnit> lengthUnitFromName(std::string_view name) {
    for (const LengthUnit unit : {LengthUnit::Pixel, LengthUnit::Inch, LengthUnit::Millimeter,
                                  LengthUnit::Centimeter, LengthUnit::Point, LengthUnit::Pica}) {
        if (lengthUnitName(unit) == name) {
            return unit;
        }
    }
    return std::nullopt;
}

std::optional<std::size_t> DocumentState::indexOf(base::LayerId layer) const {
    for (std::size_t i = 0; i < layers.size(); ++i) {
        if (layers[i]->id == layer) {
            return i;
        }
    }
    return std::nullopt;
}

const Layer *DocumentState::findLayer(base::LayerId layer) const {
    const auto index = indexOf(layer);
    return index ? layers[*index].get() : nullptr;
}

base::Result<DocumentState> makeDocumentState(std::int64_t width, std::int64_t height,
                                              tiles::SampleType sampleType, color::ColorPolicy policy,
                                              std::string title) {
    DocumentState document;
    document.id = base::DocumentId::generate();
    document.title = std::move(title);
    document.width = width;
    document.height = height;
    document.sampleType = sampleType;
    document.colorPolicy = std::move(policy);
    document.capabilities = {std::string(kRasterLayersCapability)};
    if (auto valid = validate(document); !valid) {
        return base::unexpected(std::move(valid).error());
    }
    return document;
}

std::shared_ptr<const Layer> makeRasterLayer(const DocumentState &document, std::string name) {
    auto content = std::make_shared<RasterContent>(
        RasterContent{base::RasterId::generate(), tiles::TileMap(document.pixelLayout())});
    auto layer = std::make_shared<Layer>();
    layer->id = base::LayerId::generate();
    layer->name = std::move(name);
    layer->content = std::move(content);
    return layer;
}

base::Status validate(const DocumentState &document) {
    if (document.id.isNull()) {
        return fail(ErrorCategory::DamagedInput, "document.no_id", "The document has no identifier");
    }
    if (document.width < 1 || document.height < 1 || document.width > kMaxDimension ||
        document.height > kMaxDimension) {
        return fail(ErrorCategory::Validation, "document.dimensions",
                    "Document dimensions must be between 1 and 300,000 pixels",
                    std::to_string(document.width) + " x " + std::to_string(document.height));
    }
    if (!std::isfinite(document.resolution) || document.resolution <= 0.0) {
        return fail(ErrorCategory::Validation, "document.resolution", "Document resolution must be positive",
                    std::to_string(document.resolution));
    }
    if (document.sampleType != tiles::SampleType::UInt8 &&
        document.sampleType != tiles::SampleType::Float32) {
        return fail(ErrorCategory::UnsupportedInput, "document.precision_unsupported",
                    "Only 8-bit and 32-bit float documents are supported in this version",
                    std::string(tiles::sampleTypeName(document.sampleType)));
    }
    if (auto policy =
            color::ColorPolicy::make(document.colorPolicy.profile, document.colorPolicy.blendPolicy);
        !policy) {
        return base::unexpected(std::move(policy).error());
    }
    const tiles::ChannelLayout layout = document.pixelLayout();
    std::unordered_set<base::LayerId> layerIds;
    for (const auto &layer : document.layers) {
        if (!layer || layer->id.isNull()) {
            return fail(ErrorCategory::DamagedInput, "layer.no_id", "A layer has no identifier");
        }
        if (!layerIds.insert(layer->id).second) {
            return fail(ErrorCategory::DamagedInput, "layer.duplicate_id", "Two layers share one identifier",
                        layer->id.toString());
        }
        if (!layer->content || layer->content->id.isNull()) {
            return fail(ErrorCategory::DamagedInput, "layer.missing_content",
                        "A layer refers to missing pixels", layer->id.toString());
        }
        if (!(layer->content->tiles.layout() == layout)) {
            return fail(ErrorCategory::DamagedInput, "layer.layout_mismatch",
                        "A layer's pixel format differs from the document's", layer->id.toString());
        }
        if (!std::isfinite(layer->opacity) || layer->opacity < 0.0f || layer->opacity > 1.0f) {
            return fail(ErrorCategory::Validation, "layer.opacity_range",
                        "Layer opacity must be between 0 and 1",
                        layer->id.toString() + ": " + std::to_string(layer->opacity));
        }
        if (layer->offset.x < -kMaxOffset || layer->offset.x > kMaxOffset || layer->offset.y < -kMaxOffset ||
            layer->offset.y > kMaxOffset) {
            return fail(ErrorCategory::Validation, "layer.offset_range",
                        "A layer is placed too far from the canvas", layer->id.toString());
        }
        if (!layer->extensions.is_object()) {
            return fail(ErrorCategory::DamagedInput, "layer.extensions",
                        "A layer's extension data is damaged", layer->id.toString());
        }
    }
    if (!document.extensions.is_object()) {
        return fail(ErrorCategory::DamagedInput, "document.extensions",
                    "The document's extension data is damaged");
    }
    return {};
}

} // namespace archmage::document
