// Writes one revision into an empty database (plan S1-4.3, spec §10.1).

#include "archmage/document/Version.h"
#include "archmage/native/Blob.h"
#include "archmage/native/Format.h"
#include "archmage/native/NativeFile.h"
#include "Payloads.h"
#include "archmage/render/Graph.h"
#include "archmage/render/ImageBuffer.h"
#include "archmage/render/Kernels.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <ctime>
#include <set>
#include <unordered_set>

namespace archmage::native {
namespace {

using nlohmann::json;

std::string utcNow() {
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
    gmtime_r(&now, &tm);
    char buffer[32];
    std::strftime(buffer, sizeof buffer, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buffer;
}

// Inserts rows through prepared statements; the first failure stops further work and is reported once.
class Writer {
public:
    Writer(Database &db, WriteReport &report) : db_(db), report_(report) {}

    base::Status prepare() {
        object_ = statement("INSERT INTO objects (id, type, version, payload) VALUES (?, ?, ?, ?)");
        relation_ = statement("INSERT INTO relations (parent, kind, ordinal, child) VALUES (?, ?, ?, ?)");
        resource_ = statement("INSERT OR IGNORE INTO resources (hash, media_type, codec, codec_version, raw_length, "
                              "data) VALUES (?, ?, ?, ?, ?, ?)");
        tile_ = statement("INSERT INTO tile_maps (raster_id, version, tx, ty, plane, uniform, resource_hash) "
                          "VALUES (?, ?, ?, ?, ?, ?, ?)");
        manifest_ = statement("INSERT INTO manifest (key, value) VALUES (?, ?)");
        asset_ = statement("INSERT INTO assets (id, kind, payload) VALUES (?, ?, ?)");
        return status_;
    }

    [[nodiscard]] const base::Status &status() const { return status_; }

    void object(const std::string &id, std::string_view type, int version, const json &payload) {
        objectText(id, type, version, payload.dump());
    }

    void objectText(const std::string &id, std::string_view type, int version, std::string_view payload) {
        if (!status_) {
            return;
        }
        ++report_.objects;
        object_->bind(1, id).bind(2, type).bind(3, std::int64_t{version}).bind(4, payload);
        run(*object_);
    }

    void relation(const std::string &parent, std::string_view kind, std::int64_t ordinal, const std::string &child) {
        if (!status_) {
            return;
        }
        relation_->bind(1, parent).bind(2, kind).bind(3, ordinal).bind(4, child);
        run(*relation_);
    }

    void resource(const Blob &blob) {
        if (!status_ || !written_.insert(blob.hash).second) {
            return; // content-addressed: one row per distinct content
        }
        ++report_.resources;
        report_.rawBytes += blob.rawLength;
        report_.encodedBytes += blob.data.size();
        resource_->bind(1, blob.hash)
            .bind(2, blob.mediaType)
            .bind(3, blob.codec)
            .bind(4, std::int64_t{blob.codecVersion})
            .bind(5, static_cast<std::int64_t>(blob.rawLength))
            .bind(6, blob.data);
        run(*resource_);
    }

    void manifest(std::string_view key, std::string_view value) {
        if (!status_) {
            return;
        }
        manifest_->bind(1, key).bind(2, value);
        run(*manifest_);
    }

    void asset(const document::PreservedContent::Asset &asset) {
        if (!status_) {
            return;
        }
        asset_->bind(1, asset.id).bind(2, asset.kind).bind(3, asset.payload);
        run(*asset_);
    }

    void preservedPlane(const document::PreservedContent::TilePlane &plane) {
        if (!status_) {
            return;
        }
        ++report_.tilePlanes;
        tile_->bind(1, plane.raster).bind(2, plane.version).bind(3, plane.tx).bind(4, plane.ty).bind(5, plane.plane);
        if (plane.uniform) {
            tile_->bind(6, *plane.uniform).bindNull(7);
        } else {
            tile_->bindNull(6).bind(7, plane.resourceHash);
        }
        run(*tile_);
    }

    /// The raster object and its tile planes; a raster shared by several layers is written once.
    void raster(const document::RasterContent &content) {
        const std::string id = content.id.toString();
        if (!status_ || !rasters_.insert(id).second) {
            return;
        }
        const tiles::TileMap &map = content.tiles;
        object(id, kRasterType, kRasterVersion, rasterPayload(map));
        map.forEach([&](tiles::TileCoord coord, const std::shared_ptr<const tiles::Tile> &tile) {
            for (std::size_t p = 0; p < map.layout().planeCount() && status_; ++p) {
                plane(id, coord, p, *tile, map.layout().sampleBytes());
            }
        });
    }

private:
    std::optional<Statement> statement(std::string_view sql) {
        auto prepared = db_.prepare(sql);
        if (!prepared) {
            if (status_) {
                status_ = base::unexpected(prepared.error());
            }
            return std::nullopt;
        }
        return std::move(*prepared);
    }

    // A uniform plane, or a dense plane whose samples happen to be equal, stores its one sample inline.
    void plane(const std::string &raster, tiles::TileCoord coord, std::size_t index, const tiles::Tile &tile,
               std::size_t sampleBytes) {
        ++report_.tilePlanes;
        std::span<const std::byte> uniform;
        std::optional<Blob> blob;
        if (tile.isUniform()) {
            uniform = tile.uniformSample(index);
        } else {
            const std::span<const std::byte> bytes = tile.plane(index);
            bool same = true;
            for (std::size_t i = sampleBytes; i < bytes.size() && same; i += sampleBytes) {
                same = std::memcmp(bytes.data(), bytes.data() + i, sampleBytes) == 0;
            }
            if (same) {
                uniform = bytes.first(sampleBytes);
            } else {
                blob = encodeBlob(bytes, std::string(kTilePlaneMedia));
                resource(*blob);
            }
        }
        if (!status_) {
            return;
        }
        tile_->bind(1, raster).bind(2, std::int64_t{0}).bind(3, coord.tx).bind(4, coord.ty);
        tile_->bind(5, static_cast<std::int64_t>(index));
        if (blob) {
            tile_->bindNull(6).bind(7, blob->hash);
        } else {
            tile_->bind(6, uniform).bindNull(7);
        }
        run(*tile_);
    }

    void run(Statement &statement) {
        statement.step();
        if (auto status = statement.status(); !status && status_) {
            status_ = std::move(status);
        }
        statement.reset();
    }

    Database &db_;
    WriteReport &report_;
    base::Status status_;
    std::optional<Statement> object_, relation_, resource_, tile_, manifest_, asset_;
    std::unordered_set<std::string> written_;
    std::unordered_set<std::string> rasters_;
};

} // namespace

base::Result<Preview> renderPreview(const document::DocumentState &state, std::int64_t maxSize) {
    if (maxSize <= 0 || state.width <= 0 || state.height <= 0) {
        return base::fail(base::ErrorCategory::Validation, "preview.invalid_size", "The preview size is invalid");
    }
    const double scale =
        std::min(1.0, static_cast<double>(maxSize) / static_cast<double>(std::max(state.width, state.height)));
    Preview preview;
    preview.width = std::max<std::int64_t>(1, std::llround(static_cast<double>(state.width) * scale));
    preview.height = std::max<std::int64_t>(1, std::llround(static_cast<double>(state.height) * scale));

    // The document pixel under each preview pixel center.
    const auto samples = [](std::int64_t count, std::int64_t extent) {
        std::vector<std::int64_t> positions(static_cast<std::size_t>(count));
        for (std::int64_t i = 0; i < count; ++i) {
            const double center = (static_cast<double>(i) + 0.5) * static_cast<double>(extent) /
                                  static_cast<double>(count);
            positions[static_cast<std::size_t>(i)] =
                std::min(extent - 1, static_cast<std::int64_t>(std::floor(center)));
        }
        return positions;
    };
    const std::vector<std::int64_t> xs = samples(preview.width, state.width);
    const std::vector<std::int64_t> ys = samples(preview.height, state.height);

    // One document row per preview row through the authoritative graph; layers clip the strip to their
    // painted bounds, so empty regions of huge documents cost nothing.
    const auto root = render::compileGraph(state);
    const render::EvaluationContext context{};
    preview.rgba.resize(static_cast<std::size_t>(preview.width * preview.height) * 4);
    for (std::int64_t y = 0; y < preview.height; ++y) {
        const std::int64_t row = ys[static_cast<std::size_t>(y)];
        render::ImageBuffer strip(base::IRect{xs.front(), row, xs.back() + 1, row + 1});
        if (auto status = root->evaluate(context, strip); !status) {
            return base::unexpected(std::move(status).error());
        }
        for (std::int64_t x = 0; x < preview.width; ++x) {
            float pixel[4];
            std::memcpy(pixel, strip.pixel(xs[static_cast<std::size_t>(x)], row), sizeof pixel);
            render::unpremultiplyRow(std::span<float>(pixel, 4));
            std::byte *out = preview.rgba.data() + static_cast<std::size_t>(y * preview.width + x) * 4;
            for (int c = 0; c < 4; ++c) {
                tiles::storeSample(tiles::SampleType::UInt8, pixel[c], out + c);
            }
        }
    }
    return preview;
}

base::Result<WriteReport> writeDocument(Database &db, const document::Revision &revision,
                                        const WriteOptions &options) {
    if (!revision.state) {
        return base::fail(base::ErrorCategory::Internal, "save.no_state", "There is no document to save");
    }
    const document::DocumentState &state = *revision.state;
    if (auto valid = document::validate(state); !valid) {
        return base::unexpected(std::move(valid).error());
    }
    std::optional<Preview> preview;
    if (options.previewMaxSize > 0) {
        auto rendered = renderPreview(state, options.previewMaxSize);
        if (!rendered) {
            return base::unexpected(std::move(rendered).error());
        }
        preview = std::move(*rendered);
    }

    // A standalone file written in one transaction: no WAL or rollback journal file beside it (spec §10.2).
    // The rollback journal stays in memory (defensive mode forbids journal_mode OFF): a crash mid-write
    // leaves a damaged temporary file, which the save discards anyway. Durability comes from the save
    // transaction's fsync and rename, so SQLite's own syncs are off.
    for (const std::string_view sql : {std::string_view("PRAGMA journal_mode = MEMORY"),
                                       std::string_view("PRAGMA synchronous = OFF"), schemaSql(),
                                       std::string_view("BEGIN")}) {
        if (auto status = db.exec(sql); !status) {
            return base::unexpected(std::move(status).error());
        }
    }
    WriteReport report;
    Writer writer(db, report);
    (void)writer.prepare();

    const base::DocumentId identity = options.fileIdentity.isNull() ? state.id : options.fileIdentity;
    const std::string documentId = identity.toString();
    const Blob profile = encodeBlob(state.colorPolicy.profile->iccBytes(), std::string(kIccMedia));
    writer.resource(profile);
    writer.object(documentId, kDocumentType, kDocumentVersion, documentPayload(state, profile.hash));

    std::set<std::string> written{documentId};
    for (std::size_t i = 0; i < state.layers.size(); ++i) {
        const document::Layer &layer = *state.layers[i];
        const std::string layerId = layer.id.toString();
        const std::string rasterId = layer.content->id.toString();
        written.insert(layerId);
        written.insert(rasterId);
        writer.object(layerId, kRasterLayerType, kRasterLayerVersion, layerPayload(layer));
        writer.relation(documentId, kLayerRelation, static_cast<std::int64_t>(i), layerId);
        writer.relation(layerId, kContentRelation, 0, rasterId);
        writer.raster(*layer.content);
    }

    // Content of the opened file that this version does not understand (spec §10.5).
    if (state.preserved) {
        const document::PreservedContent &preserved = *state.preserved;
        for (const auto &object : preserved.objects) {
            if (written.insert(object.id).second) {
                writer.objectText(object.id, object.type, object.version, object.payload);
            }
        }
        for (const auto &relation : preserved.relations) {
            if (!written.contains(relation.parent) || !written.contains(relation.child)) {
                ++report.droppedOpaqueRelations;
                continue;
            }
            writer.relation(relation.parent, relation.kind, relation.ordinal, relation.child);
        }
        for (const auto &plane : preserved.tilePlanes) {
            writer.preservedPlane(plane);
        }
        for (const auto &resource : preserved.resources) {
            writer.resource(Blob{resource.hash, resource.mediaType, resource.codec, resource.codecVersion,
                                 resource.rawLength, resource.data});
        }
        for (const auto &asset : preserved.assets) {
            writer.asset(asset);
        }
        for (const auto &[key, value] : preserved.manifest) {
            if (!isKnownManifestKey(key)) {
                writer.manifest(key, value);
            }
        }
    }

    // The revision with its provenance (spec §9.2).
    if (writer.status()) {
        auto statement = db.prepare("INSERT INTO revisions (id, sequence, parent, label, transaction_id, root, "
                                    "provenance) VALUES (?, ?, ?, ?, ?, ?, ?)");
        if (!statement) {
            return base::unexpected(statement.error());
        }
        statement->bind(1, revision.id.toString()).bind(2, static_cast<std::int64_t>(revision.sequence));
        revision.parent.isNull() ? statement->bindNull(3) : statement->bind(3, revision.parent.toString());
        statement->bind(4, revision.label);
        revision.transaction.isNull() ? statement->bindNull(5)
                                      : statement->bind(5, revision.transaction.toString());
        statement->bind(6, documentId).bind(7, revision.provenance.dump());
        statement->step();
        if (auto status = statement->status(); !status) {
            return base::unexpected(std::move(status).error());
        }
    }

    if (writer.status() && preview) {
        const Blob blob = encodeBlob(preview->rgba, std::string(kPreviewMedia));
        writer.resource(blob);
        auto statement = db.prepare("INSERT INTO previews (kind, revision, width, height, media_type, "
                                    "resource_hash, profile, quality) VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
        if (!statement) {
            return base::unexpected(statement.error());
        }
        statement->bind(1, kCompositePreview).bind(2, revision.id.toString());
        statement->bind(3, preview->width).bind(4, preview->height).bind(5, kPreviewMedia);
        statement->bind(6, blob.hash).bind(7, profile.hash).bind(8, kNearestQuality);
        statement->step();
        if (auto status = statement->status(); !status) {
            return base::unexpected(std::move(status).error());
        }
    }

    writer.manifest(manifest::kAppId, kApplicationIdText);
    writer.manifest(manifest::kSchemaMajor, std::to_string(kSchemaMajor));
    writer.manifest(manifest::kSchemaMinor, std::to_string(kSchemaMinor));
    writer.manifest(manifest::kRequiredCapabilities, json(state.capabilities).dump());
    writer.manifest(manifest::kTileSize, std::to_string(tiles::kTileSize));
    writer.manifest(manifest::kDocumentId, documentId);
    writer.manifest(manifest::kCurrentRevision, revision.id.toString());
    writer.manifest(manifest::kColorPolicy, colorPolicyPayload(state.colorPolicy, profile.hash).dump());
    writer.manifest(manifest::kWriter, document::versionBanner());
    writer.manifest(manifest::kSavedAt, utcNow());
    if (!options.derivedFrom.isNull()) {
        writer.manifest(manifest::kDerivedFrom, options.derivedFrom.toString());
    }

    if (!writer.status()) {
        (void)db.exec("ROLLBACK");
        return base::unexpected(writer.status().error());
    }
    if (auto committed = db.exec("COMMIT"); !committed) {
        return base::unexpected(std::move(committed).error());
    }
    return report;
}

} // namespace archmage::native
