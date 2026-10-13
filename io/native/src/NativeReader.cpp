// Reads .archmage files (plan S1-4.4, S1-4.8; spec §10.1, §10.5). Every value read from a file is
// untrusted: identity, schema, references, payloads, tile coordinates and blob hashes are checked before
// they reach the document model, and failures are damaged or unsupported input, never wrong pixels.

#include "archmage/native/Blob.h"
#include "archmage/native/Format.h"
#include "archmage/native/Migration.h"
#include "archmage/native/NativeFile.h"
#include "Payloads.h"
#include "archmage/tiles/ResourceCoordinator.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cstring>
#include <map>
#include <set>
#include <unordered_map>
#include <unordered_set>

namespace archmage::native {
namespace {

using base::ErrorCategory;
using nlohmann::json;

constexpr std::array<std::string_view, 8> kTables{"manifest", "objects",   "relations", "resources",
                                                  "tile_maps", "revisions", "assets",    "previews"};

// Tile coordinates far beyond any canvas yet well inside the document model's offset range (2^40 px).
constexpr std::int64_t kMaxTileCoordinate = std::int64_t{1} << 32;
constexpr std::int64_t kMaxPreviewEdge = 4096;
constexpr std::size_t kMaxProblems = 50;

base::Unexpected<base::Error> fail(base::Error error) { return base::Unexpected<base::Error>(std::move(error)); }

std::optional<std::int64_t> parseInteger(std::string_view text) {
    std::int64_t value = 0;
    const auto [end, ec] = std::from_chars(text.data(), text.data() + text.size(), value);
    if (ec != std::errc() || end != text.data() + text.size()) {
        return std::nullopt;
    }
    return value;
}

std::string versionText(int major, int minor) { return std::to_string(major) + "." + std::to_string(minor); }

// Runs `statement` to completion, calling `row` for each result row.
template <class Row> base::Status forEachRow(Statement &statement, Row &&row) {
    while (statement.step()) {
        if (auto status = row(statement); !status) {
            return status;
        }
    }
    return statement.status();
}

base::Result<Statement> prepare(Database &db, std::string_view sql) { return db.prepare(sql); }

// Opens the file read-only and checks that it is an ArchMage document. The manifest and previews tables are
// the contract every schema major keeps, so a newer major can still be identified and previewed.
base::Result<Database> openFile(const std::filesystem::path &path) {
    std::error_code ec;
    const auto status = std::filesystem::status(path, ec);
    if (ec || !std::filesystem::exists(status)) {
        return base::fail(ErrorCategory::PermissionDenied, "file.not_found", "The file does not exist",
                          path.string());
    }
    if (!std::filesystem::is_regular_file(status)) {
        return base::fail(ErrorCategory::UnsupportedInput, "file.not_regular", "The path is not a file",
                          path.string());
    }
    auto db = Database::open(path, Database::Mode::ReadOnly);
    if (!db) {
        return db;
    }
    auto id = prepare(*db, "PRAGMA application_id");
    if (!id) {
        return fail(id.error());
    }
    const bool row = id->step();
    if (auto checked = id->status(); !checked) {
        return fail(checked.error());
    }
    if (!row || id->integer(0) != kApplicationId) {
        return base::fail(ErrorCategory::UnsupportedInput, "file.not_archmage", "The file is not an ArchMage document",
                          path.string());
    }
    return db;
}

// The eight tables must be real tables: a view of the same name could run arbitrary SQL on read.
base::Status checkTables(Database &db) {
    auto statement = prepare(db, "SELECT name, type FROM sqlite_schema WHERE type IN ('table', 'view')");
    if (!statement) {
        return fail(statement.error());
    }
    std::map<std::string, std::string> found;
    if (auto status = forEachRow(*statement, [&](Statement &s) -> base::Status {
            found[s.text(0)] = s.text(1);
            return {};
        });
        !status) {
        return status;
    }
    for (const std::string_view table : kTables) {
        const auto it = found.find(std::string(table));
        if (it == found.end() || it->second != "table") {
            return fail(damaged("file.schema_missing", "table '" + std::string(table) + "' is missing"));
        }
    }
    return {};
}

base::Result<std::vector<std::pair<std::string, std::string>>> loadManifest(Database &db) {
    auto statement = prepare(db, "SELECT key, value FROM manifest ORDER BY key");
    if (!statement) {
        return fail(statement.error());
    }
    std::vector<std::pair<std::string, std::string>> entries;
    if (auto status = forEachRow(*statement, [&](Statement &s) -> base::Status {
            entries.emplace_back(s.text(0), s.text(1));
            return {};
        });
        !status) {
        return fail(status.error());
    }
    return entries;
}

const std::string *findKey(const std::vector<std::pair<std::string, std::string>> &manifest, std::string_view key) {
    for (const auto &[k, v] : manifest) {
        if (k == key) {
            return &v;
        }
    }
    return nullptr;
}

base::Result<FileInfo> parseInfo(const std::vector<std::pair<std::string, std::string>> &manifest) {
    FileInfo info;
    const std::string *app = findKey(manifest, manifest::kAppId);
    const std::string *majorText = findKey(manifest, manifest::kSchemaMajor);
    const std::string *minorText = findKey(manifest, manifest::kSchemaMinor);
    if (app == nullptr || *app != kApplicationIdText || majorText == nullptr || minorText == nullptr) {
        return fail(damaged("file.invalid_manifest", "application or schema version missing"));
    }
    const auto major = parseInteger(*majorText);
    const auto minor = parseInteger(*minorText);
    if (!major || !minor || *major < 0 || *minor < 0 || *major > 9999 || *minor > 9999) {
        return fail(damaged("file.invalid_manifest", "schema version '" + *majorText + "." + *minorText + "'"));
    }
    info.schemaMajor = static_cast<int>(*major);
    info.schemaMinor = static_cast<int>(*minor);
    if (const std::string *writer = findKey(manifest, manifest::kWriter)) {
        info.writer = *writer;
    }
    if (const std::string *savedAt = findKey(manifest, manifest::kSavedAt)) {
        info.savedAt = *savedAt;
    }
    if (info.schemaMajor > kSchemaMajor) {
        // A newer major may have changed every other key; only identify the file and show its preview.
        info.readOnly = true;
        info.readOnlyReason = "schema.newer_major";
        info.readOnlyExplanation = "This document was saved by a newer version of ArchMage (file format " +
                                   versionText(info.schemaMajor, info.schemaMinor) +
                                   "). It is shown as a read-only preview; open it in a newer version to edit it.";
        if (const std::string *id = findKey(manifest, manifest::kDocumentId)) {
            if (auto parsed = base::DocumentId::parse(*id)) {
                info.documentId = *parsed;
            }
        }
        return info;
    }
    if (info.schemaMajor < kSchemaMajor) {
        return base::fail(ErrorCategory::UnsupportedInput, "schema.too_old",
                          "This document's file format is no longer supported",
                          versionText(info.schemaMajor, info.schemaMinor));
    }

    const std::string *capabilities = findKey(manifest, manifest::kRequiredCapabilities);
    const std::string *documentId = findKey(manifest, manifest::kDocumentId);
    const std::string *revision = findKey(manifest, manifest::kCurrentRevision);
    const std::string *tileSize = findKey(manifest, manifest::kTileSize);
    if (capabilities == nullptr || documentId == nullptr || revision == nullptr || tileSize == nullptr ||
        revision->empty()) {
        return fail(damaged("file.invalid_manifest", "a required key is missing"));
    }
    const json capabilityList = json::parse(*capabilities, nullptr, false);
    if (!capabilityList.is_array()) {
        return fail(damaged("file.invalid_manifest", "required_capabilities is not a list"));
    }
    for (const json &capability : capabilityList) {
        if (!capability.is_string()) {
            return fail(damaged("file.invalid_manifest", "required_capabilities holds a non-string"));
        }
        info.capabilities.push_back(capability.get<std::string>());
    }
    auto id = base::DocumentId::parse(*documentId);
    if (!id) {
        return fail(damaged("file.invalid_manifest", "document_id '" + *documentId + "'"));
    }
    info.documentId = *id;
    if (const std::string *derived = findKey(manifest, manifest::kDerivedFrom)) {
        auto parsed = base::DocumentId::parse(*derived);
        if (!parsed) {
            return fail(damaged("file.invalid_manifest", "derived_from '" + *derived + "'"));
        }
        info.derivedFrom = *parsed;
    }
    info.revisionId = *revision;
    if (*tileSize != std::to_string(tiles::kTileSize)) {
        return base::fail(ErrorCategory::UnsupportedInput, "file.unsupported_tile_size",
                          "The document uses a tile size this version does not support", *tileSize);
    }

    std::vector<std::string> unknown;
    for (const std::string &capability : info.capabilities) {
        if (std::find(knownCapabilities().begin(), knownCapabilities().end(), capability) == knownCapabilities().end()) {
            unknown.push_back(capability);
        }
    }
    if (!unknown.empty()) {
        std::string list;
        for (const std::string &capability : unknown) {
            list += (list.empty() ? "" : ", ") + capability;
        }
        info.readOnly = true;
        info.readOnlyReason = "capability.unknown";
        info.readOnlyExplanation = "This document uses features this version of ArchMage does not support (" + list +
                                   "). It is shown as a read-only preview.";
    } else if (info.schemaMinor > kSchemaMinor) {
        info.readOnly = true;
        info.readOnlyReason = "schema.newer_minor";
        info.readOnlyExplanation = "This document was saved by a newer version of ArchMage (file format " +
                                   versionText(info.schemaMajor, info.schemaMinor) +
                                   "). It opens read-only so that saving cannot discard newer content.";
    }
    return info;
}

base::Status loadRevision(Database &db, FileInfo &info) {
    auto statement = prepare(db, "SELECT sequence, label FROM revisions WHERE id = ?");
    if (!statement) {
        return fail(statement.error());
    }
    statement->bind(1, info.revisionId);
    const bool found = statement->step();
    if (auto status = statement->status(); !status) {
        return status;
    }
    if (!found || statement->integer(0) < 1) {
        return fail(damaged("file.missing_revision", "current revision " + info.revisionId));
    }
    info.revisionSequence = static_cast<std::uint64_t>(statement->integer(0));
    info.revisionLabel = statement->text(1);
    return {};
}

base::Result<Blob> loadResource(Statement &query, const std::string &hash) {
    query.reset();
    query.bind(1, hash);
    const bool found = query.step();
    if (auto status = query.status(); !status) {
        return fail(status.error());
    }
    if (!found) {
        return fail(damaged("file.missing_resource", "resource " + hash.substr(0, 16)));
    }
    if (query.integer(4) < 0) {
        return fail(damaged("file.invalid_resource", "resource " + hash.substr(0, 16) + ": negative length"));
    }
    Blob blob{hash,
              query.text(0),
              query.text(1),
              static_cast<int>(query.integer(2)),
              static_cast<std::uint64_t>(query.integer(4)),
              query.blob(3)};
    return blob;
}

constexpr std::string_view kResourceQuery =
    "SELECT media_type, codec, codec_version, data, raw_length FROM resources WHERE hash = ?";

// The stored composite preview, preferring the current revision's. A damaged preview is absent rather
// than an error: it is a cache.
void loadPreview(Database &db, const FileInfo &info, OpenedDocument &opened) {
    auto statement = prepare(db, "SELECT width, height, media_type, resource_hash, profile FROM previews "
                                 "WHERE kind = 'composite' ORDER BY revision = ? DESC LIMIT 1");
    auto resources = prepare(db, kResourceQuery);
    if (!statement || !resources) {
        return;
    }
    statement->bind(1, info.revisionId);
    if (!statement->step()) {
        return;
    }
    const std::int64_t width = statement->integer(0);
    const std::int64_t height = statement->integer(1);
    if (width < 1 || height < 1 || width > kMaxPreviewEdge || height > kMaxPreviewEdge ||
        statement->text(2) != kPreviewMedia) {
        return;
    }
    auto blob = loadResource(*resources, statement->text(3));
    auto profileBlob = loadResource(*resources, statement->text(4));
    if (!blob || !profileBlob) {
        return;
    }
    auto pixels = decodeBlob(*blob);
    auto icc = decodeBlob(*profileBlob);
    if (!pixels || !icc || pixels->size() != static_cast<std::size_t>(width * height * 4)) {
        return;
    }
    auto profile = color::ColorProfile::fromIcc(*icc);
    if (!profile) {
        return;
    }
    opened.preview = Preview{width, height, std::move(*pixels)};
    opened.previewProfile = std::move(*profile);
}

struct ObjectRow {
    std::string type;
    int version = 0;
    std::string payload;
};

// Builds a document state from the tables; collects what it does not understand into `preserved`.
class StateLoader {
public:
    using PreservedRelation = document::PreservedContent::Relation;

    StateLoader(Database &db, const FileInfo &info) : db_(db), info_(info) {}

    base::Result<std::shared_ptr<const document::DocumentState>> load() {
        if (auto status = loadObjects(); !status) {
            return fail(status.error());
        }
        if (auto status = loadRelations(); !status) {
            return fail(status.error());
        }
        auto resources = prepare(db_, kResourceQuery);
        if (!resources) {
            return fail(resources.error());
        }
        resources_.emplace(std::move(*resources));

        auto state = std::make_shared<document::DocumentState>();
        if (auto status = loadDocument(*state); !status) {
            return fail(status.error());
        }
        if (auto status = loadLayers(*state); !status) {
            return fail(status.error());
        }
        if (auto status = preserveRest(); !status) {
            return fail(status.error());
        }
        if (!preserved_.empty()) {
            state->preserved = std::make_shared<const document::PreservedContent>(std::move(preserved_));
        }
        if (auto valid = document::validate(*state); !valid) {
            base::Error error = valid.error();
            if (error.category == ErrorCategory::Validation) {
                error = damaged("file.invalid_document", base::format(error));
            }
            return fail(std::move(error));
        }
        return std::shared_ptr<const document::DocumentState>(std::move(state));
    }

private:
    struct PlaneRow {
        std::optional<std::vector<std::byte>> uniform;
        std::string hash;
    };

    base::Status loadObjects() {
        auto statement = prepare(db_, "SELECT id, type, version, payload FROM objects ORDER BY id");
        if (!statement) {
            return fail(statement.error());
        }
        return forEachRow(*statement, [&](Statement &s) -> base::Status {
            const std::int64_t version = s.integer(2);
            if (version < 1 || version > 1'000'000) {
                return fail(damaged("file.invalid_object", s.text(0) + ": version " + std::to_string(version)));
            }
            objects_.emplace(s.text(0), ObjectRow{s.text(1), static_cast<int>(version), s.text(3)});
            return {};
        });
    }

    base::Status loadRelations() {
        auto statement = prepare(db_, "SELECT parent, kind, ordinal, child FROM relations ORDER BY parent, kind, "
                                      "ordinal");
        if (!statement) {
            return fail(statement.error());
        }
        return forEachRow(*statement, [&](Statement &s) -> base::Status {
            relations_.push_back({s.text(0), s.text(1), s.integer(2), s.text(3)});
            return {};
        });
    }

    base::Result<json> knownPayload(const std::string &id, std::string_view type, int supported) {
        const auto it = objects_.find(id);
        if (it == objects_.end()) {
            return fail(damaged("file.dangling_reference", "object " + id + " is missing"));
        }
        if (it->second.type != type) {
            return base::fail(ErrorCategory::UnsupportedInput, "file.unsupported_object",
                              "The document contains content this version does not support",
                              id + " is a " + it->second.type + ", expected " + std::string(type));
        }
        if (it->second.version > supported) {
            return base::fail(ErrorCategory::UnsupportedInput, "file.object_version",
                              "The document contains content saved by a newer version",
                              id + ": " + it->second.type + " v" + std::to_string(it->second.version));
        }
        json payload = json::parse(it->second.payload, nullptr, false);
        if (payload.is_discarded()) {
            return fail(damaged("file.invalid_object", id + ": payload is not JSON"));
        }
        understood_.insert(id);
        return payload;
    }

    base::Status loadDocument(document::DocumentState &state) {
        const std::string id = info_.documentId.toString();
        auto payload = knownPayload(id, kDocumentType, kDocumentVersion);
        if (!payload) {
            return fail(payload.error());
        }
        auto fields = parseDocument(*payload, id);
        if (!fields) {
            return fail(fields.error());
        }
        auto icc = loadResource(*resources_, fields->profileHash);
        if (!icc) {
            return fail(icc.error());
        }
        auto bytes = decodeBlob(*icc);
        if (!bytes) {
            return fail(bytes.error());
        }
        auto profile = color::ColorProfile::fromIcc(*bytes);
        if (!profile) {
            return fail(profile.error());
        }
        auto policy = color::ColorPolicy::make(std::move(*profile), fields->blendPolicy);
        if (!policy) {
            return fail(policy.error());
        }
        understoodResources_.insert(fields->profileHash);
        state.id = info_.documentId;
        state.title = std::move(fields->title);
        state.width = fields->width;
        state.height = fields->height;
        state.resolution = fields->resolution;
        state.unit = fields->unit;
        state.sampleType = fields->sampleType;
        state.colorPolicy = std::move(*policy);
        state.capabilities = std::move(fields->capabilities);
        state.extensions = std::move(fields->extensions);
        return {};
    }

    base::Status loadLayers(document::DocumentState &state) {
        const std::string documentId = info_.documentId.toString();
        std::vector<const PreservedRelation *> stack;
        std::unordered_map<std::string, const PreservedRelation *> contents;
        for (const PreservedRelation &relation : relations_) {
            if (relation.parent == documentId && relation.kind == kLayerRelation) {
                stack.push_back(&relation);
            }
        }
        std::set<std::string> layerIds;
        for (const PreservedRelation *entry : stack) {
            if (!layerIds.insert(entry->child).second) {
                return fail(damaged("file.invalid_relation", "layer " + entry->child + " appears twice"));
            }
            const auto object = objects_.find(entry->child);
            if (object != objects_.end() && object->second.type == kRasterLayerType) {
                understoodRelations_.insert(entry);
            }
        }
        for (const PreservedRelation &relation : relations_) {
            if (relation.kind == kContentRelation && layerIds.contains(relation.parent)) {
                if (relation.ordinal != 0 || !contents.emplace(relation.parent, &relation).second) {
                    return fail(damaged("file.invalid_relation", "layer " + relation.parent + " content"));
                }
                understoodRelations_.insert(&relation);
            }
        }

        std::unordered_map<std::string, std::shared_ptr<const document::RasterContent>> rasters;
        for (const PreservedRelation *entry : stack) {
            auto payload = knownPayload(entry->child, kRasterLayerType, kRasterLayerVersion);
            if (!payload) {
                return fail(payload.error());
            }
            auto parsed = parseLayer(*payload, entry->child);
            auto layerId = base::LayerId::parse(entry->child);
            if (!parsed) {
                return fail(parsed.error());
            }
            if (!layerId) {
                return fail(damaged("file.invalid_object", "layer id " + entry->child));
            }
            const auto content = contents.find(entry->child);
            if (content == contents.end()) {
                return fail(damaged("file.dangling_reference", "layer " + entry->child + " has no content"));
            }
            const std::string &rasterId = content->second->child;
            auto &raster = rasters[rasterId];
            if (!raster) {
                auto loaded = loadRaster(rasterId);
                if (!loaded) {
                    return fail(loaded.error());
                }
                raster = std::move(*loaded);
            }
            auto layer = std::make_shared<document::Layer>(std::move(*parsed));
            layer->id = *layerId;
            layer->content = raster;
            state.layers.push_back(std::move(layer));
        }
        return {};
    }

    base::Result<std::shared_ptr<const document::RasterContent>> loadRaster(const std::string &id) {
        auto payload = knownPayload(id, kRasterType, kRasterVersion);
        if (!payload) {
            return fail(payload.error());
        }
        auto fields = parseRaster(*payload, id);
        auto rasterId = base::RasterId::parse(id);
        if (!fields) {
            return fail(fields.error());
        }
        if (!rasterId) {
            return fail(damaged("file.invalid_object", "raster id " + id));
        }
        const tiles::ChannelLayout &layout = fields->layout;
        const std::size_t sampleBytes = layout.sampleBytes();
        const std::size_t planes = layout.planeCount();
        std::vector<std::byte> defaults;
        for (const auto &sample : fields->defaultSamples) {
            defaults.insert(defaults.end(), sample.begin(), sample.end());
        }

        // Refuse up front what cannot fit the RAM budget rather than failing halfway (D-26).
        auto count = prepare(db_, "SELECT COUNT(*) FROM tile_maps WHERE raster_id = ? AND version = 0 AND "
                                  "resource_hash IS NOT NULL");
        if (!count) {
            return fail(count.error());
        }
        count->bind(1, id);
        count->step();
        if (auto status = count->status(); !status) {
            return fail(status.error());
        }
        const auto denseBytes = static_cast<std::uint64_t>(count->integer(0)) * layout.planeBytes();
        if (!tiles::ResourceCoordinator::global().makeRoom(tiles::Budget::Ram, denseBytes)) {
            return base::fail(ErrorCategory::ResourceExhausted, "file.too_large",
                              "The document does not fit in the memory available to ArchMage",
                              std::to_string(denseBytes) + " bytes of tiles");
        }

        auto content = std::make_shared<document::RasterContent>(document::RasterContent{
            *rasterId, tiles::TileMap(layout, tiles::Tile::makeUniform(layout.sampleType(), planes, defaults))});
        auto rows = prepare(db_, "SELECT tx, ty, plane, uniform, resource_hash FROM tile_maps WHERE raster_id = ? "
                                 "AND version = 0 ORDER BY ty, tx, plane");
        if (!rows) {
            return fail(rows.error());
        }
        rows->bind(1, id);

        std::optional<tiles::TileCoord> current;
        std::vector<PlaneRow> pending;
        const auto finishTile = [&]() -> base::Status {
            if (!current) {
                return {};
            }
            if (pending.size() != planes) {
                return fail(damaged("file.invalid_tile", id + " tile " + std::to_string(current->tx) + "," +
                                                             std::to_string(current->ty) + ": plane count"));
            }
            auto tile = buildTile(id, *current, layout, pending);
            if (!tile) {
                return fail(tile.error());
            }
            content->tiles.set(*current, std::move(*tile));
            pending.clear();
            return {};
        };
        auto status = forEachRow(*rows, [&](Statement &s) -> base::Status {
            const tiles::TileCoord coord{s.integer(0), s.integer(1)};
            if (coord.tx < -kMaxTileCoordinate || coord.tx > kMaxTileCoordinate || coord.ty < -kMaxTileCoordinate ||
                coord.ty > kMaxTileCoordinate) {
                return fail(damaged("file.invalid_tile", id + ": tile coordinate out of range"));
            }
            if (!current || *current != coord) {
                if (auto finished = finishTile(); !finished) {
                    return finished;
                }
                current = coord;
            }
            if (s.integer(2) != static_cast<std::int64_t>(pending.size())) {
                return fail(damaged("file.invalid_tile", id + ": planes out of order"));
            }
            PlaneRow plane;
            if (s.isNull(4)) {
                plane.uniform = s.blob(3);
                if (plane.uniform->size() != sampleBytes) {
                    return fail(damaged("file.invalid_tile", id + ": uniform sample size"));
                }
            } else {
                plane.hash = s.text(4);
            }
            pending.push_back(std::move(plane));
            return {};
        });
        if (status) {
            status = finishTile();
        }
        if (!status) {
            return fail(status.error());
        }
        loadedRasters_.insert(id);
        return std::shared_ptr<const document::RasterContent>(std::move(content));
    }

    base::Result<std::shared_ptr<const tiles::Tile>> buildTile(const std::string &raster, tiles::TileCoord coord,
                                                                const tiles::ChannelLayout &layout,
                                                                const std::vector<PlaneRow> &planes) {
        const std::size_t sampleBytes = layout.sampleBytes();
        const bool uniform =
            std::all_of(planes.begin(), planes.end(), [](const PlaneRow &plane) { return plane.uniform.has_value(); });
        if (uniform) {
            std::vector<std::byte> value;
            for (const PlaneRow &plane : planes) {
                value.insert(value.end(), plane.uniform->begin(), plane.uniform->end());
            }
            return tiles::Tile::makeUniform(layout.sampleType(), planes.size(), value);
        }
        tiles::TileBuffer buffer(layout.sampleType(), planes.size());
        for (std::size_t p = 0; p < planes.size(); ++p) {
            const std::span<std::byte> target = buffer.plane(p);
            if (planes[p].uniform) {
                for (std::size_t i = 0; i < target.size(); i += sampleBytes) {
                    std::memcpy(target.data() + i, planes[p].uniform->data(), sampleBytes);
                }
                continue;
            }
            auto blob = loadResource(*resources_, planes[p].hash);
            if (!blob) {
                return fail(blob.error());
            }
            if (blob->mediaType != kTilePlaneMedia || blob->rawLength != layout.planeBytes()) {
                return fail(damaged("file.invalid_tile", raster + " tile " + std::to_string(coord.tx) + "," +
                                                             std::to_string(coord.ty) + ": plane resource"));
            }
            auto bytes = decodeBlob(*blob);
            if (!bytes) {
                return fail(bytes.error());
            }
            std::memcpy(target.data(), bytes->data(), target.size());
            understoodResources_.insert(planes[p].hash);
        }
        return std::move(buffer).seal();
    }

    // Everything not understood above goes into preserved content, to be written back on save.
    base::Status preserveRest() {
        for (const auto &[id, object] : objects_) {
            if (!understood_.contains(id)) {
                preserved_.objects.push_back({id, object.type, object.version, object.payload});
            }
        }
        for (const PreservedRelation &relation : relations_) {
            if (!understoodRelations_.contains(&relation)) {
                preserved_.relations.push_back(relation);
            }
        }
        std::unordered_set<std::string> preservedHashes; // blobs preserved planes use, shared or not
        auto planes = prepare(db_, "SELECT raster_id, version, tx, ty, plane, uniform, resource_hash FROM tile_maps "
                                   "ORDER BY raster_id, version, ty, tx, plane");
        if (!planes) {
            return fail(planes.error());
        }
        if (auto status = forEachRow(*planes, [&](Statement &s) -> base::Status {
                if (loadedRasters_.contains(s.text(0)) && s.integer(1) == 0) {
                    return {};
                }
                document::PreservedContent::TilePlane plane{s.text(0), s.integer(1), s.integer(2), s.integer(3),
                                                           s.integer(4), std::nullopt, {}};
                if (s.isNull(6)) {
                    plane.uniform = s.blob(5);
                } else {
                    plane.resourceHash = s.text(6);
                    preservedHashes.insert(plane.resourceHash);
                }
                preserved_.tilePlanes.push_back(std::move(plane));
                return {};
            });
            !status) {
            return status;
        }
        // Previews are regenerated on save; their blobs are not preserved.
        auto previews = prepare(db_, "SELECT resource_hash FROM previews");
        if (!previews) {
            return fail(previews.error());
        }
        if (auto status = forEachRow(*previews, [&](Statement &s) -> base::Status {
                understoodResources_.insert(s.text(0));
                return {};
            });
            !status) {
            return status;
        }
        auto resources = prepare(db_, "SELECT hash, media_type, codec, codec_version, raw_length, data FROM resources "
                                      "ORDER BY hash");
        if (!resources) {
            return fail(resources.error());
        }
        if (auto status = forEachRow(*resources, [&](Statement &s) -> base::Status {
                if (understoodResources_.contains(s.text(0)) && !preservedHashes.contains(s.text(0))) {
                    return {};
                }
                if (s.integer(4) < 0) {
                    return fail(damaged("file.invalid_resource", s.text(0).substr(0, 16) + ": negative length"));
                }
                preserved_.resources.push_back({s.text(0), s.text(1), s.text(2), static_cast<int>(s.integer(3)),
                                                static_cast<std::uint64_t>(s.integer(4)), s.blob(5)});
                return {};
            });
            !status) {
            return status;
        }
        auto assets = prepare(db_, "SELECT id, kind, payload FROM assets ORDER BY id");
        if (!assets) {
            return fail(assets.error());
        }
        if (auto status = forEachRow(*assets, [&](Statement &s) -> base::Status {
                preserved_.assets.push_back({s.text(0), s.text(1), s.text(2)});
                return {};
            });
            !status) {
            return status;
        }
        auto manifest = loadManifest(db_);
        if (!manifest) {
            return fail(manifest.error());
        }
        for (auto &[key, value] : *manifest) {
            if (!isKnownManifestKey(key)) {
                preserved_.manifest.emplace_back(std::move(key), std::move(value));
            }
        }
        return {};
    }

    Database &db_;
    const FileInfo &info_;
    std::map<std::string, ObjectRow> objects_;
    std::vector<PreservedRelation> relations_;
    std::optional<Statement> resources_;
    std::unordered_set<std::string> understood_;
    std::unordered_set<const PreservedRelation *> understoodRelations_;
    std::unordered_set<std::string> understoodResources_;
    std::unordered_set<std::string> loadedRasters_;
    document::PreservedContent preserved_;
};

// Opens, identifies and, for a current-major file, checks tables and the revision; migrates an older minor
// into an in-memory copy. `source` is the database to read content from.
struct OpenFile {
    Database file;
    std::optional<Database> migrated;
    FileInfo info;

    Database &source() { return migrated ? *migrated : file; }
};

base::Result<OpenFile> openAndIdentify(const std::filesystem::path &path) {
    auto db = openFile(path);
    if (!db) {
        return fail(db.error());
    }
    auto manifest = loadManifest(*db);
    if (!manifest) {
        return fail(manifest.error());
    }
    auto info = parseInfo(*manifest);
    if (!info) {
        return fail(info.error());
    }
    OpenFile open{std::move(*db), std::nullopt, std::move(*info)};
    if (open.info.schemaMajor > kSchemaMajor) {
        return open;
    }
    if (auto status = checkTables(open.file); !status) {
        return fail(status.error());
    }
    if (auto status = loadRevision(open.file, open.info); !status) {
        return fail(status.error());
    }
    if (open.info.schemaMinor < kSchemaMinor) {
        auto memory = Database::openMemory();
        if (!memory) {
            return fail(memory.error());
        }
        if (auto status = open.file.copyTo(*memory); !status) {
            return fail(status.error());
        }
        if (auto status = migrate(*memory, open.info.schemaMajor, open.info.schemaMinor); !status) {
            return fail(status.error());
        }
        open.migrated.emplace(std::move(*memory));
    }
    return open;
}

} // namespace

const std::vector<std::string> &knownCapabilities() {
    static const std::vector<std::string> kKnown{std::string(document::kRasterLayersCapability)};
    return kKnown;
}

base::Result<FileInfo> readFileInfo(const std::filesystem::path &path) {
    auto open = openAndIdentify(path);
    if (!open) {
        return fail(open.error());
    }
    return std::move(open->info);
}

base::Result<OpenedDocument> readDocument(const std::filesystem::path &path, const ReadOptions &options) {
    auto open = openAndIdentify(path);
    if (!open) {
        return fail(open.error());
    }
    OpenedDocument opened;
    opened.info = open->info;
    const bool understood = open->info.schemaMajor <= kSchemaMajor && open->info.readOnlyReason != "capability.unknown";
    if (understood) {
        auto state = StateLoader(open->source(), open->info).load();
        if (state) {
            opened.state = std::move(*state);
        } else if (!open->info.readOnly) {
            return fail(state.error());
        }
        // A newer minor that does not load is still shown as a preview.
    }
    if (options.loadPreview) {
        loadPreview(open->source(), open->info, opened);
    }
    return opened;
}

base::Result<ValidationReport> validateFile(const std::filesystem::path &path, ValidationLevel level) {
    const bool deep = level == ValidationLevel::Deep;
    ValidationReport report;
    const auto examinable = [](const base::Error &error) {
        return error.category == ErrorCategory::DamagedInput || error.category == ErrorCategory::UnsupportedInput;
    };
    auto open = openAndIdentify(path);
    if (!open) {
        if (!examinable(open.error())) {
            return fail(open.error());
        }
        report.problems.push_back(open.error());
        return report;
    }
    report.info = open->info;
    Database &db = open->source();
    if (open->info.schemaMajor > kSchemaMajor) {
        report.notes.push_back("file format " + versionText(open->info.schemaMajor, open->info.schemaMinor) +
                               " is newer than this version: only identity was checked");
        return report;
    }
    const auto problem = [&](base::Error error) {
        if (report.problems.size() < kMaxProblems) {
            report.problems.push_back(std::move(error));
        }
    };
    const auto query = [&](std::string_view sql, auto &&row) {
        auto statement = prepare(db, sql);
        if (!statement) {
            problem(statement.error());
            return;
        }
        if (auto status = forEachRow(*statement, [&](Statement &s) -> base::Status {
                row(s);
                return {};
            });
            !status) {
            problem(status.error());
        }
    };

    query(deep ? "PRAGMA integrity_check(50)" : "PRAGMA quick_check(50)", [&](Statement &s) {
        if (s.text(0) != "ok") {
            problem(damaged("file.integrity", s.text(0)));
        }
    });
    query("SELECT (SELECT COUNT(*) FROM objects), (SELECT COUNT(*) FROM relations), (SELECT COUNT(*) FROM "
          "resources), (SELECT COUNT(*) FROM tile_maps)",
          [&](Statement &s) {
              report.objects = static_cast<std::size_t>(s.integer(0));
              report.relations = static_cast<std::size_t>(s.integer(1));
              report.resources = static_cast<std::size_t>(s.integer(2));
              report.tilePlanes = static_cast<std::size_t>(s.integer(3));
          });
    query("SELECT parent, kind, ordinal, child FROM relations WHERE parent NOT IN (SELECT id FROM objects) OR "
          "child NOT IN (SELECT id FROM objects) LIMIT 50",
          [&](Statement &s) {
              problem(damaged("file.dangling_reference", "relation " + s.text(0) + " -" + s.text(1) + "[" +
                                                             std::to_string(s.integer(2)) + "]-> " + s.text(3)));
          });
    query("SELECT DISTINCT raster_id FROM tile_maps WHERE raster_id NOT IN (SELECT id FROM objects WHERE type = "
          "'raster') LIMIT 50",
          [&](Statement &s) { problem(damaged("file.dangling_reference", "tiles of missing raster " + s.text(0))); });
    query("SELECT raster_id, tx, ty, plane, resource_hash FROM tile_maps WHERE resource_hash IS NOT NULL AND "
          "resource_hash NOT IN (SELECT hash FROM resources) LIMIT 50",
          [&](Statement &s) {
              problem(damaged("file.missing_resource", "tile " + s.text(0) + " " + std::to_string(s.integer(1)) +
                                                           "," + std::to_string(s.integer(2)) + " plane " +
                                                           std::to_string(s.integer(3))));
          });
    query("SELECT kind, revision FROM previews WHERE resource_hash NOT IN (SELECT hash FROM resources) OR profile "
          "NOT IN (SELECT hash FROM resources) LIMIT 50",
          [&](Statement &s) { problem(damaged("file.missing_resource", "preview " + s.text(0) + " " + s.text(1))); });
    query("SELECT id FROM revisions WHERE root NOT IN (SELECT id FROM objects) LIMIT 50",
          [&](Statement &s) { problem(damaged("file.dangling_reference", "revision root of " + s.text(0))); });

    if (open->info.readOnlyReason == "capability.unknown") {
        report.notes.push_back("the document uses capabilities this version does not know: its content was not "
                               "loaded");
    } else if (level == ValidationLevel::Quick) {
        report.notes.push_back("payloads and tiles were not loaded (quick validation)");
    } else if (report.problems.empty()) {
        // Loading checks every understood payload, reference and tile plane, and verifies the hash of every
        // tile blob it decodes.
        auto state = StateLoader(db, open->info).load();
        if (!state) {
            problem(state.error());
        }
    }
    if (deep && report.problems.empty()) {
        query("SELECT hash, media_type, codec, codec_version, raw_length, data FROM resources ORDER BY hash",
              [&](Statement &s) {
                  if (s.integer(4) < 0) {
                      problem(damaged("file.invalid_resource", s.text(0).substr(0, 16) + ": negative length"));
                      return;
                  }
                  const Blob blob{s.text(0), s.text(1), s.text(2), static_cast<int>(s.integer(3)),
                                  static_cast<std::uint64_t>(s.integer(4)), s.blob(5)};
                  if (auto decoded = decodeBlob(blob); !decoded) {
                      problem(decoded.error());
                  } else {
                      ++report.resourcesVerified;
                  }
              });
    } else if (level == ValidationLevel::Standard) {
        report.notes.push_back("resources outside the document's tiles were not decoded: use --deep");
    }
    return report;
}

base::Result<json> inspectFile(const std::filesystem::path &path) {
    auto open = openAndIdentify(path);
    if (!open) {
        return fail(open.error());
    }
    const FileInfo &info = open->info;
    Database &db = open->file;
    json out{{"path", path.string()},
             {"schema", {{"major", info.schemaMajor}, {"minor", info.schemaMinor}}},
             {"readOnly", info.readOnly}};
    if (info.readOnly) {
        out["readOnlyReason"] = info.readOnlyReason;
        out["readOnlyExplanation"] = info.readOnlyExplanation;
    }
    auto manifest = loadManifest(db);
    if (!manifest) {
        return fail(manifest.error());
    }
    json manifestJson = json::object();
    for (const auto &[key, value] : *manifest) {
        manifestJson[key] = value;
    }
    out["manifest"] = manifestJson;
    if (info.schemaMajor > kSchemaMajor) {
        return out;
    }

    const auto run = [&](std::string_view sql, auto &&row) -> base::Status {
        auto statement = prepare(db, sql);
        if (!statement) {
            return fail(statement.error());
        }
        return forEachRow(*statement, [&](Statement &s) -> base::Status {
            row(s);
            return {};
        });
    };
    json objects = json::array();
    json types = json::object();
    base::Status status = run("SELECT id, type, version, length(payload) FROM objects ORDER BY type, id", [&](Statement &s) {
        objects.push_back({{"id", s.text(0)}, {"type", s.text(1)}, {"version", s.integer(2)}, {"payloadBytes", s.integer(3)}});
        types[s.text(1)] = types.value(s.text(1), 0) + 1;
    });
    out["objects"] = objects;
    out["objectTypes"] = types;
    json layers = json::array();
    if (status) {
        status = run("SELECT r.ordinal, o.id, o.type, o.payload FROM relations r JOIN objects o ON o.id = r.child "
                     "WHERE r.parent = (SELECT value FROM manifest WHERE key = 'document_id') AND r.kind = 'layer' "
                     "ORDER BY r.ordinal",
                     [&](Statement &s) {
                         json layer{{"index", s.integer(0)}, {"id", s.text(1)}, {"type", s.text(2)}};
                         const json payload = json::parse(s.text(3), nullptr, false);
                         if (payload.is_object()) {
                             for (const char *key : {"name", "visible", "locked", "opacity", "blendMode", "offset"}) {
                                 if (payload.contains(key)) {
                                     layer[key] = payload[key];
                                 }
                             }
                         }
                         layers.push_back(std::move(layer));
                     });
    }
    out["layers"] = layers;
    if (status) {
        status = run("SELECT COUNT(*) FROM relations", [&](Statement &s) { out["relations"] = s.integer(0); });
    }
    out["revisions"] = json::array();
    if (status) {
        status = run("SELECT id, sequence, parent, label, transaction_id, root, provenance FROM revisions ORDER BY "
                     "sequence",
                     [&](Statement &s) {
                         const json provenance = json::parse(s.text(6), nullptr, false);
                         json revision{{"id", s.text(0)},
                                       {"sequence", s.integer(1)},
                                       {"label", s.text(3)},
                                       {"root", s.text(5)},
                                       {"commands", provenance.is_array() ? provenance.size() : 0}};
                         revision["parent"] = s.isNull(2) ? json(nullptr) : json(s.text(2));
                         revision["transaction"] = s.isNull(4) ? json(nullptr) : json(s.text(4));
                         out["revisions"].push_back(std::move(revision));
                     });
    }
    json byMedia = json::object();
    std::int64_t resourceCount = 0, rawBytes = 0, storedBytes = 0;
    if (status) {
        status = run("SELECT media_type, codec, COUNT(*), SUM(raw_length), SUM(length(data)) FROM resources GROUP BY "
                     "media_type, codec ORDER BY media_type, codec",
                     [&](Statement &s) {
                         json &entry = byMedia[s.text(0)];
                         entry[s.text(1)] = {{"count", s.integer(2)}, {"rawBytes", s.integer(3)},
                                             {"storedBytes", s.integer(4)}};
                         resourceCount += s.integer(2);
                         rawBytes += s.integer(3);
                         storedBytes += s.integer(4);
                     });
    }
    out["resources"] = {{"count", resourceCount}, {"rawBytes", rawBytes}, {"storedBytes", storedBytes}, {"byMediaType", byMedia}};
    if (status) {
        status = run("SELECT COUNT(*), SUM(uniform IS NOT NULL), SUM(resource_hash IS NOT NULL) FROM tile_maps",
                     [&](Statement &s) {
                         out["tilePlanes"] = {{"total", s.integer(0)}, {"uniform", s.integer(1)}, {"dense", s.integer(2)}};
                     });
    }
    json previews = json::array();
    if (status) {
        status = run("SELECT kind, revision, width, height, quality FROM previews ORDER BY kind, revision", [&](Statement &s) {
            previews.push_back({{"kind", s.text(0)}, {"revision", s.text(1)}, {"width", s.integer(2)},
                                {"height", s.integer(3)}, {"quality", s.text(4)}});
        });
    }
    out["previews"] = previews;
    if (!status) {
        return fail(status.error());
    }
    return out;
}

} // namespace archmage::native
