// S1-2.2 and S1-2.3: sample types, tiles, sparse copy-on-write tile maps and the resource coordinator.

#include "archmage/tiles/ResourceCoordinator.h"
#include "archmage/tiles/TileMap.h"

#include <gtest/gtest.h>

#include <bit>
#include <cmath>
#include <cstring>
#include <limits>

namespace archmage::tiles {
namespace {

using base::IPoint;
using base::IRect;

std::shared_ptr<const Tile> solidTile(float r, float g, float b, float a,
                                      SampleType type = SampleType::UInt8) {
    TileBuffer buffer(type, 4);
    buffer.fill(0, r);
    buffer.fill(1, g);
    buffer.fill(2, b);
    buffer.fill(3, a);
    return std::move(buffer).seal();
}

std::shared_ptr<const Tile> gradientTile(SampleType type = SampleType::UInt8) {
    TileBuffer buffer(type, 4);
    for (int y = 0; y < kTileSize; ++y) {
        for (int x = 0; x < kTileSize; ++x) {
            buffer.setSample(0, x, y, static_cast<float>(x) / 127.0f);
            buffer.setSample(3, x, y, 1.0f);
        }
    }
    return std::move(buffer).seal();
}

TEST(SampleType, Uint8StoreRoundsClampsAndSanitizes) {
    std::byte b{};
    storeSample(SampleType::UInt8, 0.5f, &b);
    EXPECT_EQ(static_cast<int>(b), 128);
    storeSample(SampleType::UInt8, -3.0f, &b);
    EXPECT_EQ(static_cast<int>(b), 0);
    storeSample(SampleType::UInt8, 7.0f, &b);
    EXPECT_EQ(static_cast<int>(b), 255);
    storeSample(SampleType::UInt8, std::numeric_limits<float>::quiet_NaN(), &b);
    EXPECT_EQ(static_cast<int>(b), 0);
    for (int v = 0; v < 256; ++v) { // every uint8 value round trips exactly
        const std::byte in{static_cast<std::uint8_t>(v)};
        std::byte out{};
        storeSample(SampleType::UInt8, loadSample(SampleType::UInt8, &in), &out);
        ASSERT_EQ(out, in);
    }
}

TEST(SampleType, Float32KeepsOutOfRangeValues) {
    std::byte bytes[4];
    storeSample(SampleType::Float32, -0.25f, bytes);
    EXPECT_EQ(loadSample(SampleType::Float32, bytes), -0.25f);
    storeSample(SampleType::Float32, 4.5f, bytes);
    EXPECT_EQ(loadSample(SampleType::Float32, bytes), 4.5f);
}

TEST(SampleType, HalfFloatConversions) {
    EXPECT_EQ(floatToHalf(1.0f), 0x3c00);
    EXPECT_EQ(floatToHalf(-2.0f), 0xc000);
    EXPECT_EQ(floatToHalf(65504.0f), 0x7bff);
    EXPECT_EQ(floatToHalf(1.0e6f), 0x7c00);
    EXPECT_EQ(floatToHalf(std::ldexp(1.0f, -24)), 0x0001); // smallest subnormal
    EXPECT_EQ(halfToFloat(0x0001), std::ldexp(1.0f, -24));
    EXPECT_EQ(halfToFloat(0x3555), 0.333251953125f);
    EXPECT_TRUE(std::isnan(halfToFloat(floatToHalf(std::numeric_limits<float>::quiet_NaN()))));
    EXPECT_TRUE(std::isinf(halfToFloat(0x7c00)));
    for (std::uint32_t h = 0; h < 0x7c00; ++h) { // every finite positive half round trips
        ASSERT_EQ(floatToHalf(halfToFloat(static_cast<std::uint16_t>(h))), h) << h;
    }
}

TEST(Tile, UniformTilesAllocateNoPixels) {
    const std::uint64_t before = ResourceCoordinator::global().used(Budget::Ram);
    const auto zero = Tile::makeZero(SampleType::Float32, 4);
    EXPECT_TRUE(zero->isUniform());
    EXPECT_EQ(zero->denseBytes(), 0u);
    const auto solid = solidTile(1, 0, 0, 1);
    EXPECT_TRUE(solid->isUniform()) << "a uniformly filled buffer seals into a uniform tile";
    EXPECT_EQ(solid->sample(0, 77, 3), 1.0f);
    EXPECT_EQ(ResourceCoordinator::global().used(Budget::Ram), before);
}

TEST(Tile, DenseTilesChargeAndReleaseRam) {
    const std::uint64_t before = ResourceCoordinator::global().used(Budget::Ram);
    {
        const auto tile = gradientTile(SampleType::Float32);
        EXPECT_FALSE(tile->isUniform());
        EXPECT_EQ(tile->denseBytes(), ChannelLayout::rgba(SampleType::Float32).tileBytes());
        EXPECT_EQ(tile->denseBytes(), 256u * 1024u); // spec §7.1: four float32 planes of 128 × 128
        EXPECT_EQ(ResourceCoordinator::global().used(Budget::Ram), before + tile->denseBytes());
        EXPECT_NEAR(tile->sample(0, 127, 5), 1.0f, 1e-6);
    }
    EXPECT_EQ(ResourceCoordinator::global().used(Budget::Ram), before);
}

TEST(Tile, VersionsAreUniqueAndBuffersCopy) {
    const auto a = gradientTile();
    TileBuffer copy(*a);
    copy.setSample(1, 3, 4, 1.0f);
    const auto b = std::move(copy).seal();
    EXPECT_NE(a->version(), b->version());
    EXPECT_EQ(a->sample(1, 3, 4), 0.0f) << "the source tile is immutable";
    EXPECT_EQ(b->sample(1, 3, 4), 1.0f);
    EXPECT_FALSE(a->sameContent(*b));
    TileBuffer same(*a);
    EXPECT_TRUE(std::move(same).seal()->sameContent(*a));
}

TEST(TileCoords, FloorDivisionForNegativePixels) {
    EXPECT_EQ(tileContaining(IPoint{0, 127}), (TileCoord{0, 0}));
    EXPECT_EQ(tileContaining(IPoint{128, -1}), (TileCoord{1, -1}));
    EXPECT_EQ(tileContaining(IPoint{-128, -129}), (TileCoord{-1, -2}));
    EXPECT_EQ(tilePixelRect(TileCoord{-1, 2}), (IRect{-128, 256, 0, 384}));
    EXPECT_EQ(tileRangeCovering(IRect{-37, 59, 963, 1059}), (IRect{-1, 0, 8, 9}));
    EXPECT_TRUE(tileRangeCovering(IRect{}).empty());
}

TEST(TileMap, HugeEmptyMapAllocatesNothing) {
    const TileMap map(ChannelLayout::rgba(SampleType::Float32));
    const IRect extent{0, 0, 300000, 300000};
    EXPECT_EQ(map.tileCount(), 0u);
    EXPECT_EQ(map.denseBytes(), 0u);
    EXPECT_TRUE(map.pixelBounds().empty());
    const auto tile = map.at(tileContaining(IPoint{299999, 299999}));
    EXPECT_TRUE(tile->isUniform());
    EXPECT_EQ(tile->sample(3, 0, 0), 0.0f);
    EXPECT_EQ(denseBytes(extent, map.layout()), std::uint64_t{300000} * 300000 * 16);
}

TEST(TileMap, AllocationSizeOverflowIsRejected) {
    const IRect absurd{0, 0, std::numeric_limits<std::int64_t>::max() / 8, 8};
    EXPECT_FALSE(denseBytes(absurd, ChannelLayout::rgba(SampleType::Float32)));
}

TEST(TileMap, SetFindEraseAndDefaultCompaction) {
    TileMap map(ChannelLayout::rgba(SampleType::UInt8));
    const auto red = solidTile(1, 0, 0, 1);
    map.set({-3, 7}, red);
    EXPECT_EQ(map.tileCount(), 1u);
    EXPECT_EQ(map.find({-3, 7}), red);
    EXPECT_EQ(map.find({-3, 8}), nullptr);
    map.set({-3, 7}, Tile::makeZero(SampleType::UInt8, 4)); // default content erases
    EXPECT_EQ(map.tileCount(), 0u);
    map.set({100, 100}, red);
    map.set({101, 100}, red);
    EXPECT_EQ(map.pixelBounds(), (IRect{12800, 12800, 13056, 12928}));
    map.erase({100, 100});
    map.erase({100, 100});
    EXPECT_EQ(map.tileCount(), 1u);
}

TEST(TileMap, CopiesShareStorageUntilModified) {
    TileMap source(ChannelLayout::rgba(SampleType::UInt8));
    for (std::int64_t i = 0; i < 40; ++i) {
        source.set({i, i / 3}, gradientTile());
    }
    TileMap copy = source;
    EXPECT_TRUE(copy.sharesStorageWith(source));
    EXPECT_EQ(copy.generation(), source.generation()) << "a copy has the same content";
    const auto before = source.find({5, 1});
    copy.set({5, 1}, solidTile(0, 0, 1, 1));
    copy.set({1000, -1000}, solidTile(0, 1, 0, 1));
    EXPECT_FALSE(copy.sharesStorageWith(source));
    EXPECT_NE(copy.generation(), source.generation());
    EXPECT_EQ(source.find({5, 1}), before) << "modifying a copy leaves the source unchanged";
    EXPECT_EQ(source.find({1000, -1000}), nullptr);
    EXPECT_EQ(source.tileCount(), 40u);
    EXPECT_EQ(copy.tileCount(), 41u);
    EXPECT_EQ(copy.find({6, 2}), source.find({6, 2})) << "untouched tiles stay shared";
    EXPECT_EQ(TileMap::changedTiles(source, copy), (std::vector<TileCoord>{{1000, -1000}, {5, 1}}));
    EXPECT_TRUE(TileMap::changedTiles(source, source).empty());
}

TEST(TileMap, ForEachIsDeterministic) {
    TileMap map(ChannelLayout::coverage(SampleType::Float32));
    const std::array<float, 1> half{0.5f};
    const auto tile = Tile::makeUniform(SampleType::Float32, 1, std::as_bytes(std::span(half)));
    for (const TileCoord c : {TileCoord{40, -2}, TileCoord{-17, 3}, TileCoord{0, 0}, TileCoord{17, 0}}) {
        map.set(c, tile);
    }
    std::vector<TileCoord> visited;
    map.forEach([&visited](TileCoord c, const std::shared_ptr<const Tile> &) { visited.push_back(c); });
    EXPECT_EQ(visited, (std::vector<TileCoord>{{40, -2}, {-17, 3}, {0, 0}, {17, 0}}));
}

TEST(DirtyRegion, CoversTilesTouchedByPixels) {
    DirtyRegion region;
    region.addPixels(IRect{120, 120, 130, 130});
    EXPECT_EQ(region.tileCount(), 4u);
    region.addTile({5, 5});
    EXPECT_TRUE(region.containsTile({1, 1}));
    EXPECT_EQ(region.pixelBounds(), (IRect{0, 0, 768, 768}));
}

TEST(ResourceCoordinator, EvictsDisposableBeforeSpoolable) {
    ResourceCoordinator coordinator(
        {.ram = 1000, .vram = 0, .scratch = 0, .history = 0, .reservedPool = 100});
    std::uint64_t disposable = 300;
    std::uint64_t spoolable = 300;
    std::vector<std::string> order;
    coordinator.charge(Budget::Ram, 300); // dirty authoritative data: no evictor, never reclaimed
    coordinator.charge(Budget::Ram, disposable);
    coordinator.charge(Budget::Ram, spoolable);
    coordinator.addEvictor(Budget::Ram, EvictionClass::Spoolable, [&](std::uint64_t want) {
        const std::uint64_t freed = std::min(want, spoolable);
        spoolable -= freed;
        coordinator.release(Budget::Ram, freed);
        order.emplace_back("spoolable");
        return freed;
    });
    coordinator.addEvictor(Budget::Ram, EvictionClass::Disposable, [&](std::uint64_t want) {
        const std::uint64_t freed = std::min(want, disposable);
        disposable -= freed;
        coordinator.release(Budget::Ram, freed);
        order.emplace_back("disposable");
        return freed;
    });

    EXPECT_TRUE(coordinator.makeRoom(Budget::Ram, 100)); // 900 used: fits without eviction
    EXPECT_TRUE(order.empty());
    EXPECT_TRUE(coordinator.makeRoom(Budget::Ram, 300)); // needs 200 back: disposable only
    EXPECT_EQ(order, (std::vector<std::string>{"disposable"}));
    EXPECT_EQ(disposable, 100u);
    EXPECT_EQ(spoolable, 300u);
    EXPECT_TRUE(coordinator.makeRoom(Budget::Ram, 550)); // disposable empties, then spoolable gives the rest
    EXPECT_EQ(disposable, 0u);
    EXPECT_EQ(spoolable, 150u);
    EXPECT_EQ(coordinator.used(Budget::Ram), 450u);
    EXPECT_FALSE(coordinator.makeRoom(Budget::Ram, 900)) << "the 300 dirty bytes are never evicted";
    EXPECT_EQ(coordinator.used(Budget::Ram), 300u);
    EXPECT_TRUE(coordinator.makeRoomFromReserve(Budget::Ram, 790))
        << "the reserved pool serves recovery paths";
    EXPECT_EQ(coordinator.peak(Budget::Ram), 900u);
}

} // namespace
} // namespace archmage::tiles
