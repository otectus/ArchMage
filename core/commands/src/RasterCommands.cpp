// Raster commands (plan S1-3.3, §8.1): archmage.raster.paintStroke.

#include "archmage/brush/Stroke.h"
#include "archmage/commands/Command.h"

namespace archmage::commands {
namespace {

using nlohmann::json;

/// One brush or eraser stroke: target the layer; parameters are the brush settings plus "samples" in
/// document coordinates. The result is recomputed from the samples, so a replay is bit-identical.
class PaintStroke final : public CommandDefinition {
public:
    [[nodiscard]] std::string_view id() const override { return "archmage.raster.paintStroke"; }
    [[nodiscard]] base::Result<CommandEffect> apply(const CommandEnvelope &e,
                                                    const document::DocumentState &state) const override {
        auto target = detail::singleLayerTarget(e, state);
        if (!target) {
            return base::unexpected(std::move(target).error());
        }
        const document::Layer &layer = *state.findLayer(*target);
        if (auto unlocked = detail::requireUnlocked(layer); !unlocked) {
            return base::unexpected(std::move(unlocked).error());
        }
        auto settings = brush::BrushSettings::fromJson(e.parameters);
        if (!settings) {
            return base::unexpected(std::move(settings).error());
        }
        if (!e.parameters.contains("samples")) {
            return detail::badParameters(id(), "samples are required");
        }
        auto samples = brush::samplesFromJson(e.parameters["samples"]);
        if (!samples) {
            return base::unexpected(std::move(samples).error());
        }
        std::size_t dabs = 0;
        CommandEffect effect{
            state, settings->mode == brush::PaintMode::Erase ? "Eraser" : "Brush Stroke", {}};
        effect.state.layers[*state.indexOf(*target)] = brush::paintLayer(layer, *settings, *samples, &dabs);
        effect.result["dabs"] = dabs;
        if (!layer.visible) {
            effect.result["warning"] = "layer.hidden"; // allowed, but the user cannot see the result
        }
        return effect;
    }
};

} // namespace

void registerRasterCommands(CommandRegistry &registry) { registry.add(std::make_unique<PaintStroke>()); }

} // namespace archmage::commands
