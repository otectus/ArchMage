// Object payloads and manifest keys of schema 0.1, shared by the writer and the reader. Parsers treat every
// payload as untrusted: a missing field, a wrong type or an out-of-range value is damaged input.
#pragma once

#include "archmage/base/Error.h"
#include "archmage/document/Document.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace archmage::native {

inline constexpr std::string_view kDocumentType = "document";
inline constexpr int kDocumentVersion = 1;
inline constexpr std::string_view kRasterLayerType = "layer.raster";
inline constexpr int kRasterLayerVersion = 1;
inline constexpr std::string_view kRasterType = "raster";
inline constexpr int kRasterVersion = 1;

inline constexpr std::string_view kLayerRelation = "layer";     // document -> layer, ordinal = stack index
inline constexpr std::string_view kContentRelation = "content"; // layer -> raster, ordinal 0

inline constexpr std::string_view kCompositePreview = "composite";
inline constexpr std::string_view kNearestQuality = "nearest";

namespace manifest {
inline constexpr std::string_view kAppId = "app_id";
inline constexpr std::string_view kSchemaMajor = "schema_major";
inline constexpr std::string_view kSchemaMinor = "schema_minor";
inline constexpr std::string_view kRequiredCapabilities = "required_capabilities";
inline constexpr std::string_view kTileSize = "tile_size";
inline constexpr std::string_view kDocumentId = "document_id";
inline constexpr std::string_view kDerivedFrom = "derived_from";
inline constexpr std::string_view kCurrentRevision = "current_revision";
inline constexpr std::string_view kColorPolicy = "color_policy";
inline constexpr std::string_view kWriter = "writer";
inline constexpr std::string_view kSavedAt = "saved_at";
} // namespace manifest

/// Keys this version writes itself; any other manifest key is preserved verbatim.
[[nodiscard]] bool isKnownManifestKey(std::string_view key);

[[nodiscard]] std::string hexOf(std::span<const std::byte> bytes);
[[nodiscard]] std::optional<std::vector<std::byte>> bytesOfHex(std::string_view hex);

[[nodiscard]] nlohmann::json colorPolicyPayload(const color::ColorPolicy &policy, const std::string &profileHash);
[[nodiscard]] nlohmann::json documentPayload(const document::DocumentState &state, const std::string &profileHash);
[[nodiscard]] nlohmann::json layerPayload(const document::Layer &layer);
[[nodiscard]] nlohmann::json rasterPayload(const tiles::TileMap &map);

struct DocumentFields {
    std::string title;
    std::int64_t width = 0;
    std::int64_t height = 0;
    double resolution = 72.0;
    document::LengthUnit unit = document::LengthUnit::Pixel;
    tiles::SampleType sampleType = tiles::SampleType::UInt8;
    std::string profileHash;
    color::BlendPolicy blendPolicy = color::BlendPolicy::Encoded;
    std::vector<std::string> capabilities;
    nlohmann::json extensions = nlohmann::json::object();
};

/// Layer properties; the caller sets the ID and content.
struct RasterFields {
    tiles::ChannelLayout layout{{}, tiles::SampleType::UInt8};
    std::vector<std::vector<std::byte>> defaultSamples; // one sample per plane
};

[[nodiscard]] base::Result<DocumentFields> parseDocument(const nlohmann::json &payload, std::string_view id);
[[nodiscard]] base::Result<document::Layer> parseLayer(const nlohmann::json &payload, std::string_view id);
[[nodiscard]] base::Result<RasterFields> parseRaster(const nlohmann::json &payload, std::string_view id);

/// Damaged-input error naming the file part at fault.
[[nodiscard]] base::Error damaged(std::string code, std::string detail);

} // namespace archmage::native
