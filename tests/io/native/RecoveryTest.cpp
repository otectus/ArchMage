// S1-5.2 to S1-5.4: the recovery journal, durability states, discovery by identity, recovery and discard,
// and spooling journaled tiles under a RAM budget.

#include "NativeTestSupport.h"

#include "archmage/native/Recovery.h"
#include "archmage/tiles/ResourceCoordinator.h"
#include "archmage/tiles/TileSpool.h"

#include <gtest/gtest.h>

#include <chrono>
#include <thread>

namespace archmage::native {
namespace {

using commands::CommandEnvelope;
using std::chrono::milliseconds;
using testing::ScratchDirectory;

RecoverySession::Options manualOptions() {
    RecoverySession::Options options;
    options.runThread = false;
    options.previewInterval = milliseconds(0);
    return options;
}

std::unique_ptr<RecoverySession> startSession(const std::filesystem::path &root, RecoverySession::Options options) {
    auto session = RecoverySession::start(root, options);
    EXPECT_TRUE(session) << base::format(session.error());
    return session ? std::move(*session) : nullptr;
}

base::LayerId layerId(const std::string &text) { return *base::LayerId::parse(text); }

TEST(RecoveryRoot, PrefersXdgStateHomeOnALocalFilesystem) {
    auto root = locateRecoveryRoot();
    ASSERT_TRUE(root) << base::format(root.error());
    EXPECT_TRUE(root->filesystem.local);
    EXPECT_EQ(root->path.filename(), "archmage");
    EXPECT_TRUE(std::filesystem::is_directory(root->path));
}

// Spec §10.4 cadence: a commit is journaled within 2.5 s (plan S1-5.2), and its status moves from
// in_memory to journaled.
TEST(RecoveryJournal, ACommitIsJournaledWithinTwoAndAHalfSeconds) {
    ScratchDirectory root(testing::homeRoot());
    testing::SampleDocument doc = testing::buildSampleDocument(400, 300);
    auto session = startSession(root.path(), RecoverySession::Options{});
    ASSERT_TRUE(session);
    auto journal = session->track(*doc.executor);
    ASSERT_TRUE(journal) << base::format(journal.error());
    ASSERT_TRUE((*journal)->flush());
    EXPECT_EQ((*journal)->currentDurability(), Durability::Journaled);

    const auto committed = doc.executor->execute(
        CommandEnvelope::make("archmage.layer.rename", {{"name", "Journaled"}}, {doc.ink}));
    ASSERT_TRUE(committed);
    const auto started = std::chrono::steady_clock::now();
    EXPECT_EQ((*journal)->currentDurability(), Durability::InMemory);
    while ((*journal)->currentDurability() != Durability::Journaled &&
           std::chrono::steady_clock::now() - started < std::chrono::seconds(5)) {
        std::this_thread::sleep_for(milliseconds(20));
    }
    const auto elapsed = std::chrono::steady_clock::now() - started;
    EXPECT_EQ((*journal)->currentDurability(), Durability::Journaled);
    EXPECT_LT(elapsed, milliseconds(2500));
    RecordProperty("journaled_after_ms",
                   static_cast<int>(std::chrono::duration_cast<milliseconds>(elapsed).count()));
    EXPECT_EQ((*journal)->journaledRevision(), committed->revisionId);
    ASSERT_TRUE(session->untrack(*journal, true));
}

TEST(RecoveryJournal, SavedRevisionsReportSavedAndLaterEditsDoNot) {
    ScratchDirectory root(testing::tmpRoot());
    ScratchDirectory files(testing::tmpRoot());
    testing::SampleDocument doc = testing::buildSampleDocument(200, 200);
    auto session = startSession(root.path(), manualOptions());
    auto journal = session->track(*doc.executor);
    ASSERT_TRUE(journal);
    const auto revision = doc.executor->current();
    auto saved = testing::save(revision, files / "doc.archmage", identityForSave(*revision->state, std::nullopt));
    ASSERT_TRUE(saved);
    ASSERT_TRUE((*journal)->markSaved(*revision, files / "doc.archmage"));
    EXPECT_EQ((*journal)->currentDurability(), Durability::Saved);
    ASSERT_TRUE(doc.executor->execute(CommandEnvelope::make("archmage.layer.rename", {{"name", "After"}}, {doc.ink})));
    EXPECT_EQ((*journal)->currentDurability(), Durability::InMemory);
    ASSERT_TRUE((*journal)->flush());
    EXPECT_EQ((*journal)->currentDurability(), Durability::Journaled);
    EXPECT_EQ((*journal)->durability(revision->id), Durability::Saved);
    ASSERT_TRUE(session->untrack(*journal, true));
}

TEST(RecoveryJournal, WritesOnlyTheTilesAnEditChanged) {
    ScratchDirectory root(testing::tmpRoot());
    testing::SampleDocument doc = testing::buildSampleDocument(1024, 1024);
    auto session = startSession(root.path(), manualOptions());
    auto journal = session->track(*doc.executor);
    ASSERT_TRUE(journal);
    ASSERT_TRUE((*journal)->flush());
    const auto first = (*journal)->stats();
    EXPECT_GT(first.tilesWritten, 60u);
    brush::BrushSettings dot;
    dot.size = 10.0;
    ASSERT_TRUE(doc.executor->execute(testing::stroke(doc.glaze, dot, testing::line({600, 600}, {610, 604}, 4))));
    ASSERT_TRUE((*journal)->flush());
    const auto second = (*journal)->stats();
    EXPECT_LE(second.tilesWritten - first.tilesWritten, 4u) << "a small stroke touches at most four tiles";
    ASSERT_TRUE(doc.executor->undo());
    ASSERT_TRUE((*journal)->flush());
    ASSERT_TRUE((*journal)->compact());
    EXPECT_EQ((*journal)->currentDurability(), Durability::Journaled);
    ASSERT_TRUE(session->untrack(*journal, true));
}

// Spec §10.4: discover by document identity, not file names; recover as a new unsaved document; never
// touch the original.
TEST(RecoveryDiscovery, FindsAnAbandonedJournalByIdentityAfterRenames) {
    ScratchDirectory root(testing::homeRoot());
    ScratchDirectory files(testing::homeRoot());
    testing::SampleDocument doc = testing::buildSampleDocument(300, 240);
    const auto original = files / "work.archmage";
    ASSERT_TRUE(testing::save(doc.executor->current(), original,
                              identityForSave(*doc.executor->current()->state, std::nullopt)));
    const std::string originalHash = testing::fileHash(original);

    auto opened = readDocument(original);
    ASSERT_TRUE(opened);
    commands::MutationExecutor executor(*opened->state, "Open");
    std::filesystem::path journalPath;
    std::string journaledRevision;
    {
        auto session = startSession(root.path(), manualOptions());
        auto journal = session->track(executor, original);
        ASSERT_TRUE(journal);
        ASSERT_TRUE(executor.execute(CommandEnvelope::make("archmage.layer.rename", {{"name", "Unsaved edit"}},
                                                           {doc.glaze})));
        brush::BrushSettings brush;
        brush.color = {1.0f, 0.0f, 0.0f};
        ASSERT_TRUE(executor.execute(testing::stroke(doc.ink, brush, testing::line({10, 200}, {290, 20}, 40))));
        ASSERT_TRUE((*journal)->flush());
        journalPath = (*journal)->path();
        journaledRevision = executor.current()->id.toString();
        // The process "crashes": the session ends without untracking, as after a kill.
    }
    ASSERT_TRUE(std::filesystem::exists(journalPath));
    // Neither the document nor the journal keeps its name.
    std::filesystem::rename(original, files / "renamed.archmage");
    std::filesystem::rename(journalPath, journalPath.parent_path() / "anything.bin");
    for (const char *suffix : {"-wal", "-shm"}) {
        if (std::filesystem::exists(journalPath.string() + suffix)) {
            std::filesystem::rename(journalPath.string() + suffix,
                                    (journalPath.parent_path() / "anything.bin").string() + suffix);
        }
    }

    auto candidates = listRecoveryCandidates(root.path());
    ASSERT_TRUE(candidates) << base::format(candidates.error());
    ASSERT_EQ(candidates->size(), 1u);
    const RecoveryCandidate &candidate = candidates->front();
    EXPECT_FALSE(candidate.problem);
    EXPECT_EQ(candidate.documentId, doc.executor->current()->state->id);
    EXPECT_EQ(readFileInfo(files / "renamed.archmage")->documentId, candidate.documentId)
        << "the moved original is matched by identity";
    EXPECT_EQ(candidate.revisionId, journaledRevision);
    EXPECT_EQ(candidate.sourcePath, original);
    EXPECT_EQ(candidate.title, doc.executor->current()->state->title);
    EXPECT_FALSE(candidate.upToDate);
    EXPECT_FALSE(candidate.journaledAt.empty());
    ASSERT_TRUE(candidate.thumbnail);
    EXPECT_LE(candidate.thumbnail->width, 256);

    auto recovered = recoverDocument(candidate);
    ASSERT_TRUE(recovered) << base::format(recovered.error());
    ASSERT_TRUE(recovered->state);
    const document::DocumentState &state = *recovered->state;
    const document::DocumentState &expected = *executor.current()->state;
    EXPECT_EQ(state.findLayer(layerId(doc.glaze))->name, "Unsaved edit");
    for (std::size_t i = 0; i < expected.layers.size(); ++i) {
        EXPECT_TRUE(testing::sameTiles(state.layers[i]->content->tiles, expected.layers[i]->content->tiles));
    }
    EXPECT_FALSE(state.preserved) << "journal bookkeeping is not document content";
    EXPECT_EQ(testing::composite(state), testing::composite(expected));
    EXPECT_EQ(testing::fileHash(files / "renamed.archmage"), originalHash) << "the original is untouched";

    ASSERT_TRUE(discardRecovery(candidate));
    auto after = listRecoveryCandidates(root.path());
    ASSERT_TRUE(after);
    EXPECT_TRUE(after->empty());
    EXPECT_TRUE(std::filesystem::is_empty(root / "sessions"));
}

TEST(RecoveryDiscovery, RunningSessionsAreNotOffered) {
    ScratchDirectory root(testing::tmpRoot());
    testing::SampleDocument doc = testing::buildSampleDocument(128, 128);
    auto session = startSession(root.path(), manualOptions());
    auto journal = session->track(*doc.executor);
    ASSERT_TRUE(journal);
    ASSERT_TRUE((*journal)->flush());
    auto candidates = listRecoveryCandidates(root.path());
    ASSERT_TRUE(candidates);
    EXPECT_TRUE(candidates->empty()) << "the session's lock is held";
    ASSERT_TRUE(session->untrack(*journal, false));
    session.reset();
    candidates = listRecoveryCandidates(root.path());
    ASSERT_TRUE(candidates);
    EXPECT_EQ(candidates->size(), 1u) << "untracked without discard: kept for recovery";
}

TEST(RecoveryDiscovery, ASavedDocumentIsFlaggedUpToDate) {
    ScratchDirectory root(testing::tmpRoot());
    ScratchDirectory files(testing::tmpRoot());
    testing::SampleDocument doc = testing::buildSampleDocument(128, 128);
    {
        auto session = startSession(root.path(), manualOptions());
        auto journal = session->track(*doc.executor);
        ASSERT_TRUE(journal);
        ASSERT_TRUE((*journal)->flush());
        const auto revision = doc.executor->current();
        ASSERT_TRUE(testing::save(revision, files / "doc.archmage", identityForSave(*revision->state, std::nullopt)));
        ASSERT_TRUE((*journal)->markSaved(*revision, files / "doc.archmage"));
    }
    auto candidates = listRecoveryCandidates(root.path());
    ASSERT_TRUE(candidates);
    ASSERT_EQ(candidates->size(), 1u);
    EXPECT_TRUE(candidates->front().upToDate);
    EXPECT_EQ(candidates->front().sourcePath, files / "doc.archmage");
}

TEST(RecoverySession, ACleanEndLeavesNothingBehind) {
    ScratchDirectory root(testing::tmpRoot());
    testing::SampleDocument doc = testing::buildSampleDocument(128, 128);
    std::filesystem::path directory;
    {
        auto session = startSession(root.path(), manualOptions());
        directory = session->directory();
        auto journal = session->track(*doc.executor);
        ASSERT_TRUE(journal);
        ASSERT_TRUE((*journal)->flush());
        ASSERT_TRUE(session->untrack(*journal, true));
    }
    EXPECT_FALSE(std::filesystem::exists(directory));
}

// Plan S1-5.4: journaled tiles leave RAM under a 512 MiB budget and come back intact.
class FillTiles final : public commands::CommandDefinition {
public:
    [[nodiscard]] std::string_view id() const override { return "test.fillTiles"; }
    [[nodiscard]] base::Result<commands::CommandEffect> apply(const commands::CommandEnvelope &e,
                                                              const document::DocumentState &state) const override {
        commands::CommandEffect effect{state, "Fill Tiles", {}};
        auto layer = std::make_shared<document::Layer>(*state.layers[0]);
        auto content = std::make_shared<document::RasterContent>(*layer->content);
        const auto first = e.parameters["first"].get<std::int64_t>();
        const auto count = e.parameters["count"].get<std::int64_t>();
        const auto columns = e.parameters["columns"].get<std::int64_t>();
        for (std::int64_t i = first; i < first + count; ++i) {
            const tiles::TileCoord coord{i % columns, (i / columns) * 2}; // every other tile row: sparse
            content->tiles.set(coord, patternTile(coord));
        }
        layer->content = std::move(content);
        effect.state.layers[0] = std::move(layer);
        return effect;
    }

    static std::shared_ptr<const tiles::Tile> patternTile(tiles::TileCoord coord) {
        tiles::TileBuffer buffer(tiles::SampleType::UInt8, 4);
        for (std::size_t p = 0; p < 4; ++p) {
            auto samples = buffer.planeAs<std::uint8_t>(p);
            for (std::size_t i = 0; i < samples.size(); ++i) {
                samples[i] = expected(coord, p, i);
            }
        }
        return std::move(buffer).seal();
    }

    static std::uint8_t expected(tiles::TileCoord coord, std::size_t plane, std::size_t index) {
        return static_cast<std::uint8_t>(coord.tx * 7 + coord.ty * 13 + static_cast<std::int64_t>(plane * 31 + index));
    }
};

TEST(RecoverySpool, A20000PixelDocumentStaysWithinA512MiBBudget) {
    ScratchDirectory root(testing::tmpRoot());
    auto &coordinator = tiles::ResourceCoordinator::global();
    constexpr std::uint64_t kBudget = 512ull << 20;
    coordinator.setLimit(tiles::Budget::Ram, kBudget);

    commands::CommandRegistry registry;
    commands::registerLayerCommands(registry);
    commands::registerRasterCommands(registry);
    registry.add(std::make_unique<FillTiles>());
    auto created = commands::createDocument(CommandEnvelope::make(
        "archmage.document.create", {{"width", 20000}, {"height", 20000}, {"background", "transparent"}}));
    ASSERT_TRUE(created);
    commands::MutationExecutor executor(created->state, "New Document", commands::MutationExecutor::Options{},
                                        registry);
    RecoverySession::Options options = manualOptions();
    options.previewMaxSize = 0; // a thumbnail would read every tile back
    auto session = startSession(root.path(), options);
    auto journal = session->track(executor);
    ASSERT_TRUE(journal);

    // 12,246 dense tiles in every other tile row (765 MiB of samples, half of the 157 × 157 tile grid of a
    // 20,000² canvas), 32 MiB per commit.
    constexpr std::int64_t kColumns = 157;
    constexpr std::int64_t kBatch = 512;
    constexpr std::int64_t kTiles = kColumns * 78;
    for (std::int64_t first = 0; first < kTiles; first += kBatch) {
        const std::int64_t count = std::min(kBatch, kTiles - first);
        ASSERT_TRUE(executor.execute(CommandEnvelope::make("test.fillTiles",
                                                           {{"first", first}, {"count", count}, {"columns", kColumns}})));
        ASSERT_TRUE((*journal)->flush()) << base::format((*journal)->lastError().error());
    }
    const auto spooled = tiles::TileSpool::global().stats();
    EXPECT_GT(spooled.evictions, 0u);
    EXPECT_LT(spooled.resident, static_cast<std::uint64_t>(kTiles));

    // Every tile reads back with its samples.
    const tiles::TileMap &map = executor.current()->state->layers[0]->content->tiles;
    EXPECT_EQ(map.tileCount(), static_cast<std::size_t>(kTiles));
    std::size_t checked = 0;
    map.forEach([&](tiles::TileCoord coord, const std::shared_ptr<const tiles::Tile> &tile) {
        auto pin = tile->pin();
        ASSERT_TRUE(pin) << base::format(pin.error());
        for (std::size_t p = 0; p < 4; ++p) {
            const auto samples = tile->plane(p);
            for (const std::size_t i : {std::size_t{0}, std::size_t{4097}, tiles::kTilePixels - 1}) {
                ASSERT_EQ(static_cast<std::uint8_t>(samples[i]), FillTiles::expected(coord, p, i));
            }
        }
        ++checked;
    });
    EXPECT_EQ(checked, static_cast<std::size_t>(kTiles));
    const auto after = tiles::TileSpool::global().stats();
    EXPECT_GT(after.reloads, 0u);
    const std::uint64_t peak = coordinator.peak(tiles::Budget::Ram);
    RecordProperty("peak_ram_mib", static_cast<int>(peak >> 20));
    RecordProperty("evictions", static_cast<int>(after.evictions));
    RecordProperty("reloads", static_cast<int>(after.reloads));
    EXPECT_LE(peak, kBudget + coordinator.reservedPool()) << "peak " << (peak >> 20) << " MiB";
    ASSERT_TRUE(session->untrack(*journal, true));
}

} // namespace
} // namespace archmage::native
