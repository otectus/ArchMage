#include "archmage/brush/Stroke.h"

#include <algorithm>
#include <cmath>

namespace archmage::brush {
namespace {

using base::ErrorCategory;
using nlohmann::json;

constexpr double kMinimumStep = 0.5; // pixels: zero-pressure dabs must still advance
constexpr std::size_t kMaxSamples = 1'000'000;

base::Unexpected<base::Error> invalid(std::string detail) {
    return base::fail(ErrorCategory::Validation, "brush.settings", "The brush settings are not valid",
                      std::move(detail));
}

double lerp(double a, double b, double t) { return a + (b - a) * t; }

} // namespace

base::Status BrushSettings::validate() const {
    for (const float c : color) {
        if (!std::isfinite(c)) {
            return invalid("color must be finite");
        }
    }
    if (!(size >= 0.5 && size <= 5000.0)) {
        return invalid("size must be between 0.5 and 5000 pixels");
    }
    if (!(hardness >= 0.0 && hardness <= 1.0)) {
        return invalid("hardness must be between 0 and 1");
    }
    if (!(spacing >= 0.01 && spacing <= 10.0)) {
        return invalid("spacing must be between 0.01 and 10");
    }
    if (!(opacity >= 0.0 && opacity <= 1.0)) {
        return invalid("opacity must be between 0 and 1");
    }
    return {};
}

json BrushSettings::toJson() const {
    return {{"color", {color[0], color[1], color[2]}},
            {"size", size},
            {"hardness", hardness},
            {"spacing", spacing},
            {"opacity", opacity},
            {"pressureSize", pressureSize},
            {"mode", mode == PaintMode::Normal ? "normal" : "erase"}};
}

base::Result<BrushSettings> BrushSettings::fromJson(const json &in) {
    BrushSettings s;
    if (!in.is_object()) {
        return invalid("not an object");
    }
    const auto number = [&in](const char *key, double &out) -> bool {
        if (!in.contains(key)) {
            return true; // keep the default
        }
        if (!in[key].is_number()) {
            return false;
        }
        out = in[key].get<double>();
        return true;
    };
    if (in.contains("color")) {
        if (!in["color"].is_array() || in["color"].size() != 3) {
            return invalid("color must be three numbers");
        }
        for (std::size_t i = 0; i < 3; ++i) {
            if (!in["color"][i].is_number()) {
                return invalid("color must be three numbers");
            }
            s.color[i] = in["color"][i].get<float>();
        }
    }
    if (!number("size", s.size) || !number("hardness", s.hardness) || !number("spacing", s.spacing) ||
        !number("opacity", s.opacity)) {
        return invalid("size, hardness, spacing and opacity must be numbers");
    }
    if (in.contains("pressureSize")) {
        if (!in["pressureSize"].is_boolean()) {
            return invalid("pressureSize must be true or false");
        }
        s.pressureSize = in["pressureSize"].get<bool>();
    }
    if (in.contains("mode")) {
        if (in["mode"] == "normal") {
            s.mode = PaintMode::Normal;
        } else if (in["mode"] == "erase") {
            s.mode = PaintMode::Erase;
        } else {
            return invalid("mode must be normal or erase");
        }
    }
    if (auto valid = s.validate(); !valid) {
        return base::unexpected(std::move(valid).error());
    }
    return s;
}

StrokeResampler::StrokeResampler(const BrushSettings &settings) : settings_(settings) {}

double StrokeResampler::diameterAt(float pressure) const {
    return settings_.pressureSize ? settings_.size * std::clamp(static_cast<double>(pressure), 0.0, 1.0)
                                  : settings_.size;
}

void StrokeResampler::add(const StrokeSample &sample, std::vector<Dab> &out) {
    if (!last_) {
        out.push_back({sample.position, diameterAt(sample.pressure)});
        last_ = sample;
        sinceDab_ = 0.0;
        return;
    }
    const base::DPoint p0 = last_->position;
    const base::DPoint p1 = sample.position;
    const double dx = p1.x - p0.x;
    const double dy = p1.y - p0.y;
    const double length = std::sqrt(dx * dx + dy * dy);
    if (length > 0.0) {
        double t = 0.0; // distance along the segment
        for (;;) {
            const double pressureHere = lerp(last_->pressure, sample.pressure, t / length);
            const double step =
                std::max(settings_.spacing * diameterAt(static_cast<float>(pressureHere)), kMinimumStep);
            const double need = step - sinceDab_;
            if (t + need > length) {
                sinceDab_ += length - t;
                break;
            }
            t += need;
            const double f = t / length;
            const auto pressure = static_cast<float>(lerp(last_->pressure, sample.pressure, f));
            out.push_back({{p0.x + dx * f, p0.y + dy * f}, diameterAt(pressure)});
            sinceDab_ = 0.0;
        }
    }
    last_ = sample;
}

float dabCoverage(const Dab &dab, double hardness, base::DPoint pixelCenter) {
    if (!(dab.diameter > 0.0)) {
        return 0.0f;
    }
    const double radius = std::max(dab.diameter, 1.0) * 0.5;
    const double areaScale = dab.diameter < 1.0 ? dab.diameter * dab.diameter : 1.0;
    const double dx = pixelCenter.x - dab.center.x;
    const double dy = pixelCenter.y - dab.center.y;
    const double distance = std::sqrt(dx * dx + dy * dy);
    const double edge = std::clamp(radius + 0.5 - distance, 0.0, 1.0);
    if (edge == 0.0) {
        return 0.0f;
    }
    double falloff = 1.0;
    const double inner = radius * hardness;
    if (distance > inner && hardness < 1.0) {
        const double x = std::clamp((distance - inner) / std::max(radius - inner, 1e-9), 0.0, 1.0);
        falloff = 1.0 - x * x * (3.0 - 2.0 * x);
    }
    return static_cast<float>(edge * falloff * areaScale);
}

base::IRect dabBounds(const Dab &dab) {
    const double reach = std::max(dab.diameter, 1.0) * 0.5 + 1.0;
    return base::DRect{dab.center.x - reach, dab.center.y - reach, 2.0 * reach, 2.0 * reach}.coveringPixels();
}

void StrokeMask::addDab(const Dab &dab, double hardness, tiles::DirtyRegion &dirty) {
    const base::IRect bounds = dabBounds(dab);
    const base::IRect range = tiles::tileRangeCovering(bounds);
    for (std::int64_t ty = range.y0; ty < range.y1; ++ty) {
        for (std::int64_t tx = range.x0; tx < range.x1; ++tx) {
            const tiles::TileCoord coord{tx, ty};
            const base::IRect tileRect = tiles::tilePixelRect(coord);
            const base::IRect part = tileRect.intersected(bounds);
            auto &buffer = tiles_[coord];
            if (!buffer) {
                buffer = std::make_unique<tiles::TileBuffer>(tiles::SampleType::Float32, 1);
            }
            const std::span<float> coverage = buffer->planeAs<float>(0);
            for (std::int64_t y = part.y0; y < part.y1; ++y) {
                float *row = coverage.data() + (y - tileRect.y0) * tiles::kTileSize;
                for (std::int64_t x = part.x0; x < part.x1; ++x) {
                    const float c = dabCoverage(dab, hardness, base::pixelCenter({x, y}));
                    float &slot = row[x - tileRect.x0];
                    slot = std::max(slot, c);
                }
            }
            dirty.addTile(coord);
        }
    }
}

const tiles::TileBuffer *StrokeMask::tile(tiles::TileCoord coord) const {
    const auto it = tiles_.find(coord);
    return it == tiles_.end() ? nullptr : it->second.get();
}

std::shared_ptr<const tiles::Tile> applyStrokeToTile(const tiles::Tile &base, const tiles::TileBuffer &mask,
                                                     const BrushSettings &settings) {
    tiles::TileBuffer out(base);
    const tiles::SampleType type = out.sampleType();
    const std::size_t bytes = tiles::bytesPerSample(type);
    const std::span<const std::byte> coverageBytes = mask.plane(0);
    const auto *coverage = reinterpret_cast<const float *>(coverageBytes.data());
    std::byte *planes[4] = {out.plane(0).data(), out.plane(1).data(), out.plane(2).data(),
                            out.plane(3).data()};
    const auto opacity = static_cast<float>(settings.opacity);
    for (std::size_t i = 0; i < tiles::kTilePixels; ++i) {
        const float m = std::clamp(coverage[i] * opacity, 0.0f, 1.0f);
        if (m <= 0.0f) {
            continue;
        }
        const float a = std::clamp(tiles::loadSample(type, planes[3] + i * bytes), 0.0f, 1.0f);
        if (settings.mode == PaintMode::Erase) {
            tiles::storeSample(type, a * (1.0f - m), planes[3] + i * bytes);
            continue;
        }
        const float outA = m + a * (1.0f - m);
        for (std::size_t c = 0; c < 3; ++c) {
            const float d = tiles::loadSample(type, planes[c] + i * bytes);
            tiles::storeSample(type, (settings.color[c] * m + d * a * (1.0f - m)) / outA,
                               planes[c] + i * bytes);
        }
        tiles::storeSample(type, outA, planes[3] + i * bytes);
    }
    return std::move(out).seal();
}

std::shared_ptr<const document::Layer> paintLayer(const document::Layer &layer, const BrushSettings &settings,
                                                  std::span<const StrokeSample> samples,
                                                  std::size_t *dabCount) {
    StrokeResampler resampler(settings);
    std::vector<Dab> dabs;
    for (const StrokeSample &sample : samples) {
        StrokeSample local = sample;
        local.position = {sample.position.x - static_cast<double>(layer.offset.x),
                          sample.position.y - static_cast<double>(layer.offset.y)};
        resampler.add(local, dabs);
    }
    StrokeMask mask;
    tiles::DirtyRegion dirty;
    for (const Dab &dab : dabs) {
        mask.addDab(dab, settings.hardness, dirty);
    }
    tiles::TileMap map = layer.content->tiles;
    for (const tiles::TileCoord coord : dirty.tiles()) {
        map.set(coord, applyStrokeToTile(*map.at(coord), *mask.tile(coord), settings));
    }
    if (dabCount != nullptr) {
        *dabCount = dabs.size();
    }
    auto painted = std::make_shared<document::Layer>(layer);
    painted->content = std::make_shared<const document::RasterContent>(
        document::RasterContent{base::RasterId::generate(), std::move(map)});
    return painted;
}

json samplesToJson(std::span<const StrokeSample> samples) {
    json out = json::array();
    for (const StrokeSample &s : samples) {
        out.push_back({s.position.x, s.position.y, static_cast<double>(s.pressure), s.timestampNs});
    }
    return out;
}

base::Result<std::vector<StrokeSample>> samplesFromJson(const json &in) {
    const auto bad = [](std::string detail) {
        return base::fail(ErrorCategory::Validation, "brush.samples", "The stroke's samples are not valid",
                          std::move(detail));
    };
    if (!in.is_array() || in.empty()) {
        return bad("samples must be a non-empty array");
    }
    if (in.size() > kMaxSamples) {
        return bad("more than " + std::to_string(kMaxSamples) + " samples");
    }
    std::vector<StrokeSample> samples;
    samples.reserve(in.size());
    for (const json &entry : in) {
        if (!entry.is_array() || entry.size() < 3 || entry.size() > 4 || !entry[0].is_number() ||
            !entry[1].is_number() || !entry[2].is_number() ||
            (entry.size() == 4 && !entry[3].is_number_integer())) {
            return bad("each sample is [x, y, pressure] or [x, y, pressure, timestampNs]");
        }
        StrokeSample s;
        s.position = {entry[0].get<double>(), entry[1].get<double>()};
        s.pressure = entry[2].get<float>();
        s.timestampNs = entry.size() == 4 ? entry[3].get<std::int64_t>() : 0;
        if (!std::isfinite(s.position.x) || !std::isfinite(s.position.y) || std::abs(s.position.x) > 1e12 ||
            std::abs(s.position.y) > 1e12 || !(s.pressure >= 0.0f && s.pressure <= 1.0f)) {
            return bad("sample coordinates must be finite and pressure between 0 and 1");
        }
        samples.push_back(s);
    }
    return samples;
}

StrokeSession::StrokeSession(std::shared_ptr<const document::DocumentState> base, base::LayerId layer,
                             BrushSettings settings)
    : base_(std::move(base)), preview_(base_), layerId_(layer), settings_(settings), resampler_(settings_),
      content_(base_->findLayer(layer)->content->tiles) {
    layerIndex_ = *base_->indexOf(layer);
}

tiles::DirtyRegion StrokeSession::add(std::span<const StrokeSample> samples) {
    const document::Layer &layer = *base_->layers[layerIndex_];
    std::vector<Dab> dabs;
    for (const StrokeSample &sample : samples) {
        samples_.push_back(sample);
        StrokeSample local = sample;
        local.position = {sample.position.x - static_cast<double>(layer.offset.x),
                          sample.position.y - static_cast<double>(layer.offset.y)};
        resampler_.add(local, dabs);
    }
    tiles::DirtyRegion localDirty;
    for (const Dab &dab : dabs) {
        mask_.addDab(dab, settings_.hardness, localDirty);
    }
    dabCount_ += dabs.size();
    tiles::DirtyRegion documentDirty;
    if (localDirty.empty()) {
        return documentDirty;
    }
    const tiles::TileMap &original = layer.content->tiles;
    for (const tiles::TileCoord coord : localDirty.tiles()) {
        content_.set(coord, applyStrokeToTile(*original.at(coord), *mask_.tile(coord), settings_));
        documentDirty.addPixels(tiles::tilePixelRect(coord).translated(layer.offset.x, layer.offset.y));
    }
    auto painted = std::make_shared<document::Layer>(layer);
    painted->content =
        std::make_shared<const document::RasterContent>(document::RasterContent{layer.content->id, content_});
    auto state = std::make_shared<document::DocumentState>(*base_);
    state->layers[layerIndex_] = std::move(painted);
    preview_ = std::move(state);
    return documentDirty;
}

json StrokeSession::commandParameters() const {
    json parameters = settings_.toJson();
    parameters["samples"] = samplesToJson(samples_);
    return parameters;
}

} // namespace archmage::brush
