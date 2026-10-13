#include "Writer.h"

#include "archmage/native/FaultPoints.h"
#include "archmage/native/Format.h"
#include "Payloads.h"

#include <chrono>
#include <cstring>
#include <ctime>

namespace archmage::native::detail {

std::string utcNow() {
    const std::time_t now = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm tm{};
    gmtime_r(&now, &tm);
    char buffer[32];
    std::strftime(buffer, sizeof buffer, "%Y-%m-%dT%H:%M:%SZ", &tm);
    return buffer;
}

Writer::Writer(Database &db, WriteReport &report) : db_(db), report_(report) {}

base::Status Writer::prepare() {
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

void Writer::fail(base::Status status) {
    if (status_ && !status) {
        status_ = std::move(status);
    }
}

std::optional<Statement> Writer::statement(std::string_view sql) {
    auto prepared = db_.prepare(sql);
    if (!prepared) {
        fail(base::unexpected(prepared.error()));
        return std::nullopt;
    }
    return std::move(*prepared);
}

void Writer::run(Statement &statement) {
    statement.step();
    fail(statement.status());
    statement.reset();
}

void Writer::object(const std::string &id, std::string_view type, int version, const nlohmann::json &payload) {
    objectText(id, type, version, payload.dump());
}

void Writer::objectText(const std::string &id, std::string_view type, int version, std::string_view payload) {
    if (!status_) {
        return;
    }
    ++report_.objects;
    object_->bind(1, id).bind(2, type).bind(3, std::int64_t{version}).bind(4, payload);
    run(*object_);
}

void Writer::relation(const std::string &parent, std::string_view kind, std::int64_t ordinal,
                      const std::string &child) {
    if (!status_) {
        return;
    }
    relation_->bind(1, parent).bind(2, kind).bind(3, ordinal).bind(4, child);
    run(*relation_);
}

void Writer::resource(const Blob &blob) {
    if (!status_ || !written_.insert(blob.hash).second) {
        return;
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

void Writer::manifest(std::string_view key, std::string_view value) {
    if (!status_) {
        return;
    }
    manifest_->bind(1, key).bind(2, value);
    run(*manifest_);
}

void Writer::asset(const document::PreservedContent::Asset &asset) {
    if (!status_) {
        return;
    }
    asset_->bind(1, asset.id).bind(2, asset.kind).bind(3, asset.payload);
    run(*asset_);
}

void Writer::preservedPlane(const document::PreservedContent::TilePlane &plane) {
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

void Writer::rasterObject(const document::RasterContent &content) {
    object(content.id.toString(), kRasterType, kRasterVersion, rasterPayload(content.tiles));
}

void Writer::raster(const document::RasterContent &content) {
    const std::string id = content.id.toString();
    if (!status_ || !rasters_.insert(id).second) {
        return; // instances share one raster
    }
    rasterObject(content);
    const tiles::TileMap &map = content.tiles;
    map.forEach([&](tiles::TileCoord coord, const std::shared_ptr<const tiles::Tile> &stored) {
        if (status_) {
            (void)tile(id, coord, *stored, map.layout().sampleBytes());
        }
    });
}

// A uniform plane, or a dense plane whose samples happen to be equal, stores its one sample inline.
std::vector<tiles::SpoolPlane> Writer::tile(const std::string &raster, tiles::TileCoord coord,
                                            const tiles::Tile &stored, std::size_t sampleBytes) {
    std::vector<tiles::SpoolPlane> planes;
    if (!status_) {
        return planes;
    }
    auto pinned = stored.pin(); // spooled tiles are read back from the recovery journal
    if (!pinned) {
        fail(base::unexpected(std::move(pinned).error()));
        return planes;
    }
    for (std::size_t index = 0; index < stored.planeCount() && status_; ++index) {
        ++report_.tilePlanes;
        tiles::SpoolPlane spool;
        if (stored.isUniform()) {
            const auto sample = stored.uniformSample(index);
            spool.uniformSample.assign(sample.begin(), sample.end());
        } else {
            const std::span<const std::byte> bytes = stored.plane(index);
            bool same = true;
            for (std::size_t i = sampleBytes; i < bytes.size() && same; i += sampleBytes) {
                same = std::memcmp(bytes.data(), bytes.data() + i, sampleBytes) == 0;
            }
            if (same) {
                spool.uniformSample.assign(bytes.begin(), bytes.begin() + static_cast<std::ptrdiff_t>(sampleBytes));
            } else {
                const Blob blob = encodeBlob(bytes, std::string(kTilePlaneMedia));
                resource(blob);
                spool.resourceHash = blob.hash;
            }
        }
        if (!status_) {
            break;
        }
        if (auto fault = faultPoint(FaultPoint::TileWrite); !fault) {
            fail(std::move(fault));
            break;
        }
        tile_->bind(1, raster).bind(2, std::int64_t{0}).bind(3, coord.tx).bind(4, coord.ty);
        tile_->bind(5, static_cast<std::int64_t>(index));
        if (spool.resourceHash.empty()) {
            tile_->bind(6, spool.uniformSample).bindNull(7);
        } else {
            tile_->bindNull(6).bind(7, spool.resourceHash);
        }
        run(*tile_);
        planes.push_back(std::move(spool));
    }
    return planes;
}

} // namespace archmage::native::detail
