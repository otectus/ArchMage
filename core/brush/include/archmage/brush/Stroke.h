// Round-brush strokes (plan S1-3, spec §16.1 PNT-01/02/03/05 subsets, §16.3, D-09).
//
// A stroke is an ordered list of input samples. Dabs are placed along the polyline through the samples at
// arc-length intervals of `spacing × current diameter`, with pressure interpolated linearly between
// samples; placement depends only on the sample sequence, never on how events were batched or on the
// display refresh rate. Each dab adds coverage to a stroke mask with max(), so overlapping dabs never build
// up beyond the stroke opacity ("flat" opacity; flow and buildup are M5). The finished mask is composited
// once onto the layer's straight samples: normal mode is source-over of the brush color at
// opacity × coverage, erase mode scales alpha by 1 − opacity × coverage and keeps the stored color.
//
// StrokeSession builds the same result incrementally for the canvas preview; the committed command
// (archmage.raster.paintStroke) recomputes it from the samples, so preview and commit match bit for bit.
//
// Thread safety: values and functions are not synchronized; a StrokeSession belongs to one thread.
#pragma once

#include "archmage/base/Error.h"
#include "archmage/base/Geometry.h"
#include "archmage/document/Document.h"
#include "archmage/tiles/TileMap.h"

#include <nlohmann/json.hpp>

#include <array>
#include <cstdint>
#include <map>
#include <memory>
#include <optional>
#include <span>
#include <vector>

namespace archmage::brush {

struct StrokeSample {
    base::DPoint position; // document coordinates
    float pressure = 1.0f; // [0, 1]; 1 for devices without pressure
    std::int64_t timestampNs = 0;

    friend bool operator==(const StrokeSample &, const StrokeSample &) = default;
};

enum class PaintMode : std::uint8_t { Normal, Erase };

struct BrushSettings {
    std::array<float, 3> color{0.0f, 0.0f, 0.0f}; // straight, in the document's encoding
    double size = 24.0;                           // diameter at full pressure, document pixels: [0.5, 5000]
    double hardness = 0.8;                        // [0, 1]; 1 is a hard edge
    double spacing = 0.1;                         // [0.01, 10], fraction of the current diameter
    double opacity = 1.0;                         // [0, 1], flat over the whole stroke
    bool pressureSize = true;                     // pressure scales the diameter (linear response)
    PaintMode mode = PaintMode::Normal;

    [[nodiscard]] base::Status validate() const;
    [[nodiscard]] nlohmann::json toJson() const;
    [[nodiscard]] static base::Result<BrushSettings> fromJson(const nlohmann::json &json);
};

struct Dab {
    base::DPoint center; // layer-local pixels
    double diameter = 1.0;
};

/// Places dabs along incoming samples (S1-3.1).
class StrokeResampler {
public:
    explicit StrokeResampler(const BrushSettings &settings);
    /// Appends the dabs that `sample` completes. The first sample always yields a dab.
    void add(const StrokeSample &sample, std::vector<Dab> &out);

private:
    [[nodiscard]] double diameterAt(float pressure) const;

    BrushSettings settings_;
    std::optional<StrokeSample> last_;
    double sinceDab_ = 0.0; // arc length travelled since the last dab
};

/// Coverage in [0, 1] of a round dab at a pixel center: hardness falloff inside a one-pixel antialiased edge;
/// dabs smaller than a pixel scale their coverage by area (S1-3.2).
[[nodiscard]] float dabCoverage(const Dab &dab, double hardness, base::DPoint pixelCenter);
/// Pixels a dab can touch (half-open).
[[nodiscard]] base::IRect dabBounds(const Dab &dab);

/// Stroke coverage, the maximum over dabs, in layer-local tiles of float coverage.
class StrokeMask {
public:
    /// Rasterizes a dab and records the touched layer-local tiles in `dirty`.
    void addDab(const Dab &dab, double hardness, tiles::DirtyRegion &dirty);
    /// Coverage buffer of a tile, or null when the stroke never touched it.
    [[nodiscard]] const tiles::TileBuffer *tile(tiles::TileCoord coord) const;
    [[nodiscard]] std::size_t tileCount() const { return tiles_.size(); }

private:
    std::map<tiles::TileCoord, std::unique_ptr<tiles::TileBuffer>> tiles_;
};

/// The tile a stroke produces from `base` and the stroke's coverage tile. Fails only when a spooled `base`
/// cannot be read back from the recovery journal.
[[nodiscard]] base::Result<std::shared_ptr<const tiles::Tile>>
applyStrokeToTile(const tiles::Tile &base, const tiles::TileBuffer &mask, const BrushSettings &settings);

/// Paints a whole stroke (samples in document coordinates) onto a layer; the layer's content is replaced
/// by new content with a new RasterId, the rest is shared.
[[nodiscard]] base::Result<std::shared_ptr<const document::Layer>> paintLayer(const document::Layer &layer,
                                                                const BrushSettings &settings,
                                                                std::span<const StrokeSample> samples,
                                                                std::size_t *dabCount = nullptr);

/// Samples as JSON for the paintStroke command: [[x, y, pressure, timestampNs], ...].
[[nodiscard]] nlohmann::json samplesToJson(std::span<const StrokeSample> samples);
[[nodiscard]] base::Result<std::vector<StrokeSample>> samplesFromJson(const nlohmann::json &json);

/// The in-progress stroke shown on the canvas before it is committed (S1-3.4).
class StrokeSession {
public:
    StrokeSession(std::shared_ptr<const document::DocumentState> base, base::LayerId layer,
                  BrushSettings settings);

    /// Adds samples; returns the document-space tiles whose pixels changed. After a failure (a spooled tile
    /// that cannot be read back) the session stops painting and status() reports it.
    tiles::DirtyRegion add(std::span<const StrokeSample> samples);
    [[nodiscard]] const base::Status &status() const { return status_; }

    [[nodiscard]] std::shared_ptr<const document::DocumentState> preview() const { return preview_; }
    [[nodiscard]] const std::vector<StrokeSample> &samples() const { return samples_; }
    [[nodiscard]] std::size_t dabCount() const { return dabCount_; }
    [[nodiscard]] base::LayerId layer() const { return layerId_; }
    [[nodiscard]] const BrushSettings &settings() const { return settings_; }
    /// Parameters of the archmage.raster.paintStroke command that reproduces this stroke exactly.
    [[nodiscard]] nlohmann::json commandParameters() const;

private:
    std::shared_ptr<const document::DocumentState> base_;
    std::shared_ptr<const document::DocumentState> preview_;
    base::LayerId layerId_;
    std::size_t layerIndex_ = 0;
    BrushSettings settings_;
    StrokeResampler resampler_;
    StrokeMask mask_;
    tiles::TileMap content_; // base content with the stroke applied so far
    std::vector<StrokeSample> samples_;
    std::size_t dabCount_ = 0;
    base::Status status_;
};

} // namespace archmage::brush
