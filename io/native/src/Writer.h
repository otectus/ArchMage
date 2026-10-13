// Row writers shared by the saved-file writer and the recovery journal (io/native internal).
#pragma once

#include "archmage/document/Document.h"
#include "archmage/native/Blob.h"
#include "archmage/native/NativeFile.h"
#include "archmage/native/Sqlite.h"
#include "archmage/tiles/Tile.h"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>
#include <string_view>
#include <unordered_set>
#include <vector>

namespace archmage::native::detail {

/// UTC now as "YYYY-MM-DDTHH:MM:SSZ".
[[nodiscard]] std::string utcNow();

/// Inserts rows through prepared statements. The first failure stops further work and stays in status().
class Writer {
public:
    Writer(Database &db, WriteReport &report);

    /// Prepares the statements; call once before writing.
    base::Status prepare();
    [[nodiscard]] const base::Status &status() const { return status_; }
    void fail(base::Status status);

    void object(const std::string &id, std::string_view type, int version, const nlohmann::json &payload);
    void objectText(const std::string &id, std::string_view type, int version, std::string_view payload);
    void relation(const std::string &parent, std::string_view kind, std::int64_t ordinal, const std::string &child);
    /// Content-addressed: one row per distinct hash (INSERT OR IGNORE).
    void resource(const Blob &blob);
    void manifest(std::string_view key, std::string_view value);
    void asset(const document::PreservedContent::Asset &asset);
    void preservedPlane(const document::PreservedContent::TilePlane &plane);

    /// The raster object and every stored tile; a raster already written by this writer is skipped.
    void raster(const document::RasterContent &content);
    /// The raster object only.
    void rasterObject(const document::RasterContent &content);
    /// One tile's plane rows (version 0); returns how each plane was stored, for spooling. Pins the tile.
    std::vector<tiles::SpoolPlane> tile(const std::string &raster, tiles::TileCoord coord, const tiles::Tile &tile,
                                        std::size_t sampleBytes);

private:
    std::optional<Statement> statement(std::string_view sql);
    void run(Statement &statement);

    Database &db_;
    WriteReport &report_;
    base::Status status_;
    std::optional<Statement> object_, relation_, resource_, tile_, manifest_, asset_;
    std::unordered_set<std::string> written_;
    std::unordered_set<std::string> rasters_;
};

} // namespace archmage::native::detail
