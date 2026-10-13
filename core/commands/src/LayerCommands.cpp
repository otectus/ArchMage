// Stage 1 document and layer commands (plan S1-2.6, §8.1).

#include "archmage/commands/Command.h"

#include <cmath>

namespace archmage::commands {
namespace {

using base::ErrorCategory;
using document::DocumentState;
using document::Layer;
using nlohmann::json;

std::shared_ptr<Layer> mutableCopy(const DocumentState &state, base::LayerId id) {
    return std::make_shared<Layer>(*state.findLayer(id));
}

void replaceLayer(DocumentState &state, std::shared_ptr<const Layer> layer) {
    state.layers[*state.indexOf(layer->id)] = std::move(layer);
}

// Opaque white over the canvas rectangle, transparent beyond it (edge tiles are dense).
void fillWhite(tiles::TileMap &map, const base::IRect &canvas) {
    const tiles::ChannelLayout &layout = map.layout();
    std::vector<std::byte> white(layout.planeCount() * layout.sampleBytes());
    for (std::size_t p = 0; p < layout.planeCount(); ++p) {
        tiles::storeSample(layout.sampleType(), 1.0f, white.data() + p * layout.sampleBytes());
    }
    const auto uniform = tiles::Tile::makeUniform(layout.sampleType(), layout.planeCount(), white);
    const base::IRect range = tiles::tileRangeCovering(canvas);
    for (std::int64_t ty = range.y0; ty < range.y1; ++ty) {
        for (std::int64_t tx = range.x0; tx < range.x1; ++tx) {
            const tiles::TileCoord coord{tx, ty};
            const base::IRect rect = tiles::tilePixelRect(coord);
            if (canvas.contains(rect)) {
                map.set(coord, uniform);
                continue;
            }
            tiles::TileBuffer buffer(layout.sampleType(), layout.planeCount());
            const base::IRect inside = rect.intersected(canvas);
            for (std::int64_t y = inside.y0; y < inside.y1; ++y) {
                for (std::int64_t x = inside.x0; x < inside.x1; ++x) {
                    for (std::size_t p = 0; p < layout.planeCount(); ++p) {
                        buffer.setSample(p, static_cast<int>(x - rect.x0), static_cast<int>(y - rect.y0),
                                         1.0f);
                    }
                }
            }
            map.set(coord, std::move(buffer).seal());
        }
    }
}

std::optional<std::string> optionalString(const json &parameters, const char *key) {
    if (parameters.contains(key) && parameters[key].is_string()) {
        return parameters[key].get<std::string>();
    }
    return std::nullopt;
}

class AddLayer final : public CommandDefinition {
public:
    [[nodiscard]] std::string_view id() const override { return "archmage.layer.add"; }
    [[nodiscard]] base::Result<CommandEffect> apply(const CommandEnvelope &e,
                                                    const DocumentState &state) const override {
        // Inserted above the target layer, or at `index` (bottom to top), or on top.
        std::size_t index = state.layers.size();
        if (!e.targetIds.empty()) {
            auto above = detail::singleLayerTarget(e, state);
            if (!above) {
                return base::unexpected(std::move(above).error());
            }
            index = *state.indexOf(*above) + 1;
        } else if (e.parameters.contains("index")) {
            if (!e.parameters["index"].is_number_integer() || e.parameters["index"].get<std::int64_t>() < 0 ||
                e.parameters["index"].get<std::size_t>() > state.layers.size()) {
                return detail::badParameters(id(), "index out of range");
            }
            index = e.parameters["index"].get<std::size_t>();
        }
        const std::string name =
            optionalString(e.parameters, "name").value_or("Layer " + std::to_string(state.layers.size() + 1));
        CommandEffect effect{state, "New Layer", {}};
        auto layer = document::makeRasterLayer(state, name);
        effect.result["layerId"] = layer->id.toString();
        effect.state.layers.insert(effect.state.layers.begin() + static_cast<std::ptrdiff_t>(index),
                                   std::move(layer));
        return effect;
    }
};

class RemoveLayer final : public CommandDefinition {
public:
    [[nodiscard]] std::string_view id() const override { return "archmage.layer.remove"; }
    [[nodiscard]] base::Result<CommandEffect> apply(const CommandEnvelope &e,
                                                    const DocumentState &state) const override {
        auto target = detail::singleLayerTarget(e, state);
        if (!target) {
            return base::unexpected(std::move(target).error());
        }
        if (auto unlocked = detail::requireUnlocked(*state.findLayer(*target)); !unlocked) {
            return base::unexpected(std::move(unlocked).error());
        }
        CommandEffect effect{state, "Delete Layer", {}};
        effect.state.layers.erase(effect.state.layers.begin() +
                                  static_cast<std::ptrdiff_t>(*state.indexOf(*target)));
        return effect;
    }
};

class RenameLayer final : public CommandDefinition {
public:
    [[nodiscard]] std::string_view id() const override { return "archmage.layer.rename"; }
    [[nodiscard]] base::Result<CommandEffect> apply(const CommandEnvelope &e,
                                                    const DocumentState &state) const override {
        auto target = detail::singleLayerTarget(e, state);
        if (!target) {
            return base::unexpected(std::move(target).error());
        }
        const auto name = optionalString(e.parameters, "name");
        if (!name || name->empty() || name->size() > 4096) {
            return detail::badParameters(id(), "name must be 1 to 4096 bytes");
        }
        CommandEffect effect{state, "Rename Layer", {}};
        auto layer = mutableCopy(state, *target);
        layer->name = *name;
        replaceLayer(effect.state, std::move(layer));
        return effect;
    }
};

class ReorderLayer final : public CommandDefinition {
public:
    [[nodiscard]] std::string_view id() const override { return "archmage.layer.reorder"; }
    [[nodiscard]] base::Result<CommandEffect> apply(const CommandEnvelope &e,
                                                    const DocumentState &state) const override {
        auto target = detail::singleLayerTarget(e, state);
        if (!target) {
            return base::unexpected(std::move(target).error());
        }
        if (!e.parameters.contains("index") || !e.parameters["index"].is_number_integer() ||
            e.parameters["index"].get<std::int64_t>() < 0 ||
            e.parameters["index"].get<std::size_t>() >= state.layers.size()) {
            return detail::badParameters(id(), "index must name a position in the layer stack");
        }
        const std::size_t from = *state.indexOf(*target);
        const std::size_t to = e.parameters["index"].get<std::size_t>();
        CommandEffect effect{state, "Reorder Layer", {}};
        auto layer = effect.state.layers[from];
        effect.state.layers.erase(effect.state.layers.begin() + static_cast<std::ptrdiff_t>(from));
        effect.state.layers.insert(effect.state.layers.begin() + static_cast<std::ptrdiff_t>(to),
                                   std::move(layer));
        return effect;
    }
};

class DuplicateInstance final : public CommandDefinition {
public:
    [[nodiscard]] std::string_view id() const override { return "archmage.layer.duplicateInstance"; }
    [[nodiscard]] base::Result<CommandEffect> apply(const CommandEnvelope &e,
                                                    const DocumentState &state) const override {
        auto target = detail::singleLayerTarget(e, state);
        if (!target) {
            return base::unexpected(std::move(target).error());
        }
        const Layer &source = *state.findLayer(*target);
        auto copy = std::make_shared<Layer>(source); // shares the immutable content (spec §6.1 instance)
        copy->id = base::LayerId::generate();
        copy->name = optionalString(e.parameters, "name").value_or(source.name + " copy");
        CommandEffect effect{state, "Duplicate Layer", {}};
        effect.result["layerId"] = copy->id.toString();
        effect.state.layers.insert(effect.state.layers.begin() +
                                       static_cast<std::ptrdiff_t>(*state.indexOf(*target) + 1),
                                   std::move(copy));
        return effect;
    }
};

class SetVisible final : public CommandDefinition {
public:
    [[nodiscard]] std::string_view id() const override { return "archmage.layer.setVisible"; }
    [[nodiscard]] base::Result<CommandEffect> apply(const CommandEnvelope &e,
                                                    const DocumentState &state) const override {
        auto target = detail::singleLayerTarget(e, state);
        if (!target) {
            return base::unexpected(std::move(target).error());
        }
        if (!e.parameters.contains("visible") || !e.parameters["visible"].is_boolean()) {
            return detail::badParameters(id(), "visible must be true or false");
        }
        const bool visible = e.parameters["visible"].get<bool>();
        CommandEffect effect{state, visible ? "Show Layer" : "Hide Layer", {}};
        auto layer = mutableCopy(state, *target);
        layer->visible = visible;
        replaceLayer(effect.state, std::move(layer));
        return effect;
    }
};

class SetOpacity final : public CommandDefinition {
public:
    [[nodiscard]] std::string_view id() const override { return "archmage.layer.setOpacity"; }
    [[nodiscard]] base::Result<CommandEffect> apply(const CommandEnvelope &e,
                                                    const DocumentState &state) const override {
        auto target = detail::singleLayerTarget(e, state);
        if (!target) {
            return base::unexpected(std::move(target).error());
        }
        if (auto unlocked = detail::requireUnlocked(*state.findLayer(*target)); !unlocked) {
            return base::unexpected(std::move(unlocked).error());
        }
        if (!e.parameters.contains("opacity") || !e.parameters["opacity"].is_number()) {
            return detail::badParameters(id(), "opacity must be a number");
        }
        const double opacity = e.parameters["opacity"].get<double>();
        if (!std::isfinite(opacity) || opacity < 0.0 || opacity > 1.0) {
            return detail::badParameters(id(), "opacity must be between 0 and 1");
        }
        CommandEffect effect{state, "Layer Opacity", {}};
        auto layer = mutableCopy(state, *target);
        layer->opacity = static_cast<float>(opacity);
        replaceLayer(effect.state, std::move(layer));
        return effect;
    }
};

class SetLocked final : public CommandDefinition {
public:
    [[nodiscard]] std::string_view id() const override { return "archmage.layer.setLocked"; }
    [[nodiscard]] base::Result<CommandEffect> apply(const CommandEnvelope &e,
                                                    const DocumentState &state) const override {
        auto target = detail::singleLayerTarget(e, state);
        if (!target) {
            return base::unexpected(std::move(target).error());
        }
        if (!e.parameters.contains("locked") || !e.parameters["locked"].is_boolean()) {
            return detail::badParameters(id(), "locked must be true or false");
        }
        const bool locked = e.parameters["locked"].get<bool>();
        CommandEffect effect{state, locked ? "Lock Layer" : "Unlock Layer", {}};
        auto layer = mutableCopy(state, *target);
        layer->locked = locked;
        replaceLayer(effect.state, std::move(layer));
        return effect;
    }
};

class SetOffset final : public CommandDefinition {
public:
    [[nodiscard]] std::string_view id() const override { return "archmage.layer.setOffset"; }
    [[nodiscard]] base::Result<CommandEffect> apply(const CommandEnvelope &e,
                                                    const DocumentState &state) const override {
        auto target = detail::singleLayerTarget(e, state);
        if (!target) {
            return base::unexpected(std::move(target).error());
        }
        if (auto unlocked = detail::requireUnlocked(*state.findLayer(*target)); !unlocked) {
            return base::unexpected(std::move(unlocked).error());
        }
        if (!e.parameters.contains("x") || !e.parameters.contains("y") ||
            !e.parameters["x"].is_number_integer() || !e.parameters["y"].is_number_integer()) {
            return detail::badParameters(id(), "x and y must be integers");
        }
        CommandEffect effect{state, "Move Layer", {}};
        auto layer = mutableCopy(state, *target);
        layer->offset = {e.parameters["x"].get<std::int64_t>(), e.parameters["y"].get<std::int64_t>()};
        replaceLayer(effect.state, std::move(layer));
        if (auto valid = document::validate(effect.state); !valid) {
            return base::unexpected(std::move(valid).error());
        }
        return effect;
    }
};

} // namespace

base::Result<CommandEffect> createDocument(const CommandEnvelope &e) {
    if (e.commandId != "archmage.document.create") {
        return base::fail(ErrorCategory::Internal, "command.wrong_id", "Not a document creation command",
                          e.commandId);
    }
    const json &p = e.parameters;
    if (!p.contains("width") || !p.contains("height") || !p["width"].is_number_integer() ||
        !p["height"].is_number_integer()) {
        return detail::badParameters(e.commandId, "width and height must be integers");
    }
    tiles::SampleType type = tiles::SampleType::UInt8;
    if (const auto name = optionalString(p, "sampleType")) {
        if (*name == "float32") {
            type = tiles::SampleType::Float32;
        } else if (*name != "uint8") {
            return base::fail(ErrorCategory::UnsupportedInput, "document.precision_unsupported",
                              "Only 8-bit and 32-bit float documents are supported in this version", *name);
        }
    }
    auto state = document::makeDocumentState(p["width"].get<std::int64_t>(), p["height"].get<std::int64_t>(),
                                             type, color::ColorPolicy::defaultPolicy(),
                                             optionalString(p, "title").value_or("Untitled"));
    if (!state) {
        return base::unexpected(std::move(state).error());
    }
    if (p.contains("resolution")) {
        if (!p["resolution"].is_number() || !(p["resolution"].get<double>() > 0.0)) {
            return detail::badParameters(e.commandId, "resolution must be positive");
        }
        state->resolution = p["resolution"].get<double>();
    }
    const std::string background = optionalString(p, "background").value_or("white");
    if (background != "white" && background != "transparent") {
        return detail::badParameters(e.commandId, "background must be white or transparent");
    }
    auto layer = std::const_pointer_cast<Layer>(document::makeRasterLayer(*state, "Background"));
    if (background == "white") {
        auto content = std::make_shared<document::RasterContent>(*layer->content);
        fillWhite(content->tiles, state->canvasRect());
        layer->content = std::move(content);
    }
    state->layers.push_back(std::move(layer));
    CommandEffect effect{*state, "New Document", {{"documentId", state->id.toString()}}};
    return effect;
}

void registerLayerCommands(CommandRegistry &registry) {
    registry.add(std::make_unique<AddLayer>());
    registry.add(std::make_unique<RemoveLayer>());
    registry.add(std::make_unique<RenameLayer>());
    registry.add(std::make_unique<ReorderLayer>());
    registry.add(std::make_unique<DuplicateInstance>());
    registry.add(std::make_unique<SetVisible>());
    registry.add(std::make_unique<SetOpacity>());
    registry.add(std::make_unique<SetLocked>());
    registry.add(std::make_unique<SetOffset>());
}

} // namespace archmage::commands
