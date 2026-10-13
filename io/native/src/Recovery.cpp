// The recovery workspace: location, session lock and thread, document journals, discovery (plan S1-5.2 to
// S1-5.4; D-14, D-26).

#include "archmage/native/Recovery.h"

#include "archmage/base/Random.h"
#include "archmage/document/Version.h"
#include "archmage/native/FaultPoints.h"
#include "archmage/native/Format.h"
#include "archmage/tiles/TileSpool.h"
#include "Payloads.h"
#include "Reader.h"
#include "Writer.h"

#include <algorithm>
#include <condition_variable>
#include <cstring>
#include <mutex>
#include <set>
#include <thread>
#include <unordered_map>

#include <fcntl.h>
#include <sys/file.h>
#include <unistd.h>

namespace archmage::native {
namespace {

using base::ErrorCategory;
using nlohmann::json;

constexpr std::string_view kLockName = "session.lock";
constexpr std::string_view kJournalExtension = ".recovery";
constexpr std::string_view kSessionKey = "journal.session";
constexpr std::string_view kWrittenAtKey = "journal.written_at";
constexpr std::string_view kSourcePathKey = "journal.source_path";
constexpr std::string_view kSavedRevisionKey = "journal.saved_revision";
constexpr std::string_view kSavedPathKey = "journal.saved_path";
constexpr std::string_view kJournalKeyPrefix = "journal.";

base::Unexpected<base::Error> fail(base::Error error) { return base::Unexpected<base::Error>(std::move(error)); }

// An exclusive, non-blocking flock(2) on a session's lock file, held while the object lives.
class SessionLock {
public:
    SessionLock() = default;
    SessionLock(SessionLock &&other) noexcept : fd_(std::exchange(other.fd_, -1)) {}
    SessionLock &operator=(SessionLock &&other) noexcept {
        if (this != &other) {
            release();
            fd_ = std::exchange(other.fd_, -1);
        }
        return *this;
    }
    ~SessionLock() { release(); }

    /// False when another process holds the lock (a live session) or the file cannot be opened.
    bool tryAcquire(const std::filesystem::path &path) {
        release();
        fd_ = ::open(path.c_str(), O_RDWR | O_CREAT | O_CLOEXEC, 0600);
        if (fd_ < 0) {
            return false;
        }
        if (::flock(fd_, LOCK_EX | LOCK_NB) != 0) {
            release();
            return false;
        }
        return true;
    }
    void release() {
        if (fd_ >= 0) {
            ::close(fd_);
            fd_ = -1;
        }
    }

private:
    int fd_ = -1;
};

void removeDatabaseFiles(const std::filesystem::path &path) {
    std::error_code ignored;
    for (const char *suffix : {"", "-wal", "-shm", "-journal"}) {
        std::filesystem::remove(path.string() + suffix, ignored);
    }
}

bool isJournalFile(const std::filesystem::directory_entry &entry) {
    if (!entry.is_regular_file()) {
        return false;
    }
    const std::string name = entry.path().filename().string();
    for (const char *suffix : {"-wal", "-shm", "-journal"}) {
        if (name.ends_with(suffix)) {
            return false;
        }
    }
    return name != kLockName;
}

bool hasJournals(const std::filesystem::path &directory) {
    std::error_code ec;
    for (const auto &entry : std::filesystem::directory_iterator(directory, ec)) {
        if (isJournalFile(entry)) {
            return true;
        }
    }
    return false;
}

// Reads journaled planes back for evicted tiles, through its own connection (WAL readers never block the
// journal writer).
class JournalSpool final : public tiles::SpoolSource {
public:
    explicit JournalSpool(Database db) : db_(std::move(db)) {}

    base::Status load(std::span<const tiles::SpoolPlane> planes, tiles::SampleType type,
                      std::span<std::byte> out) override {
        const std::lock_guard lock(mutex_);
        if (!query_) {
            auto prepared = db_.prepare(
                "SELECT media_type, codec, codec_version, raw_length, data FROM resources WHERE hash = ?");
            if (!prepared) {
                return base::unexpected(prepared.error());
            }
            query_.emplace(std::move(*prepared));
        }
        const std::size_t sampleBytes = tiles::bytesPerSample(type);
        const std::size_t planeBytes = tiles::kTilePixels * sampleBytes;
        if (out.size() != planes.size() * planeBytes) {
            return base::fail(ErrorCategory::Internal, "recovery.spool_size", "A tile could not be read back",
                              "size mismatch");
        }
        for (std::size_t p = 0; p < planes.size(); ++p) {
            const std::span<std::byte> target = out.subspan(p * planeBytes, planeBytes);
            const tiles::SpoolPlane &plane = planes[p];
            if (plane.resourceHash.empty()) {
                if (plane.uniformSample.size() != sampleBytes) {
                    return fail(damaged("recovery.spool_damaged", "uniform sample size"));
                }
                for (std::size_t i = 0; i < target.size(); i += sampleBytes) {
                    std::memcpy(target.data() + i, plane.uniformSample.data(), sampleBytes);
                }
                continue;
            }
            query_->reset();
            query_->bind(1, plane.resourceHash);
            const bool found = query_->step();
            if (auto status = query_->status(); !status) {
                query_.reset(); // a failed statement keeps its error; prepare a fresh one next time
                return status;
            }
            if (!found) {
                return fail(damaged("recovery.spool_missing", "journal blob " + plane.resourceHash.substr(0, 16)));
            }
            const Blob blob{plane.resourceHash, query_->text(0), query_->text(1),
                            static_cast<int>(query_->integer(2)), static_cast<std::uint64_t>(query_->integer(3)),
                            query_->blob(4)};
            auto bytes = decodeBlob(blob);
            if (!bytes) {
                return base::unexpected(std::move(bytes).error());
            }
            if (bytes->size() != planeBytes) {
                return fail(damaged("recovery.spool_damaged", "plane size"));
            }
            std::memcpy(target.data(), bytes->data(), planeBytes);
        }
        return {};
    }

private:
    std::mutex mutex_;
    Database db_;
    std::optional<Statement> query_;
};

} // namespace

std::string_view durabilityName(Durability durability) {
    switch (durability) {
    case Durability::InMemory:
        return "in_memory";
    case Durability::Journaled:
        return "journaled";
    case Durability::Saved:
        return "saved";
    }
    return "in_memory";
}

base::Result<RecoveryRoot> locateRecoveryRoot() {
    std::vector<std::filesystem::path> candidates;
    if (const char *state = std::getenv("XDG_STATE_HOME"); state != nullptr && *state == '/') {
        candidates.push_back(std::filesystem::path(state) / "archmage");
    }
    if (const char *home = std::getenv("HOME"); home != nullptr && *home == '/') {
        candidates.push_back(std::filesystem::path(home) / ".local" / "state" / "archmage");
    }
    candidates.push_back(std::filesystem::path("/var/tmp") / ("archmage-" + std::to_string(::getuid())));
    RecoveryRoot root;
    for (const auto &candidate : candidates) {
        const FilesystemInfo info = filesystemInfo(candidate); // asks the nearest existing directory
        if (!info.local) {
            root.refused.push_back(candidate.string() + ": " + info.type + " is not a local filesystem");
            continue;
        }
        std::error_code ec;
        std::filesystem::create_directories(candidate, ec);
        if (ec) {
            root.refused.push_back(candidate.string() + ": " + ec.message());
            continue;
        }
        std::filesystem::permissions(candidate, std::filesystem::perms::owner_all, ec);
        root.path = candidate;
        root.filesystem = info;
        return root;
    }
    std::string reasons;
    for (const std::string &reason : root.refused) {
        reasons += (reasons.empty() ? "" : "; ") + reason;
    }
    return base::fail(ErrorCategory::PermissionDenied, "recovery.no_location",
                      "No local folder is available for recovery files", reasons);
}

// ---------------------------------------------------------------------------------------------------------
// DocumentJournal

struct DocumentJournal::Impl {
    // Configuration.
    base::SessionId session;
    RecoverySession::Options options;

    // The executor observation (set by track, cleared by untrack).
    commands::MutationExecutor *executor = nullptr;
    std::uint64_t subscription = 0;

    // Writer state, guarded by writeMutex.
    mutable std::mutex writeMutex;
    std::optional<Database> db;
    std::shared_ptr<JournalSpool> spool;
    std::shared_ptr<const document::DocumentState> journaledState;
    std::shared_ptr<const document::PreservedContent> journaledPreserved;
    std::chrono::steady_clock::time_point lastPreview{};
    bool hasPreview = false;
    bool closed = false;

    // Status, guarded by statusMutex.
    mutable std::mutex statusMutex;
    std::shared_ptr<const document::Revision> latest;
    std::optional<base::RevisionId> journaledRevision;
    std::optional<base::RevisionId> savedRevision;
    std::filesystem::path sourcePath;
    std::filesystem::path savedPath;
    base::Status lastError;
    Stats stats;
};

DocumentJournal::DocumentJournal(std::filesystem::path path, base::DocumentId documentId,
                                 const RecoverySession &session)
    : path_(std::move(path)), documentId_(documentId), impl_(std::make_unique<Impl>()) {
    impl_->session = session.id();
    impl_->options = session.options();
}

DocumentJournal::~DocumentJournal() = default;

Durability DocumentJournal::durability(const base::RevisionId &revision) const {
    const std::lock_guard lock(impl_->statusMutex);
    if (impl_->savedRevision == revision) {
        return Durability::Saved;
    }
    if (impl_->journaledRevision == revision) {
        return Durability::Journaled;
    }
    return Durability::InMemory;
}

Durability DocumentJournal::currentDurability() const {
    std::shared_ptr<const document::Revision> latest;
    {
        const std::lock_guard lock(impl_->statusMutex);
        latest = impl_->latest;
    }
    return latest ? durability(latest->id) : Durability::InMemory;
}

std::optional<base::RevisionId> DocumentJournal::journaledRevision() const {
    const std::lock_guard lock(impl_->statusMutex);
    return impl_->journaledRevision;
}

base::Status DocumentJournal::lastError() const {
    const std::lock_guard lock(impl_->statusMutex);
    return impl_->lastError;
}

DocumentJournal::Stats DocumentJournal::stats() const {
    const std::lock_guard lock(impl_->statusMutex);
    return impl_->stats;
}

namespace {

// Writes `revision` into the journal in one transaction, re-encoding only tiles that changed since the
// journaled state. Requires impl.writeMutex.
base::Status writeRevision(DocumentJournal::Impl &impl, const std::shared_ptr<const document::Revision> &revision) {
    const auto started = std::chrono::steady_clock::now();
    if (auto fault = faultPoint(FaultPoint::JournalWrite); !fault) {
        return fault;
    }
    Database &db = *impl.db;
    const document::DocumentState &state = *revision->state;
    if (auto status = db.exec("BEGIN IMMEDIATE"); !status) {
        return status;
    }
    const auto rollback = [&](base::Status status) {
        (void)db.exec("ROLLBACK");
        return status;
    };
    for (const char *sql : {"DELETE FROM objects", "DELETE FROM relations", "DELETE FROM revisions",
                            "DELETE FROM manifest", "DELETE FROM assets"}) {
        if (auto status = db.exec(sql); !status) {
            return rollback(std::move(status));
        }
    }
    WriteReport report;
    detail::Writer writer(db, report);
    if (auto status = writer.prepare(); !status) {
        return rollback(std::move(status));
    }

    const std::string documentId = state.id.toString();
    const Blob profile = encodeBlob(state.colorPolicy.profile->iccBytes(), std::string(kIccMedia));
    writer.resource(profile);
    writer.object(documentId, kDocumentType, kDocumentVersion, documentPayload(state, profile.hash));

    // Rasters in the new state, and what the journal already holds for each: the same raster, or the
    // previous content of the same layer (painting gives a layer new content with a new ID).
    std::unordered_map<std::string, const document::RasterContent *> oldRasters;
    std::unordered_map<std::string, const document::RasterContent *> oldByLayer;
    if (impl.journaledState) {
        for (const auto &layer : impl.journaledState->layers) {
            oldRasters.emplace(layer->content->id.toString(), layer->content.get());
            oldByLayer.emplace(layer->id.toString(), layer->content.get());
        }
    }
    std::set<std::string> newRasters;
    for (const auto &layer : state.layers) {
        newRasters.insert(layer->content->id.toString());
    }
    std::vector<std::pair<std::shared_ptr<const tiles::Tile>, std::vector<tiles::SpoolPlane>>> written;
    const auto writeTile = [&](const std::string &raster, tiles::TileCoord coord,
                               const std::shared_ptr<const tiles::Tile> &tile, std::size_t sampleBytes) {
        auto planes = writer.tile(raster, coord, *tile, sampleBytes);
        if (!tile->isUniform() && writer.status()) {
            written.emplace_back(tile, std::move(planes));
        }
    };
    auto deleteTile = db.prepare("DELETE FROM tile_maps WHERE raster_id = ? AND version = 0 AND tx = ? AND ty = ?");
    auto moveRows = db.prepare("UPDATE tile_maps SET raster_id = ? WHERE raster_id = ? AND version = 0");
    auto copyRows = db.prepare("INSERT INTO tile_maps (raster_id, version, tx, ty, plane, uniform, resource_hash) "
                               "SELECT ?, version, tx, ty, plane, uniform, resource_hash FROM tile_maps "
                               "WHERE raster_id = ? AND version = 0");
    if (!deleteTile || !moveRows || !copyRows) {
        return rollback(base::unexpected(!deleteTile ? deleteTile.error()
                                                     : (!moveRows ? moveRows.error() : copyRows.error())));
    }
    const auto run = [&](Statement &statement) {
        statement.step();
        writer.fail(statement.status());
        statement.reset();
    };

    std::set<std::string> rastersDone;
    std::set<std::string> movedAway;
    std::uint64_t tilesWritten = 0;
    for (std::size_t i = 0; i < state.layers.size() && writer.status(); ++i) {
        const document::Layer &layer = *state.layers[i];
        const std::string layerId = layer.id.toString();
        const document::RasterContent &content = *layer.content;
        const std::string rasterId = content.id.toString();
        writer.object(layerId, kRasterLayerType, kRasterLayerVersion, layerPayload(layer));
        writer.relation(documentId, kLayerRelation, static_cast<std::int64_t>(i), layerId);
        writer.relation(layerId, kContentRelation, 0, rasterId);
        if (!rastersDone.insert(rasterId).second) {
            continue;
        }
        writer.rasterObject(content);
        const document::RasterContent *base = nullptr;
        if (const auto same = oldRasters.find(rasterId); same != oldRasters.end()) {
            base = same->second;
        } else if (const auto previous = oldByLayer.find(layerId);
                   previous != oldByLayer.end() && previous->second->tiles.layout() == content.tiles.layout()) {
            const std::string previousId = previous->second->id.toString();
            Statement &statement =
                !newRasters.contains(previousId) && movedAway.insert(previousId).second ? *moveRows : *copyRows;
            statement.bind(1, rasterId).bind(2, previousId);
            run(statement);
            base = previous->second;
        }
        const std::size_t sampleBytes = content.tiles.layout().sampleBytes();
        if (base != nullptr) {
            for (const tiles::TileCoord coord : tiles::TileMap::changedTiles(base->tiles, content.tiles)) {
                if (!writer.status()) {
                    break;
                }
                deleteTile->bind(1, rasterId).bind(2, coord.tx).bind(3, coord.ty);
                run(*deleteTile);
                if (const auto tile = content.tiles.find(coord)) {
                    writeTile(rasterId, coord, tile, sampleBytes);
                    ++tilesWritten;
                }
            }
        } else {
            content.tiles.forEach([&](tiles::TileCoord coord, const std::shared_ptr<const tiles::Tile> &tile) {
                if (writer.status()) {
                    writeTile(rasterId, coord, tile, sampleBytes);
                    ++tilesWritten;
                }
            });
        }
    }

    // Unknown content of the opened file travels with the journal so that recovery keeps it.
    const auto &preserved = state.preserved;
    if (preserved) {
        for (const auto &object : preserved->objects) {
            writer.objectText(object.id, object.type, object.version, object.payload);
        }
        for (const auto &relation : preserved->relations) {
            writer.relation(relation.parent, relation.kind, relation.ordinal, relation.child);
        }
        for (const auto &asset : preserved->assets) {
            writer.asset(asset);
        }
    }
    // Preserved tile planes are replaced on every write; then the rows of rasters that are gone go too.
    if (writer.status() && impl.journaledPreserved && !impl.journaledPreserved->tilePlanes.empty()) {
        auto remove = db.prepare("DELETE FROM tile_maps WHERE raster_id = ? AND version = ? AND tx = ? AND ty = ? "
                                 "AND plane = ?");
        if (!remove) {
            writer.fail(base::unexpected(remove.error()));
        }
        for (const auto &plane : impl.journaledPreserved->tilePlanes) {
            if (!writer.status()) {
                break;
            }
            remove->bind(1, plane.raster).bind(2, plane.version).bind(3, plane.tx).bind(4, plane.ty);
            remove->bind(5, plane.plane);
            run(*remove);
        }
    }
    if (writer.status()) {
        if (auto status = db.exec("DELETE FROM tile_maps WHERE raster_id NOT IN "
                                  "(SELECT id FROM objects WHERE type = 'raster')");
            !status) {
            writer.fail(std::move(status));
        }
    }
    if (writer.status() && preserved) {
        for (const auto &plane : preserved->tilePlanes) {
            writer.preservedPlane(plane);
        }
        if (preserved != impl.journaledPreserved) { // blobs can be large: only when the content changed
            for (const auto &resource : preserved->resources) {
                writer.resource(Blob{resource.hash, resource.mediaType, resource.codec, resource.codecVersion,
                                     resource.rawLength, resource.data});
            }
        }
    }

    // The revision, a thumbnail now and then, and the manifest.
    if (writer.status()) {
        auto statement = db.prepare("INSERT INTO revisions (id, sequence, parent, label, transaction_id, root, "
                                    "provenance) VALUES (?, ?, ?, ?, ?, ?, ?)");
        if (!statement) {
            return rollback(base::unexpected(statement.error()));
        }
        statement->bind(1, revision->id.toString()).bind(2, static_cast<std::int64_t>(revision->sequence));
        revision->parent.isNull() ? statement->bindNull(3) : statement->bind(3, revision->parent.toString());
        statement->bind(4, revision->label);
        revision->transaction.isNull() ? statement->bindNull(5)
                                       : statement->bind(5, revision->transaction.toString());
        statement->bind(6, documentId).bind(7, revision->provenance.dump());
        run(*statement);
    }
    const auto now = std::chrono::steady_clock::now();
    const bool previewDue = impl.options.previewMaxSize > 0 &&
                            (!impl.hasPreview || now - impl.lastPreview >= impl.options.previewInterval);
    if (writer.status() && previewDue) {
        if (auto preview = renderPreview(state, impl.options.previewMaxSize)) {
            const Blob blob = encodeBlob(preview->rgba, std::string(kPreviewMedia));
            writer.resource(blob);
            if (auto status = db.exec("DELETE FROM previews"); !status) {
                writer.fail(std::move(status));
            }
            auto statement = db.prepare("INSERT INTO previews (kind, revision, width, height, media_type, "
                                        "resource_hash, profile, quality) VALUES (?, ?, ?, ?, ?, ?, ?, ?)");
            if (statement) {
                statement->bind(1, kCompositePreview).bind(2, revision->id.toString());
                statement->bind(3, preview->width).bind(4, preview->height).bind(5, kPreviewMedia);
                statement->bind(6, blob.hash).bind(7, profile.hash).bind(8, kNearestQuality);
                run(*statement);
            } else {
                writer.fail(base::unexpected(statement.error()));
            }
        } else {
            writer.fail(base::unexpected(std::move(preview).error()));
        }
    }
    std::string sourcePath;
    std::string savedRevision;
    std::string savedPath;
    {
        const std::lock_guard lock(impl.statusMutex);
        sourcePath = impl.sourcePath.string();
        savedRevision = impl.savedRevision ? impl.savedRevision->toString() : std::string();
        savedPath = impl.savedPath.string();
    }
    writer.manifest(manifest::kAppId, kApplicationIdText);
    writer.manifest(manifest::kSchemaMajor, std::to_string(kSchemaMajor));
    writer.manifest(manifest::kSchemaMinor, std::to_string(kSchemaMinor));
    writer.manifest(manifest::kRequiredCapabilities, json(state.capabilities).dump());
    writer.manifest(manifest::kTileSize, std::to_string(tiles::kTileSize));
    writer.manifest(manifest::kDocumentId, documentId);
    writer.manifest(manifest::kCurrentRevision, revision->id.toString());
    writer.manifest(manifest::kColorPolicy, colorPolicyPayload(state.colorPolicy, profile.hash).dump());
    writer.manifest(manifest::kWriter, document::versionBanner());
    writer.manifest(kSessionKey, impl.session.toString());
    writer.manifest(kWrittenAtKey, detail::utcNow());
    if (!sourcePath.empty()) {
        writer.manifest(kSourcePathKey, sourcePath);
    }
    if (!savedRevision.empty()) {
        writer.manifest(kSavedRevisionKey, savedRevision);
        writer.manifest(kSavedPathKey, savedPath);
    }
    if (preserved) {
        for (const auto &[key, value] : preserved->manifest) {
            if (!isKnownManifestKey(key) && !key.starts_with(kJournalKeyPrefix)) {
                writer.manifest(key, value);
            }
        }
    }
    if (!writer.status()) {
        return rollback(writer.status());
    }
    if (auto fault = faultPoint(FaultPoint::TxCommit); !fault) {
        return rollback(std::move(fault));
    }
    if (auto status = db.exec("COMMIT"); !status) {
        return rollback(std::move(status));
    }

    // Durable now: the tiles just written may leave memory, and this revision is journaled.
    for (auto &[tile, planes] : written) {
        tiles::TileSpool::global().add(tile, impl.spool, std::move(planes));
    }
    impl.journaledState = revision->state;
    impl.journaledPreserved = preserved;
    if (previewDue) {
        impl.lastPreview = now;
        impl.hasPreview = true;
    }
    const std::lock_guard lock(impl.statusMutex);
    impl.journaledRevision = revision->id;
    impl.lastError = {};
    ++impl.stats.writes;
    impl.stats.tilesWritten += tilesWritten;
    impl.stats.lastWriteMs =
        std::chrono::duration<double, std::milli>(std::chrono::steady_clock::now() - started).count();
    return {};
}

// Drops blobs nothing needs (not the journaled tiles, previews, unknown content, nor evicted tiles that a
// revision in memory may read back), reclaims their pages and checkpoints the write-ahead log.
base::Status compactJournal(DocumentJournal::Impl &impl) {
    if (auto fault = faultPoint(FaultPoint::Compaction); !fault) {
        return fault;
    }
    Database &db = *impl.db;
    std::set<std::string> keep;
    for (const std::string &hash : tiles::TileSpool::global().liveHashes(impl.spool.get())) {
        keep.insert(hash);
    }
    if (impl.journaledState) {
        keep.insert(impl.journaledState->colorPolicy.profile->hash().hex());
    }
    if (impl.journaledPreserved) {
        for (const auto &resource : impl.journaledPreserved->resources) {
            keep.insert(resource.hash);
        }
    }
    for (const char *sql : {"BEGIN IMMEDIATE", "CREATE TEMP TABLE IF NOT EXISTS keep_hashes (hash TEXT PRIMARY KEY)",
                            "DELETE FROM temp.keep_hashes"}) {
        if (auto status = db.exec(sql); !status) {
            (void)db.exec("ROLLBACK");
            return status;
        }
    }
    auto insert = db.prepare("INSERT OR IGNORE INTO temp.keep_hashes (hash) VALUES (?)");
    if (!insert) {
        (void)db.exec("ROLLBACK");
        return base::unexpected(insert.error());
    }
    for (const std::string &hash : keep) {
        insert->bind(1, hash);
        insert->step();
        insert->reset();
    }
    if (auto status = insert->status(); !status) {
        (void)db.exec("ROLLBACK");
        return status;
    }
    for (const char *sql : {"DELETE FROM resources WHERE hash NOT IN (SELECT resource_hash FROM tile_maps WHERE "
                            "resource_hash IS NOT NULL) AND hash NOT IN (SELECT resource_hash FROM previews) AND "
                            "hash NOT IN (SELECT profile FROM previews) AND hash NOT IN (SELECT hash FROM "
                            "temp.keep_hashes)",
                            "COMMIT", "PRAGMA incremental_vacuum"}) {
        if (auto status = db.exec(sql); !status) {
            (void)db.exec("ROLLBACK");
            return status;
        }
    }
    if (auto fault = faultPoint(FaultPoint::Checkpoint); !fault) {
        return fault;
    }
    if (auto status = db.exec("PRAGMA wal_checkpoint(TRUNCATE)"); !status) {
        return status;
    }
    const std::lock_guard lock(impl.statusMutex);
    ++impl.stats.compactions;
    return {};
}

// Writes the latest revision if the journal does not hold it; compacts every few writes. Records the
// outcome as the journal's last error. Requires impl.writeMutex.
base::Status journalLatest(DocumentJournal::Impl &impl) {
    if (impl.closed || !impl.db) {
        return {};
    }
    std::shared_ptr<const document::Revision> latest;
    {
        const std::lock_guard lock(impl.statusMutex);
        latest = impl.latest;
        if (!latest || impl.journaledRevision == latest->id) {
            return {};
        }
    }
    base::Status status = writeRevision(impl, latest);
    if (status) {
        std::uint64_t writes = 0;
        {
            const std::lock_guard lock(impl.statusMutex);
            writes = impl.stats.writes;
        }
        if (impl.options.compactEvery > 0 && writes % static_cast<std::uint64_t>(impl.options.compactEvery) == 0) {
            status = compactJournal(impl);
        }
    }
    if (!status) {
        const std::lock_guard lock(impl.statusMutex);
        impl.lastError = status;
    }
    return status;
}

} // namespace

base::Status DocumentJournal::flush() {
    const std::lock_guard lock(impl_->writeMutex);
    return journalLatest(*impl_);
}

base::Status DocumentJournal::compact() {
    const std::lock_guard lock(impl_->writeMutex);
    if (impl_->closed || !impl_->db) {
        return {};
    }
    return compactJournal(*impl_);
}

base::Status DocumentJournal::markSaved(const document::Revision &revision, const std::filesystem::path &path) {
    const std::lock_guard write(impl_->writeMutex);
    {
        const std::lock_guard lock(impl_->statusMutex);
        impl_->savedRevision = revision.id;
        impl_->savedPath = path;
        impl_->sourcePath = path;
    }
    if (impl_->closed || !impl_->db) {
        return {};
    }
    // Recorded at once so that a crash right after a save does not offer a pointless recovery.
    Database &db = *impl_->db;
    auto statement = db.prepare("INSERT OR REPLACE INTO manifest (key, value) VALUES (?, ?)");
    if (!statement) {
        return base::unexpected(statement.error());
    }
    if (auto status = db.exec("BEGIN IMMEDIATE"); !status) {
        return status;
    }
    for (const auto &[key, value] : {std::pair{kSavedRevisionKey, revision.id.toString()},
                                     std::pair{kSavedPathKey, path.string()},
                                     std::pair{kSourcePathKey, path.string()}}) {
        statement->bind(1, key).bind(2, value);
        statement->step();
        statement->reset();
    }
    if (auto status = statement->status(); !status) {
        (void)db.exec("ROLLBACK");
        return status;
    }
    return db.exec("COMMIT");
}

// ---------------------------------------------------------------------------------------------------------
// RecoverySession

struct RecoverySession::State {
    SessionLock lock;
    std::mutex mutex;
    std::condition_variable_any wake;
    std::vector<std::shared_ptr<DocumentJournal>> journals;
    std::jthread thread;
};

RecoverySession::RecoverySession(base::SessionId id, std::filesystem::path directory, Options options, int lockFd)
    : id_(id), directory_(std::move(directory)), options_(options), state_(std::make_unique<State>()) {
    (void)lockFd;
}

base::Result<std::unique_ptr<RecoverySession>> RecoverySession::start(const std::filesystem::path &root,
                                                                     Options options) {
    const FilesystemInfo info = filesystemInfo(root);
    if (!info.local) {
        return base::fail(ErrorCategory::PermissionDenied, "recovery.network_filesystem",
                          "Recovery files cannot be kept on a network folder", root.string() + ": " + info.type);
    }
    const base::SessionId id = base::SessionId::generate();
    const std::filesystem::path directory = root / "sessions" / id.toString();
    std::error_code ec;
    std::filesystem::create_directories(directory, ec);
    if (ec) {
        return base::fail(ErrorCategory::PermissionDenied, "recovery.directory",
                          "The recovery folder could not be created", directory.string() + ": " + ec.message());
    }
    std::filesystem::permissions(directory, std::filesystem::perms::owner_all, ec);
    SessionLock lock;
    if (!lock.tryAcquire(directory / kLockName)) {
        return base::fail(ErrorCategory::PermissionDenied, "recovery.lock", "The recovery folder could not be locked",
                          (directory / kLockName).string());
    }
    std::unique_ptr<RecoverySession> session(new RecoverySession(id, directory, options, -1));
    session->state_->lock = std::move(lock);
    if (options.runThread) {
        session->state_->thread = std::jthread([raw = session.get()](std::stop_token stop) { raw->run(stop); });
    }
    return session;
}

RecoverySession::~RecoverySession() {
    if (state_->thread.joinable()) {
        state_->thread.request_stop();
        state_->wake.notify_all();
        state_->thread.join();
    }
    std::vector<std::shared_ptr<DocumentJournal>> journals;
    {
        const std::lock_guard lock(state_->mutex);
        journals.swap(state_->journals);
    }
    for (const auto &journal : journals) {
        // Keep the file for recovery; stop observing and writing.
        DocumentJournal::Impl &impl = *journal->impl_;
        const std::lock_guard write(impl.writeMutex);
        impl.closed = true;
        impl.db.reset();
    }
    if (!hasJournals(directory_)) {
        std::error_code ignored;
        std::filesystem::remove(directory_ / kLockName, ignored);
        state_->lock.release();
        std::filesystem::remove(directory_, ignored);
    }
}

void RecoverySession::run(std::stop_token stop) {
    while (!stop.stop_requested()) {
        std::vector<std::shared_ptr<DocumentJournal>> journals;
        {
            std::unique_lock lock(state_->mutex);
            state_->wake.wait_for(lock, stop, options_.cadence, [] { return false; });
            if (stop.stop_requested()) {
                return;
            }
            journals = state_->journals;
        }
        for (const auto &journal : journals) {
            (void)journal->flush(); // failures stay in lastError(); the next tick retries
        }
    }
}

base::Result<std::shared_ptr<DocumentJournal>> RecoverySession::track(commands::MutationExecutor &executor,
                                                                      std::filesystem::path sourcePath) {
    const auto current = executor.current();
    const base::DocumentId documentId = current->state->id;
    std::filesystem::path path = directory_ / (documentId.toString() + std::string(kJournalExtension));
    for (int n = 2; std::filesystem::exists(path); ++n) {
        path = directory_ / (documentId.toString() + "-" + std::to_string(n) + std::string(kJournalExtension));
    }
    std::shared_ptr<DocumentJournal> journal(new DocumentJournal(path, documentId, *this));
    DocumentJournal::Impl &impl = *journal->impl_;

    auto db = Database::open(path, Database::Mode::ReadWriteCreate);
    if (!db) {
        return fail(db.error());
    }
    for (const std::string_view sql : {std::string_view("PRAGMA auto_vacuum = INCREMENTAL"),
                                       std::string_view("PRAGMA journal_mode = WAL"),
                                       std::string_view("PRAGMA synchronous = FULL"), schemaSql()}) {
        if (auto status = db->exec(sql); !status) {
            removeDatabaseFiles(path);
            return fail(status.error());
        }
    }
    auto reader = Database::open(path, Database::Mode::ReadWriteCreate);
    if (!reader) {
        removeDatabaseFiles(path);
        return fail(reader.error());
    }
    impl.db.emplace(std::move(*db));
    impl.spool = std::make_shared<JournalSpool>(std::move(*reader));
    {
        const std::lock_guard lock(impl.statusMutex);
        impl.sourcePath = std::move(sourcePath);
        impl.latest = current;
    }
    impl.executor = &executor;
    impl.subscription = executor.subscribe([weak = std::weak_ptr<DocumentJournal>(journal)](
                                               const std::shared_ptr<const document::Revision> &revision) {
        if (const auto alive = weak.lock()) {
            const std::lock_guard lock(alive->impl_->statusMutex);
            alive->impl_->latest = revision;
        }
    });
    {
        const std::lock_guard lock(state_->mutex);
        state_->journals.push_back(journal);
    }
    return journal;
}

base::Status RecoverySession::untrack(const std::shared_ptr<DocumentJournal> &journal, bool discard) {
    {
        const std::lock_guard lock(state_->mutex);
        std::erase(state_->journals, journal);
    }
    DocumentJournal::Impl &impl = *journal->impl_;
    if (impl.executor != nullptr) {
        impl.executor->unsubscribe(impl.subscription);
        impl.executor = nullptr;
    }
    base::Status status;
    {
        const std::lock_guard write(impl.writeMutex); // waits for a write in progress
        impl.closed = true;
        impl.db.reset();
    }
    if (discard) {
        // Tiles still alive elsewhere must not depend on a journal that is about to disappear.
        status = tiles::TileSpool::global().detach(impl.spool.get());
        if (status) {
            removeDatabaseFiles(journal->path());
        }
    }
    return status;
}

base::Status RecoverySession::flush() {
    std::vector<std::shared_ptr<DocumentJournal>> journals;
    {
        const std::lock_guard lock(state_->mutex);
        journals = state_->journals;
    }
    base::Status first;
    for (const auto &journal : journals) {
        if (auto status = journal->flush(); !status && first) {
            first = std::move(status);
        }
    }
    return first;
}

// ---------------------------------------------------------------------------------------------------------
// Discovery

namespace {

std::string valueOf(const std::vector<std::pair<std::string, std::string>> &manifest, std::string_view key) {
    for (const auto &[k, v] : manifest) {
        if (k == key) {
            return v;
        }
    }
    return {};
}

// Reads one journal's identity and summary. nullopt: the file is not an ArchMage recovery journal.
std::optional<RecoveryCandidate> readCandidate(const std::filesystem::path &path) {
    RecoveryCandidate candidate;
    candidate.journalPath = path;
    auto open = detail::openAndIdentify(path, Database::Mode::ReadWriteCreate);
    if (!open) {
        if (open.error().code == "file.not_archmage") {
            return std::nullopt;
        }
        candidate.problem = open.error();
        return candidate;
    }
    auto manifest = detail::loadManifest(open->file);
    if (!manifest) {
        candidate.problem = manifest.error();
        return candidate;
    }
    const std::string session = valueOf(*manifest, kSessionKey);
    if (session.empty()) {
        return std::nullopt; // an ordinary document, not a journal
    }
    if (auto id = base::SessionId::parse(session)) {
        candidate.sessionId = *id;
    }
    const FileInfo &info = open->info;
    candidate.documentId = info.documentId;
    candidate.revisionId = info.revisionId;
    candidate.revisionSequence = info.revisionSequence;
    candidate.revisionLabel = info.revisionLabel;
    candidate.journaledAt = valueOf(*manifest, kWrittenAtKey);
    candidate.sourcePath = valueOf(*manifest, kSourcePathKey);
    candidate.savedRevisionId = valueOf(*manifest, kSavedRevisionKey);
    candidate.upToDate = !candidate.savedRevisionId.empty() && candidate.savedRevisionId == candidate.revisionId;
    if (auto title = open->source().prepare("SELECT payload FROM objects WHERE id = ?")) {
        title->bind(1, info.documentId.toString());
        if (title->step()) {
            const json payload = json::parse(title->text(0), nullptr, false);
            if (payload.is_object() && payload.contains("title") && payload["title"].is_string()) {
                candidate.title = payload["title"].get<std::string>();
            }
        }
    }
    candidate.thumbnail = detail::readPreview(*open).preview;
    return candidate;
}

} // namespace

base::Result<std::vector<RecoveryCandidate>> listRecoveryCandidates(const std::filesystem::path &root) {
    std::vector<RecoveryCandidate> candidates;
    const std::filesystem::path sessions = root / "sessions";
    std::error_code ec;
    if (!std::filesystem::is_directory(sessions, ec)) {
        return candidates;
    }
    for (const auto &directory : std::filesystem::directory_iterator(sessions, ec)) {
        if (!directory.is_directory()) {
            continue;
        }
        SessionLock lock;
        if (!lock.tryAcquire(directory.path() / kLockName)) {
            continue; // a running session (or one we cannot lock): not ours to recover
        }
        for (const auto &entry : std::filesystem::directory_iterator(directory.path(), ec)) {
            if (!isJournalFile(entry)) {
                continue;
            }
            if (auto candidate = readCandidate(entry.path())) {
                candidates.push_back(std::move(*candidate));
            }
        }
    }
    std::sort(candidates.begin(), candidates.end(), [](const RecoveryCandidate &a, const RecoveryCandidate &b) {
        return a.journaledAt > b.journaledAt; // newest first
    });
    return candidates;
}

base::Result<OpenedDocument> recoverDocument(const RecoveryCandidate &candidate) {
    SessionLock lock;
    if (!lock.tryAcquire(candidate.journalPath.parent_path() / kLockName)) {
        return base::fail(ErrorCategory::Conflict, "recovery.session_live",
                          "That document is still open in a running ArchMage", candidate.journalPath.string());
    }
    auto open = detail::openAndIdentify(candidate.journalPath, Database::Mode::ReadWriteCreate);
    if (!open) {
        return fail(open.error());
    }
    auto opened = detail::readOpen(*open, ReadOptions{});
    if (!opened) {
        return opened;
    }
    if (opened->state && opened->state->preserved) {
        // The journal's own bookkeeping is not document content.
        auto preserved = std::make_shared<document::PreservedContent>(*opened->state->preserved);
        std::erase_if(preserved->manifest, [](const auto &entry) { return entry.first.starts_with(kJournalKeyPrefix); });
        auto state = std::make_shared<document::DocumentState>(*opened->state);
        state->preserved = preserved->empty() ? nullptr : std::shared_ptr<const document::PreservedContent>(preserved);
        opened->state = std::move(state);
    }
    return opened;
}

base::Status discardRecovery(const RecoveryCandidate &candidate) {
    const std::filesystem::path directory = candidate.journalPath.parent_path();
    SessionLock lock;
    if (!lock.tryAcquire(directory / kLockName)) {
        return base::fail(ErrorCategory::Conflict, "recovery.session_live",
                          "That document is still open in a running ArchMage", candidate.journalPath.string());
    }
    removeDatabaseFiles(candidate.journalPath);
    if (!hasJournals(directory)) {
        std::error_code ignored;
        std::filesystem::remove(directory / kLockName, ignored);
        lock.release();
        std::filesystem::remove(directory, ignored);
    }
    return {};
}

} // namespace archmage::native
