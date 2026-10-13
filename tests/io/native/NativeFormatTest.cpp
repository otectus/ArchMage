// S1-4.1 to S1-4.6: schema, blob codec, writer and reader round trips, preservation of unknown content,
// read-only opening of newer files, the save transaction and file identity.

#include "NativeTestSupport.h"

#include "archmage/base/Sha256.h"
#include "archmage/native/Blob.h"
#include "archmage/native/Format.h"
#include "archmage/native/Platform.h"
#include "archmage/native/Sqlite.h"

#include <gtest/gtest.h>

#include <cmath>
#include <fstream>
#include <future>
#include <limits>
#include <thread>

namespace archmage::native {
namespace {

using commands::CommandEnvelope;
using nlohmann::json;
using testing::ScratchDirectory;

std::vector<std::byte> bytesOf(std::string_view text) {
    const auto view = std::as_bytes(std::span(text.data(), text.size()));
    return {view.begin(), view.end()};
}

std::string scalar(const std::filesystem::path &file, const std::string &sql) {
    auto db = Database::open(file, Database::Mode::ReadOnly);
    EXPECT_TRUE(db);
    auto statement = db->prepare(sql);
    EXPECT_TRUE(statement) << sql;
    if (!statement || !statement->step()) {
        return {};
    }
    return statement->text(0);
}

TEST(NativeSchema, AppliesToAnEmptyDatabaseAndPassesTheIntegrityCheck) {
    auto db = Database::openMemory();
    ASSERT_TRUE(db);
    ASSERT_TRUE(db->exec(schemaSql()));
    auto check = db->prepare("PRAGMA integrity_check");
    ASSERT_TRUE(check);
    ASSERT_TRUE(check->step());
    EXPECT_EQ(check->text(0), "ok");
    auto id = db->prepare("PRAGMA application_id");
    ASSERT_TRUE(id && id->step());
    EXPECT_EQ(id->integer(0), kApplicationId);
    auto tables = db->prepare("SELECT COUNT(*) FROM sqlite_schema WHERE type = 'table'");
    ASSERT_TRUE(tables && tables->step());
    EXPECT_EQ(tables->integer(0), 8);
}

TEST(NativeBlob, RoundTripsCompressibleAndIncompressibleData) {
    std::vector<std::byte> flat(65536, std::byte{7});
    const Blob compressed = encodeBlob(flat, "test/flat");
    EXPECT_EQ(compressed.codec, "zstd");
    EXPECT_LT(compressed.data.size(), flat.size() / 10);
    EXPECT_EQ(compressed.hash, base::Sha256::of(flat).hex());
    auto decoded = decodeBlob(compressed);
    ASSERT_TRUE(decoded);
    EXPECT_EQ(*decoded, flat);

    std::vector<std::byte> noise(4096);
    std::uint32_t state = 12345;
    for (std::byte &b : noise) {
        state = state * 1664525u + 1013904223u;
        b = static_cast<std::byte>(state >> 24u);
    }
    const Blob raw = encodeBlob(noise, "test/noise");
    EXPECT_EQ(raw.codec, "raw");
    auto decodedNoise = decodeBlob(raw);
    ASSERT_TRUE(decodedNoise);
    EXPECT_EQ(*decodedNoise, noise);
}

TEST(NativeBlob, TamperingIsDamagedInputNeverWrongBytes) {
    std::vector<std::byte> plane(65536);
    for (std::size_t i = 0; i < plane.size(); ++i) {
        plane[i] = static_cast<std::byte>((i * 7) % 251);
    }
    const Blob good = encodeBlob(plane, std::string(kTilePlaneMedia));
    ASSERT_EQ(good.codec, "zstd");
    for (std::size_t offset : {std::size_t{0}, good.data.size() / 2, good.data.size() - 1}) {
        Blob tampered = good;
        tampered.data[offset] ^= std::byte{0x40};
        auto result = decodeBlob(tampered);
        ASSERT_FALSE(result) << "offset " << offset;
        EXPECT_EQ(result.error().category, base::ErrorCategory::DamagedInput) << base::format(result.error());
    }
    Blob wrongHash = good;
    wrongHash.hash[3] = wrongHash.hash[3] == 'a' ? 'b' : 'a';
    EXPECT_EQ(decodeBlob(wrongHash).error().code, "blob.damaged");
    Blob wrongLength = good;
    wrongLength.rawLength += 1;
    EXPECT_EQ(decodeBlob(wrongLength).error().category, base::ErrorCategory::DamagedInput);
    Blob huge = good;
    huge.rawLength = kMaxBlobBytes + 1;
    EXPECT_EQ(decodeBlob(huge).error().category, base::ErrorCategory::DamagedInput);
    Blob unknown = good;
    unknown.codec = "lzma";
    EXPECT_EQ(decodeBlob(unknown).error().category, base::ErrorCategory::UnsupportedInput);
    Blob truncatedRaw = encodeBlob(bytesOf("short"), "test/raw");
    truncatedRaw.data.pop_back();
    EXPECT_EQ(decodeBlob(truncatedRaw).error().category, base::ErrorCategory::DamagedInput);
}

TEST(NativeFile, RoundTripIsStructurallyEqualAndByteExactForTiles) {
    ScratchDirectory dir(testing::tmpRoot());
    testing::SampleDocument doc = testing::buildSampleDocument(700, 500);
    const auto revision = doc.executor->current();
    const document::DocumentState &before = *revision->state;
    auto saved = testing::save(revision, dir / "round.archmage", identityForSave(before, std::nullopt));
    ASSERT_TRUE(saved) << base::format(saved.error());
    EXPECT_GT(saved->report.resources, 0u);
    EXPECT_LT(saved->report.encodedBytes, saved->report.rawBytes);

    auto opened = readDocument(dir / "round.archmage");
    ASSERT_TRUE(opened) << base::format(opened.error());
    ASSERT_TRUE(opened->state);
    EXPECT_FALSE(opened->info.readOnly);
    const document::DocumentState &after = *opened->state;
    EXPECT_EQ(after.id, before.id);
    EXPECT_EQ(after.title, before.title);
    EXPECT_EQ(after.width, before.width);
    EXPECT_EQ(after.height, before.height);
    EXPECT_EQ(after.resolution, before.resolution);
    EXPECT_EQ(after.unit, before.unit);
    EXPECT_EQ(after.sampleType, before.sampleType);
    EXPECT_TRUE(after.colorPolicy.profile->sameAs(*before.colorPolicy.profile));
    EXPECT_EQ(after.colorPolicy.blendPolicy, before.colorPolicy.blendPolicy);
    EXPECT_EQ(after.capabilities, before.capabilities);
    EXPECT_EQ(after.extensions, before.extensions);
    EXPECT_FALSE(after.preserved) << "nothing unknown in a file this version wrote";
    ASSERT_EQ(after.layers.size(), before.layers.size());
    for (std::size_t i = 0; i < before.layers.size(); ++i) {
        const document::Layer &a = *before.layers[i];
        const document::Layer &b = *after.layers[i];
        SCOPED_TRACE(a.name);
        EXPECT_EQ(b.id, a.id);
        EXPECT_EQ(b.name, a.name);
        EXPECT_EQ(b.visible, a.visible);
        EXPECT_EQ(b.locked, a.locked);
        EXPECT_EQ(b.opacity, a.opacity);
        EXPECT_EQ(b.blendMode, a.blendMode);
        EXPECT_EQ(b.offset, a.offset);
        EXPECT_EQ(b.extensions, a.extensions);
        EXPECT_EQ(b.content->id, a.content->id);
        EXPECT_TRUE(testing::sameTiles(a.content->tiles, b.content->tiles));
    }
    // The instance still shares its raster with the layer it was duplicated from.
    const auto ink = base::LayerId::parse(doc.ink);
    const auto instance = base::LayerId::parse(doc.instance);
    ASSERT_TRUE(ink && instance);
    EXPECT_EQ(after.findLayer(*instance)->content, after.findLayer(*ink)->content);
    EXPECT_EQ(testing::composite(after), testing::composite(before));
    EXPECT_EQ(opened->info.revisionId, revision->id.toString());
    EXPECT_EQ(opened->info.revisionSequence, revision->sequence);
}

TEST(NativeFile, Float32SamplesSurviveBitForBit) {
    ScratchDirectory dir(testing::tmpRoot());
    testing::SampleDocument doc = testing::buildSampleDocument(260, 140, "float32");
    // Extended-range and non-finite values are stored as given (D-09); the file must not normalize them.
    auto state = std::make_shared<document::DocumentState>(*doc.executor->current()->state);
    auto layer = std::make_shared<document::Layer>(*state->layers[1]);
    auto content = std::make_shared<document::RasterContent>(*layer->content);
    tiles::TileBuffer buffer(*content->tiles.at({1, 0}));
    buffer.setSample(0, 3, 4, -0.25f);
    buffer.setSample(1, 5, 6, 7.5f);
    buffer.setSample(2, 7, 8, std::numeric_limits<float>::quiet_NaN());
    buffer.setSample(3, 9, 10, std::numeric_limits<float>::denorm_min());
    content->tiles.set({1, 0}, std::move(buffer).seal());
    layer->content = content;
    state->layers[1] = layer;
    auto revision = std::make_shared<document::Revision>(*doc.executor->current());
    revision->state = state;

    auto saved = testing::save(revision, dir / "float.archmage", identityForSave(*state, std::nullopt));
    ASSERT_TRUE(saved) << base::format(saved.error());
    auto opened = readDocument(dir / "float.archmage");
    ASSERT_TRUE(opened) << base::format(opened.error());
    ASSERT_EQ(opened->state->sampleType, tiles::SampleType::Float32);
    const auto tile = opened->state->layers[1]->content->tiles.at({1, 0});
    EXPECT_EQ(tile->sample(0, 3, 4), -0.25f);
    EXPECT_EQ(tile->sample(1, 5, 6), 7.5f);
    EXPECT_TRUE(std::isnan(tile->sample(2, 7, 8)));
    EXPECT_EQ(tile->sample(3, 9, 10), std::numeric_limits<float>::denorm_min());
    EXPECT_TRUE(testing::sameTiles(content->tiles, opened->state->layers[1]->content->tiles));
}

TEST(NativeFile, StoresAPreviewOfTheCompositeAtMost512Pixels) {
    ScratchDirectory dir(testing::tmpRoot());
    testing::SampleDocument doc = testing::buildSampleDocument(1500, 600);
    const auto revision = doc.executor->current();
    ASSERT_TRUE(testing::save(revision, dir / "preview.archmage", identityForSave(*revision->state, std::nullopt)));
    auto opened = readDocument(dir / "preview.archmage");
    ASSERT_TRUE(opened);
    ASSERT_TRUE(opened->preview);
    EXPECT_EQ(opened->preview->width, 512);
    EXPECT_EQ(opened->preview->height, 205);
    ASSERT_TRUE(opened->previewProfile);
    EXPECT_TRUE(opened->previewProfile->sameAs(*revision->state->colorPolicy.profile));
    // Nearest sampling: a preview pixel equals the composite at the document pixel under its center.
    const std::vector<std::byte> full = testing::composite(*revision->state);
    const std::pair<int, int> points[] = {{0, 0}, {255, 100}, {511, 204}, {100, 30}};
    for (const auto &[px, py] : points) {
        const auto dx = static_cast<std::int64_t>((px + 0.5) * 1500.0 / 512.0);
        const auto dy = static_cast<std::int64_t>((py + 0.5) * 600.0 / 205.0);
        for (int c = 0; c < 4; ++c) {
            EXPECT_EQ(opened->preview->rgba[static_cast<std::size_t>((py * 512 + px) * 4 + c)],
                      full[static_cast<std::size_t>((dy * 1500 + dx) * 4 + c)])
                << px << "," << py << " channel " << c;
        }
    }
    // Small documents keep their size.
    auto small = renderPreview(*revision->state, 4096);
    ASSERT_TRUE(small);
    EXPECT_EQ(small->width, 1500);
}

TEST(NativeFile, ATamperedTileBlobFailsToOpenAsDamagedInput) {
    ScratchDirectory dir(testing::tmpRoot());
    testing::SampleDocument doc = testing::buildSampleDocument(300, 200);
    const auto revision = doc.executor->current();
    const auto file = dir / "tampered.archmage";
    ASSERT_TRUE(testing::save(revision, file, identityForSave(*revision->state, std::nullopt)));
    // Flip one bit in the middle of one stored tile plane.
    {
        auto db = Database::open(file, Database::Mode::ReadWriteCreate);
        ASSERT_TRUE(db);
        auto select = db->prepare("SELECT rowid, data FROM resources WHERE media_type = "
                                  "'application/vnd.archmage.tile-plane' ORDER BY hash LIMIT 1");
        ASSERT_TRUE(select && select->step());
        const std::int64_t row = select->integer(0);
        std::vector<std::byte> data = select->blob(1);
        data[data.size() / 2] ^= std::byte{0x01};
        auto update = db->prepare("UPDATE resources SET data = ? WHERE rowid = ?");
        ASSERT_TRUE(update);
        update->bind(1, data).bind(2, row);
        update->step();
        ASSERT_TRUE(update->status());
    }
    auto opened = readDocument(file);
    ASSERT_FALSE(opened);
    EXPECT_EQ(opened.error().category, base::ErrorCategory::DamagedInput) << base::format(opened.error());
    auto report = validateFile(file, ValidationLevel::Standard);
    ASSERT_TRUE(report);
    EXPECT_FALSE(report->valid());
}

TEST(NativeFile, RejectsFilesThatAreNotArchMageDocuments) {
    ScratchDirectory dir(testing::tmpRoot());
    {
        std::ofstream text(dir / "notes.archmage");
        text << "definitely not sqlite, but long enough to look like a header: 0123456789012345678901234567890123456789";
    }
    auto notDb = readDocument(dir / "notes.archmage");
    ASSERT_FALSE(notDb);
    EXPECT_EQ(notDb.error().category, base::ErrorCategory::DamagedInput) << base::format(notDb.error());

    testing::execSql(dir / "other.db", "CREATE TABLE t (x)");
    auto otherDb = readDocument(dir / "other.db");
    ASSERT_FALSE(otherDb);
    EXPECT_EQ(otherDb.error().code, "file.not_archmage");

    auto missing = readDocument(dir / "missing.archmage");
    ASSERT_FALSE(missing);
    EXPECT_EQ(missing.error().code, "file.not_found");

    // A view standing in for a table is refused before any query reads through it.
    testing::SampleDocument doc = testing::buildSampleDocument(64, 64);
    const auto file = dir / "view.archmage";
    ASSERT_TRUE(testing::save(doc.executor->current(), file, identityForSave(*doc.executor->current()->state, {})));
    testing::execSql(file, "ALTER TABLE objects RENAME TO objects_real; CREATE VIEW objects AS SELECT * FROM "
                           "objects_real");
    auto viewed = readDocument(file);
    ASSERT_FALSE(viewed);
    EXPECT_EQ(viewed.error().code, "file.schema_missing");
}

// Spec §10.5: unknown objects, their relations, blobs, tile planes, assets and manifest keys survive
// open then save; a relation whose known endpoint was deleted is dropped and counted.
TEST(NativeFile, UnknownContentSurvivesOpenThenSave) {
    ScratchDirectory dir(testing::tmpRoot());
    testing::SampleDocument doc = testing::buildSampleDocument(300, 200);
    const auto revision = doc.executor->current();
    const auto original = dir / "original.archmage";
    ASSERT_TRUE(testing::save(revision, original, identityForSave(*revision->state, std::nullopt)));

    const std::string payload = R"({"z": 1, "a": [1.0000000000000002, 18446744073709551615, "x"], "nested": {"k": null}})";
    const Blob blob = encodeBlob(bytesOf("opaque resource bytes, kept as stored"), "application/x-future");
    testing::execSql(original, "INSERT INTO objects VALUES ('future-1', 'vector.path', 3, '" + payload + "');"
                               "INSERT INTO objects VALUES ('raster-orphan', 'raster', 1, '{\"from\": \"future\"}');"
                               "INSERT INTO relations VALUES ('" + doc.ink + "', 'future.mask', 0, 'future-1');"
                               "INSERT INTO relations VALUES ('future-1', 'future.source', 0, 'raster-orphan');"
                               "INSERT INTO tile_maps VALUES ('raster-orphan', 0, 0, 0, 0, x'7f', NULL);"
                               "INSERT INTO tile_maps VALUES ('raster-orphan', 0, 1, 0, 0, NULL, '" + blob.hash + "');"
                               "INSERT INTO assets VALUES ('asset-1', 'linked', '{\"uri\": \"file:///x.png\"}');"
                               "INSERT INTO manifest VALUES ('future_key', 'future value');");
    {
        auto db = Database::open(original, Database::Mode::ReadWriteCreate);
        ASSERT_TRUE(db);
        auto insert = db->prepare("INSERT INTO resources (hash, media_type, codec, codec_version, raw_length, data) "
                                  "VALUES (?, ?, ?, ?, ?, ?)");
        ASSERT_TRUE(insert);
        insert->bind(1, blob.hash).bind(2, blob.mediaType).bind(3, blob.codec);
        insert->bind(4, std::int64_t{blob.codecVersion}).bind(5, static_cast<std::int64_t>(blob.rawLength));
        insert->bind(6, blob.data);
        insert->step();
        ASSERT_TRUE(insert->status());
    }

    auto opened = readDocument(original);
    ASSERT_TRUE(opened) << base::format(opened.error());
    ASSERT_TRUE(opened->state->preserved);
    const document::PreservedContent &preserved = *opened->state->preserved;
    EXPECT_EQ(preserved.objects.size(), 2u);
    EXPECT_EQ(preserved.relations.size(), 2u);
    EXPECT_EQ(preserved.tilePlanes.size(), 2u);
    EXPECT_EQ(preserved.resources.size(), 1u);
    EXPECT_EQ(preserved.assets.size(), 1u);
    ASSERT_EQ(preserved.manifest.size(), 1u);

    // Save the reopened document under its identity, as a session would.
    auto reopened = std::make_shared<document::Revision>(*revision);
    reopened->state = opened->state;
    const auto copy = dir / "copy.archmage";
    auto saved = testing::save(reopened, copy, identityForSave(*opened->state, std::nullopt));
    ASSERT_TRUE(saved) << base::format(saved.error());
    EXPECT_EQ(saved->report.droppedOpaqueRelations, 0u);
    EXPECT_EQ(scalar(copy, "SELECT payload FROM objects WHERE id = 'future-1'"), payload)
        << "opaque payload text is written back byte for byte";
    EXPECT_EQ(scalar(copy, "SELECT type || ':' || version FROM objects WHERE id = 'future-1'"), "vector.path:3");
    EXPECT_EQ(scalar(copy, "SELECT COUNT(*) FROM relations WHERE kind LIKE 'future.%'"), "2");
    EXPECT_EQ(scalar(copy, "SELECT COUNT(*) FROM tile_maps WHERE raster_id = 'raster-orphan'"), "2");
    EXPECT_EQ(scalar(copy, "SELECT hex(data) FROM resources WHERE hash = '" + blob.hash + "'"),
              scalar(original, "SELECT hex(data) FROM resources WHERE hash = '" + blob.hash + "'"));
    EXPECT_EQ(scalar(copy, "SELECT payload FROM assets WHERE id = 'asset-1'"), "{\"uri\": \"file:///x.png\"}");
    EXPECT_EQ(scalar(copy, "SELECT value FROM manifest WHERE key = 'future_key'"), "future value");
    auto validated = validateFile(copy, ValidationLevel::Deep);
    ASSERT_TRUE(validated);
    EXPECT_TRUE(validated->valid()) << base::format(validated->problems.front());

    // Removing the layer the unknown mask hangs from drops that one relation, and says so.
    commands::MutationExecutor executor(*opened->state, "Open");
    ASSERT_TRUE(executor.execute(CommandEnvelope::make("archmage.layer.remove", {}, {doc.instance})));
    ASSERT_TRUE(executor.execute(CommandEnvelope::make("archmage.layer.remove", {}, {doc.ink})));
    auto pruned = testing::save(executor.current(), dir / "pruned.archmage",
                                identityForSave(*executor.current()->state, std::nullopt));
    ASSERT_TRUE(pruned) << base::format(pruned.error());
    EXPECT_EQ(pruned->report.droppedOpaqueRelations, 1u);
    auto prunedReport = validateFile(dir / "pruned.archmage", ValidationLevel::Deep);
    ASSERT_TRUE(prunedReport);
    EXPECT_TRUE(prunedReport->valid());
}

TEST(NativeFile, NewerOrUnknownFilesOpenReadOnlyWithTheirPreview) {
    ScratchDirectory dir(testing::tmpRoot());
    testing::SampleDocument doc = testing::buildSampleDocument(320, 240);
    const auto revision = doc.executor->current();
    const auto write = [&](const char *name, const std::string &sql) {
        const auto file = dir / name;
        EXPECT_TRUE(testing::save(revision, file, identityForSave(*revision->state, std::nullopt)));
        testing::execSql(file, sql);
        return file;
    };

    const auto major = write("major.archmage", "UPDATE manifest SET value = '1' WHERE key = 'schema_major';"
                                               "DROP TABLE objects; CREATE TABLE objects (changed BLOB);");
    auto newerMajor = readDocument(major);
    ASSERT_TRUE(newerMajor) << base::format(newerMajor.error());
    EXPECT_TRUE(newerMajor->info.readOnly);
    EXPECT_EQ(newerMajor->info.readOnlyReason, "schema.newer_major");
    EXPECT_NE(newerMajor->info.readOnlyExplanation.find("1.1"), std::string::npos);
    EXPECT_FALSE(newerMajor->state);
    ASSERT_TRUE(newerMajor->preview);
    EXPECT_EQ(newerMajor->preview->width, 320);
    EXPECT_EQ(newerMajor->info.documentId, revision->state->id);

    const auto minor = write("minor.archmage", "UPDATE manifest SET value = '99' WHERE key = 'schema_minor'");
    auto newerMinor = readDocument(minor);
    ASSERT_TRUE(newerMinor);
    EXPECT_TRUE(newerMinor->info.readOnly);
    EXPECT_EQ(newerMinor->info.readOnlyReason, "schema.newer_minor");
    ASSERT_TRUE(newerMinor->state) << "a newer minor of the same major still loads";
    EXPECT_TRUE(newerMinor->preview);

    const auto capability = write("capability.archmage",
                                  "UPDATE manifest SET value = '[\"raster.layers.v1\", \"vector.paths.v1\"]' "
                                  "WHERE key = 'required_capabilities'");
    auto unknown = readDocument(capability);
    ASSERT_TRUE(unknown);
    EXPECT_TRUE(unknown->info.readOnly);
    EXPECT_EQ(unknown->info.readOnlyReason, "capability.unknown");
    EXPECT_NE(unknown->info.readOnlyExplanation.find("vector.paths.v1"), std::string::npos);
    EXPECT_FALSE(unknown->state);
    EXPECT_TRUE(unknown->preview);
}

// Spec §6.2: Save keeps the document identity; Save As makes a new one that records where it came from.
TEST(NativeSave, SaveKeepsIdentityAndSaveAsDerivesANewOne) {
    ScratchDirectory dir(testing::tmpRoot());
    testing::SampleDocument doc = testing::buildSampleDocument(200, 120);
    const auto revision = doc.executor->current();
    const document::DocumentState &state = *revision->state;

    const FileIdentity first = identityForSave(state, std::nullopt);
    EXPECT_EQ(first.id, state.id);
    EXPECT_TRUE(identityForSaveAs(state, std::nullopt).derivedFrom.isNull()) << "first save of a new document";
    ASSERT_TRUE(testing::save(revision, dir / "a.archmage", first));
    auto info = readFileInfo(dir / "a.archmage");
    ASSERT_TRUE(info);
    EXPECT_EQ(info->documentId, state.id);
    EXPECT_TRUE(info->derivedFrom.isNull());

    const FileIdentity again = identityForSave(state, first);
    EXPECT_EQ(again.id, first.id);
    ASSERT_TRUE(testing::save(revision, dir / "a.archmage", again));
    EXPECT_EQ(readFileInfo(dir / "a.archmage")->documentId, state.id);

    const FileIdentity copy = identityForSaveAs(state, first);
    EXPECT_NE(copy.id, first.id);
    EXPECT_EQ(copy.derivedFrom, first.id);
    ASSERT_TRUE(testing::save(revision, dir / "b.archmage", copy));
    auto copied = readDocument(dir / "b.archmage");
    ASSERT_TRUE(copied);
    EXPECT_EQ(copied->info.documentId, copy.id);
    EXPECT_EQ(copied->info.derivedFrom, first.id);
    EXPECT_EQ(copied->state->id, copy.id) << "the reopened document carries its file's identity";
    EXPECT_EQ(readFileInfo(dir / "a.archmage")->documentId, state.id) << "the original is untouched";
}

class NativeSaveOnFilesystem : public ::testing::TestWithParam<const char *> {
protected:
    [[nodiscard]] std::filesystem::path root() const {
        return std::string_view(GetParam()) == "tmp" ? testing::tmpRoot() : testing::homeRoot();
    }
};

// Spec §10.2: the saved file is complete and closed: no -wal, -shm or -journal file, no temporary left.
TEST_P(NativeSaveOnFilesystem, LeavesOneSelfContainedFile) {
    ScratchDirectory dir(root());
    testing::SampleDocument doc = testing::buildSampleDocument(400, 300);
    const auto revision = doc.executor->current();
    std::vector<std::string> duringWrite;
    auto saved = testing::save(revision, dir / "doc.archmage", identityForSave(*revision->state, std::nullopt),
                               [&](SaveStep step) -> base::Status {
                                   if (step == SaveStep::CloseTemporary) {
                                       duringWrite = testing::directoryEntries(dir.path());
                                   }
                                   return {};
                               });
    ASSERT_TRUE(saved) << base::format(saved.error());
    RecordProperty("filesystem", saved->filesystem.type);
    ASSERT_EQ(duringWrite.size(), 1u) << "only the temporary file, no journal beside it";
    EXPECT_TRUE(duringWrite[0].starts_with("doc.archmage.tmp-")) << duringWrite[0];
    EXPECT_TRUE(saved->atomicReplace) << saved->filesystem.type;
    EXPECT_EQ(testing::directoryEntries(dir.path()), std::vector<std::string>{"doc.archmage"});
    EXPECT_EQ(scalar(dir / "doc.archmage", "PRAGMA journal_mode"), "delete")
        << "the file is not in WAL mode";
    // Saving again replaces it in place.
    ASSERT_TRUE(testing::save(revision, dir / "doc.archmage", identityForSave(*revision->state, std::nullopt)));
    EXPECT_EQ(testing::directoryEntries(dir.path()), std::vector<std::string>{"doc.archmage"});
    EXPECT_TRUE(readDocument(dir / "doc.archmage"));
}

// Spec §10.3 step 7: whatever step fails, the previous file is intact and no temporary file remains.
TEST_P(NativeSaveOnFilesystem, AFailedStepLeavesThePreviousFileIntact) {
    ScratchDirectory dir(root());
    testing::SampleDocument doc = testing::buildSampleDocument(256, 256);
    const auto first = doc.executor->current();
    const auto file = dir / "doc.archmage";
    ASSERT_TRUE(testing::save(first, file, identityForSave(*first->state, std::nullopt)));
    const std::string before = testing::fileHash(file);
    ASSERT_TRUE(doc.executor->execute(CommandEnvelope::make("archmage.layer.rename", {{"name", "Renamed"}}, {doc.ink})));
    const auto second = doc.executor->current();

    for (const SaveStep failing : {SaveStep::WriteTemporary, SaveStep::CloseTemporary, SaveStep::Validate,
                                   SaveStep::SyncTemporary, SaveStep::Replace}) {
        SCOPED_TRACE(saveStepName(failing));
        auto result = testing::save(second, file, identityForSave(*second->state, std::nullopt),
                                    [failing](SaveStep step) -> base::Status {
                                        if (step == failing) {
                                            return base::fail(base::ErrorCategory::ResourceExhausted, "test.enospc",
                                                              "Injected failure");
                                        }
                                        return {};
                                    });
        ASSERT_FALSE(result);
        EXPECT_EQ(result.error().code, "test.enospc");
        EXPECT_NE(result.error().detail.find(saveStepName(failing)), std::string::npos);
        EXPECT_EQ(testing::fileHash(file), before);
        EXPECT_EQ(testing::directoryEntries(dir.path()), std::vector<std::string>{"doc.archmage"});
    }
    // After the replacement, a failure is reported but the new file is already in place.
    auto late = testing::save(second, file, identityForSave(*second->state, std::nullopt), [](SaveStep step) -> base::Status {
        if (step == SaveStep::SyncDirectory) {
            return base::fail(base::ErrorCategory::Internal, "test.sync", "Injected failure");
        }
        return {};
    });
    ASSERT_FALSE(late);
    auto reopened = readDocument(file);
    ASSERT_TRUE(reopened);
    EXPECT_EQ(reopened->state->findLayer(*base::LayerId::parse(doc.ink))->name, "Renamed");
}

INSTANTIATE_TEST_SUITE_P(Filesystems, NativeSaveOnFilesystem, ::testing::Values("tmp", "home"),
                         [](const auto &suite) { return std::string(suite.param); });

// Spec §10.3 step 6: the save writes the captured revision; edits committed meanwhile stay modified.
TEST(NativeSave, EditsDuringASaveLeaveTheDocumentModified) {
    ScratchDirectory dir(testing::tmpRoot());
    testing::SampleDocument doc = testing::buildSampleDocument(300, 300);
    commands::MutationExecutor &executor = *doc.executor;
    const auto captured = executor.current();
    auto saving = std::async(std::launch::async, [&] {
        return testing::save(captured, dir / "doc.archmage", identityForSave(*captured->state, std::nullopt),
                             [&](SaveStep step) -> base::Status {
                                 if (step == SaveStep::Validate) {
                                     // An edit committed while the file is being written.
                                     auto edit = executor.execute(CommandEnvelope::make(
                                         "archmage.layer.rename", {{"name", "Edited during save"}}, {doc.glaze}));
                                     EXPECT_TRUE(edit);
                                 }
                                 return {};
                             });
    });
    auto saved = saving.get();
    ASSERT_TRUE(saved) << base::format(saved.error());
    executor.markSaved(saved->sequence);
    EXPECT_EQ(saved->sequence, captured->sequence);
    EXPECT_GT(executor.current()->sequence, captured->sequence);
    EXPECT_TRUE(executor.isModified()) << "the edit made during the save is not in the file";
    auto reopened = readDocument(dir / "doc.archmage");
    ASSERT_TRUE(reopened);
    const auto glaze = base::LayerId::parse(doc.glaze);
    ASSERT_TRUE(glaze);
    EXPECT_EQ(reopened->state->findLayer(*glaze)->name, captured->state->findLayer(*glaze)->name);
    EXPECT_NE(reopened->state->findLayer(*glaze)->name, "Edited during save");
    EXPECT_EQ(reopened->info.revisionId, captured->id.toString());
    // Saving the current revision clears the modified state.
    auto current = testing::save(executor.current(), dir / "doc.archmage",
                                 identityForSave(*executor.current()->state, std::nullopt));
    ASSERT_TRUE(current);
    executor.markSaved(current->sequence);
    EXPECT_FALSE(executor.isModified());
}

// Spec §10.3: without a trustworthy atomic rename, the previous file is kept as <name>.bak.
TEST(NativeSave, NonAtomicFilesystemsKeepThePreviousFileAsBackup) {
    ScratchDirectory dir(testing::tmpRoot());
    testing::SampleDocument doc = testing::buildSampleDocument(128, 128);
    const auto revision = doc.executor->current();
    const auto file = dir / "doc.archmage";
    SaveRequest request;
    request.target = file;
    request.revision = revision;
    request.identity = identityForSave(*revision->state, std::nullopt);
    request.filesystem = FilesystemInfo{"vfat", true, false};
    auto first = saveDocument(request);
    ASSERT_TRUE(first) << base::format(first.error());
    EXPECT_FALSE(first->atomicReplace);
    EXPECT_TRUE(first->backup.empty()) << "nothing to keep on the first save";
    const std::string previous = testing::fileHash(file);
    auto second = saveDocument(request);
    ASSERT_TRUE(second) << base::format(second.error());
    EXPECT_EQ(second->backup, file.string() + ".bak");
    EXPECT_EQ(testing::fileHash(second->backup), previous);
    EXPECT_EQ(testing::directoryEntries(dir.path()), (std::vector<std::string>{"doc.archmage", "doc.archmage.bak"}));
    EXPECT_TRUE(readDocument(file));
}

TEST(NativeSave, SavesThroughASymbolicLinkAndKeepsTheLink) {
    ScratchDirectory dir(testing::tmpRoot());
    testing::SampleDocument doc = testing::buildSampleDocument(64, 64);
    const auto revision = doc.executor->current();
    std::filesystem::create_directories(dir / "real");
    ASSERT_TRUE(testing::save(revision, dir / "real" / "doc.archmage", identityForSave(*revision->state, {})));
    std::filesystem::create_symlink(dir / "real" / "doc.archmage", dir / "link.archmage");
    auto saved = testing::save(revision, dir / "link.archmage", identityForSave(*revision->state, {}));
    ASSERT_TRUE(saved) << base::format(saved.error());
    EXPECT_EQ(saved->path, dir / "real" / "doc.archmage");
    EXPECT_TRUE(std::filesystem::is_symlink(dir / "link.archmage"));
    EXPECT_TRUE(readDocument(dir / "link.archmage"));
}

TEST(NativeSave, ReportsUnwritableLocationsAsPermissionDenied) {
    ScratchDirectory dir(testing::tmpRoot());
    testing::SampleDocument doc = testing::buildSampleDocument(64, 64);
    const auto revision = doc.executor->current();
    auto missingFolder = testing::save(revision, dir / "no-such-folder" / "doc.archmage",
                                       identityForSave(*revision->state, {}));
    ASSERT_FALSE(missingFolder);
    EXPECT_EQ(missingFolder.error().category, base::ErrorCategory::PermissionDenied);
    std::filesystem::create_directories(dir / "locked");
    std::filesystem::permissions(dir / "locked", std::filesystem::perms::owner_read | std::filesystem::perms::owner_exec);
    auto readOnly = testing::save(revision, dir / "locked" / "doc.archmage", identityForSave(*revision->state, {}));
    std::filesystem::permissions(dir / "locked", std::filesystem::perms::owner_all);
    ASSERT_FALSE(readOnly);
    EXPECT_EQ(readOnly.error().category, base::ErrorCategory::PermissionDenied) << base::format(readOnly.error());
    EXPECT_TRUE(std::filesystem::is_empty(dir / "locked"));
}

TEST(NativeValidate, QuickStandardAndDeepLevels) {
    ScratchDirectory dir(testing::tmpRoot());
    testing::SampleDocument doc = testing::buildSampleDocument(300, 200);
    const auto revision = doc.executor->current();
    const auto file = dir / "doc.archmage";
    ASSERT_TRUE(testing::save(revision, file, identityForSave(*revision->state, {})));
    for (const ValidationLevel level : {ValidationLevel::Quick, ValidationLevel::Standard, ValidationLevel::Deep}) {
        auto report = validateFile(file, level);
        ASSERT_TRUE(report);
        EXPECT_TRUE(report->valid());
        EXPECT_GT(report->objects, 0u);
        EXPECT_GT(report->tilePlanes, 0u);
        EXPECT_EQ(report->resourcesVerified > 0, level == ValidationLevel::Deep);
    }
    testing::execSql(file, "DELETE FROM objects WHERE type = 'raster'");
    auto broken = validateFile(file, ValidationLevel::Quick);
    ASSERT_TRUE(broken);
    EXPECT_FALSE(broken->valid());
    EXPECT_EQ(broken->problems.front().code, "file.dangling_reference");
    auto missing = validateFile(dir / "missing.archmage", ValidationLevel::Quick);
    ASSERT_FALSE(missing) << "a file that cannot be examined is an error, not a report";
}

TEST(NativeInspect, DescribesManifestObjectsRevisionAndResources) {
    ScratchDirectory dir(testing::tmpRoot());
    testing::SampleDocument doc = testing::buildSampleDocument(300, 200);
    const auto revision = doc.executor->current();
    const auto file = dir / "doc.archmage";
    ASSERT_TRUE(testing::save(revision, file, identityForSave(*revision->state, {})));
    auto inspected = inspectFile(file);
    ASSERT_TRUE(inspected) << base::format(inspected.error());
    const json &out = *inspected;
    EXPECT_EQ(out["schema"]["major"], kSchemaMajor);
    EXPECT_EQ(out["schema"]["minor"], kSchemaMinor);
    EXPECT_EQ(out["manifest"]["document_id"], revision->state->id.toString());
    EXPECT_EQ(out["objectTypes"]["layer.raster"], 4);
    EXPECT_EQ(out["objectTypes"]["raster"], 3) << "the instance shares a raster";
    ASSERT_EQ(out["layers"].size(), 4u);
    EXPECT_EQ(out["layers"][1]["name"], "Ink");
    EXPECT_EQ(out["layers"][2]["name"], "Ink instance");
    EXPECT_EQ(out["revisions"][0]["id"], revision->id.toString());
    EXPECT_GT(out["resources"]["count"].get<int>(), 1);
    EXPECT_EQ(out["previews"].size(), 1u);
}

} // namespace
} // namespace archmage::native
