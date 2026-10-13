// The authoritative document model (spec §6.1, §6.2; D-15, D-16).
//
// A DocumentState is an immutable snapshot: panels and renderers observe it, commands produce a new one.
// Layers, raster contents and tile maps are shared by pointer between snapshots, so a revision that
// changes one layer copies one Layer and the vector of layer pointers, nothing else. Content identity is
// separate from placement: two layers may share one RasterContent (instances); duplicating the content
// gives it a new RasterId.
//
// Stage 1 consumes raster layers only. Reserved and unknown fields travel in each object's `extensions`
// JSON and survive open and save untouched (spec §6.2, §6.4, §30 retrofit row).
//
// Thread safety: snapshots are immutable once published (held as shared_ptr<const ...>) and safe to read
// from any thread.
#pragma once

#include "archmage/base/Error.h"
#include "archmage/base/Geometry.h"
#include "archmage/base/Id.h"
#include "archmage/color/ColorProfile.h"
#include "archmage/tiles/TileMap.h"

#include <nlohmann/json.hpp>

#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace archmage::document {

/// Spec §6.3 design target for one axis.
inline constexpr std::int64_t kMaxDimension = 300000;

enum class BlendMode : std::uint8_t { Normal };
enum class LengthUnit : std::uint8_t { Pixel, Inch, Millimeter, Centimeter, Point, Pica };

[[nodiscard]] std::string_view blendModeName(BlendMode mode);
[[nodiscard]] std::optional<BlendMode> blendModeFromName(std::string_view name);
[[nodiscard]] std::string_view lengthUnitName(LengthUnit unit);
[[nodiscard]] std::optional<LengthUnit> lengthUnitFromName(std::string_view name);

/// Pixels of a raster layer, stored straight (unassociated, D-09) in layer-local coordinates.
struct RasterContent {
    base::RasterId id;
    tiles::TileMap tiles;
};

struct Layer {
    base::LayerId id;
    std::string name;
    bool visible = true;
    bool locked = false; // full lock: no pixel, property or position edits (spec §13.1 LAY-03)
    float opacity = 1.0f;
    BlendMode blendMode = BlendMode::Normal;
    base::IPoint offset; // document position of the layer-local origin
    std::shared_ptr<const RasterContent> content;
    nlohmann::json extensions = nlohmann::json::object();
};

/// Content of a saved file that this version does not understand, kept so that opening and saving does not
/// discard it (spec §6.2, §10.5): objects of unknown types (and known-type objects reachable only through
/// them), relations other than the layer stack and layer contents, tile planes of those rasters, blobs no
/// understood content uses, asset rows and unknown manifest keys. Written back verbatim; a relation whose
/// endpoint no longer exists is dropped at save time and counted in the save report.
struct PreservedContent {
    struct Object {
        std::string id;
        std::string type;
        int version = 1;
        std::string payload; // JSON text exactly as stored
    };
    struct Relation {
        std::string parent;
        std::string kind;
        std::int64_t ordinal = 0;
        std::string child;
    };
    struct TilePlane {
        std::string raster;
        std::int64_t version = 0;
        std::int64_t tx = 0;
        std::int64_t ty = 0;
        std::int64_t plane = 0;
        std::optional<std::vector<std::byte>> uniform; // one sample, or
        std::string resourceHash;                     // the blob holding the plane
    };
    struct Resource {
        std::string hash; // SHA-256 hex of the raw bytes
        std::string mediaType;
        std::string codec;
        int codecVersion = 0;
        std::uint64_t rawLength = 0;
        std::vector<std::byte> data; // encoded bytes as stored
    };
    struct Asset {
        std::string id;
        std::string kind;
        std::string payload;
    };
    std::vector<Object> objects;
    std::vector<Relation> relations;
    std::vector<TilePlane> tilePlanes;
    std::vector<Resource> resources;
    std::vector<Asset> assets;
    std::vector<std::pair<std::string, std::string>> manifest;

    [[nodiscard]] bool empty() const {
        return objects.empty() && relations.empty() && tilePlanes.empty() && resources.empty() &&
               assets.empty() && manifest.empty();
    }
};

struct DocumentState {
    base::DocumentId id;
    std::string title;
    std::int64_t width = 0;
    std::int64_t height = 0;
    double resolution = 72.0; // pixels per inch (D-29)
    LengthUnit unit = LengthUnit::Pixel;
    tiles::SampleType sampleType = tiles::SampleType::UInt8;
    color::ColorPolicy colorPolicy = color::ColorPolicy::defaultPolicy();
    std::vector<std::shared_ptr<const Layer>> layers; // bottom to top: rendering order
    std::vector<std::string> capabilities;            // required to open the document (spec §10.1)
    nlohmann::json extensions = nlohmann::json::object();
    std::shared_ptr<const PreservedContent> preserved; // null when the file had nothing unknown

    [[nodiscard]] std::optional<std::size_t> indexOf(base::LayerId layer) const;
    [[nodiscard]] const Layer *findLayer(base::LayerId layer) const;
    [[nodiscard]] base::IRect canvasRect() const { return {0, 0, width, height}; }
    [[nodiscard]] tiles::ChannelLayout pixelLayout() const { return tiles::ChannelLayout::rgba(sampleType); }
};

/// Capability every Stage 1 document declares.
inline constexpr std::string_view kRasterLayersCapability = "raster.layers.v1";

/// An empty document (no layers) after validating its parameters.
[[nodiscard]] base::Result<DocumentState> makeDocumentState(std::int64_t width, std::int64_t height,
                                                            tiles::SampleType sampleType,
                                                            color::ColorPolicy policy, std::string title);
/// A transparent raster layer matching the document's pixel layout, with new layer and raster IDs.
[[nodiscard]] std::shared_ptr<const Layer> makeRasterLayer(const DocumentState &document, std::string name);

/// Spec §6.2 invariants: identifiers present and unique, references resolve, values in range, layouts
/// consistent, Stage 1 modes and precisions only.
[[nodiscard]] base::Status validate(const DocumentState &document);

/// One committed document state and how it came to be (spec §6.1 "Revision", §9.2 provenance).
struct Revision {
    base::RevisionId id;
    std::uint64_t sequence = 0; // 1 for a new document, +1 per commit; the "baseRevision" of commands
    base::RevisionId parent;
    std::shared_ptr<const DocumentState> state;
    std::string label;
    base::TransactionId transaction;
    nlohmann::json provenance = nlohmann::json::array(); // command envelopes that produced this revision
};

} // namespace archmage::document
