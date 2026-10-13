// S1-2.4: the document model, its invariants and structural sharing between snapshots.

#include "archmage/document/Document.h"

#include <gtest/gtest.h>

#include <chrono>

namespace archmage::document {
namespace {

DocumentState newDocument(std::int64_t w = 1000, std::int64_t h = 800) {
    return *makeDocumentState(w, h, tiles::SampleType::UInt8, color::ColorPolicy::defaultPolicy(), "Test");
}

TEST(Document, NewDocumentsAreValidAndEmpty) {
    const DocumentState doc = newDocument();
    EXPECT_FALSE(doc.id.isNull());
    EXPECT_TRUE(doc.layers.empty());
    EXPECT_EQ(doc.canvasRect(), (base::IRect{0, 0, 1000, 800}));
    EXPECT_EQ(doc.capabilities, (std::vector<std::string>{"raster.layers.v1"}));
    EXPECT_EQ(doc.resolution, 72.0);
    EXPECT_TRUE(validate(doc));
}

TEST(Document, CreationValidatesParameters) {
    const auto policy = color::ColorPolicy::defaultPolicy();
    EXPECT_EQ(makeDocumentState(0, 10, tiles::SampleType::UInt8, policy, "").error().code,
              "document.dimensions");
    EXPECT_EQ(makeDocumentState(300001, 10, tiles::SampleType::UInt8, policy, "").error().code,
              "document.dimensions");
    EXPECT_TRUE(makeDocumentState(300000, 300000, tiles::SampleType::Float32, policy, ""));
    const auto half = makeDocumentState(10, 10, tiles::SampleType::Float16, policy, "");
    ASSERT_FALSE(half);
    EXPECT_EQ(half.error().category, base::ErrorCategory::UnsupportedInput);
}

TEST(Document, RasterLayersMatchTheDocumentLayout) {
    DocumentState doc = newDocument();
    doc.sampleType = tiles::SampleType::Float32;
    const auto layer = makeRasterLayer(doc, "Background");
    EXPECT_EQ(layer->name, "Background");
    EXPECT_TRUE(layer->visible);
    EXPECT_FALSE(layer->locked);
    EXPECT_EQ(layer->opacity, 1.0f);
    EXPECT_EQ(layer->content->tiles.layout(), tiles::ChannelLayout::rgba(tiles::SampleType::Float32));
    EXPECT_EQ(layer->content->tiles.tileCount(), 0u);
    doc.layers.push_back(layer);
    EXPECT_TRUE(validate(doc));
    EXPECT_EQ(doc.indexOf(layer->id), 0u);
    EXPECT_EQ(doc.findLayer(layer->id), layer.get());
    EXPECT_EQ(doc.findLayer(base::LayerId::generate()), nullptr);
}

TEST(Document, InvariantViolationsAreReported) {
    DocumentState doc = newDocument();
    const auto layer = makeRasterLayer(doc, "A");
    doc.layers = {layer, layer};
    EXPECT_EQ(validate(doc).error().code, "layer.duplicate_id");

    auto noContent = std::make_shared<Layer>(*layer);
    noContent->content.reset();
    doc.layers = {noContent};
    EXPECT_EQ(validate(doc).error().code, "layer.missing_content");

    auto badOpacity = std::make_shared<Layer>(*layer);
    badOpacity->opacity = 1.5f;
    doc.layers = {badOpacity};
    EXPECT_EQ(validate(doc).error().code, "layer.opacity_range");

    auto wrongLayout = std::make_shared<Layer>(*layer);
    wrongLayout->content = std::make_shared<RasterContent>(RasterContent{
        base::RasterId::generate(), tiles::TileMap(tiles::ChannelLayout::rgba(tiles::SampleType::Float32))});
    doc.layers = {wrongLayout};
    EXPECT_EQ(validate(doc).error().code, "layer.layout_mismatch");
}

TEST(Document, InstancesShareContentAndAreValid) {
    DocumentState doc = newDocument();
    const auto source = makeRasterLayer(doc, "Source");
    auto instance = std::make_shared<Layer>(*source);
    instance->id = base::LayerId::generate();
    instance->offset = {-37, 59};
    doc.layers = {source, instance};
    EXPECT_TRUE(validate(doc));
    EXPECT_EQ(doc.layers[0]->content, doc.layers[1]->content);
}

TEST(Document, SnapshotsShareUnchangedLayers) {
    DocumentState doc = newDocument();
    for (int i = 0; i < 5; ++i) {
        doc.layers.push_back(makeRasterLayer(doc, "Layer " + std::to_string(i)));
    }
    const auto before = std::make_shared<const DocumentState>(doc);
    DocumentState next = *before;
    auto changed = std::make_shared<Layer>(*next.layers[2]);
    changed->opacity = 0.6f;
    next.layers[2] = changed;
    const auto after = std::make_shared<const DocumentState>(std::move(next));
    for (const std::size_t i : {0u, 1u, 3u, 4u}) {
        EXPECT_EQ(before->layers[i], after->layers[i]) << i;
    }
    EXPECT_NE(before->layers[2], after->layers[2]);
    EXPECT_EQ(before->layers[2]->opacity, 1.0f) << "the earlier snapshot is untouched";
    EXPECT_EQ(before->layers[2]->content, after->layers[2]->content) << "pixels are shared, not copied";
}

TEST(Document, TenThousandLayerSnapshotIsFast) {
    DocumentState doc = newDocument(4096, 4096);
    doc.layers.reserve(10000);
    for (int i = 0; i < 10000; ++i) {
        doc.layers.push_back(makeRasterLayer(doc, "L"));
    }
    const auto start = std::chrono::steady_clock::now();
    const auto snapshot = std::make_shared<const DocumentState>(doc);
    DocumentState edited = *snapshot;
    auto layer = std::make_shared<Layer>(*edited.layers[5000]);
    layer->visible = false;
    edited.layers[5000] = layer;
    ASSERT_TRUE(validate(edited));
    const auto elapsed = std::chrono::steady_clock::now() - start;
    EXPECT_LT(std::chrono::duration_cast<std::chrono::milliseconds>(elapsed).count(), 1000);
}

TEST(Document, NamesRoundTrip) {
    EXPECT_EQ(blendModeFromName(blendModeName(BlendMode::Normal)), BlendMode::Normal);
    EXPECT_FALSE(blendModeFromName("multiply")) << "blend modes beyond Normal arrive in M2";
    for (const LengthUnit u : {LengthUnit::Pixel, LengthUnit::Inch, LengthUnit::Millimeter,
                               LengthUnit::Centimeter, LengthUnit::Point, LengthUnit::Pica}) {
        EXPECT_EQ(lengthUnitFromName(lengthUnitName(u)), u);
    }
}

} // namespace
} // namespace archmage::document
