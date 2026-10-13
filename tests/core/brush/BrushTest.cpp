// S1-3.1 and S1-3.2: stroke resampling and round dabs.

#include "archmage/brush/Stroke.h"

#include <gtest/gtest.h>

#include <random>

namespace archmage::brush {
namespace {

std::vector<StrokeSample> wobblyStroke() {
    std::vector<StrokeSample> samples;
    std::mt19937 rng{7};
    std::uniform_real_distribution<double> jitter{-0.7, 0.7};
    for (int i = 0; i < 300; ++i) {
        samples.push_back({{10.0 + i * 1.7 + jitter(rng), 40.0 + 25.0 * std::sin(i * 0.05) + jitter(rng)},
                           static_cast<float>(0.2 + 0.8 * (i % 50) / 49.0),
                           i * 4'166'667LL});
    }
    return samples;
}

std::vector<Dab> resample(const BrushSettings &settings, const std::vector<StrokeSample> &samples,
                          std::vector<std::size_t> batchSizes) {
    StrokeResampler resampler(settings);
    std::vector<Dab> dabs;
    std::size_t i = 0;
    std::size_t b = 0;
    while (i < samples.size()) {
        const std::size_t n = std::min(batchSizes[b++ % batchSizes.size()], samples.size() - i);
        for (std::size_t k = 0; k < n; ++k) {
            resampler.add(samples[i + k], dabs);
        }
        i += n;
    }
    return dabs;
}

TEST(Resampler, DabPositionsDoNotDependOnBatching) {
    BrushSettings settings;
    settings.size = 12.0;
    settings.spacing = 0.15;
    const auto samples = wobblyStroke();
    const auto one = resample(settings, samples, {1});
    const auto batched = resample(settings, samples, {7, 1, 30, 2});
    const auto whole = resample(settings, samples, {samples.size()});
    ASSERT_GT(one.size(), 100u);
    ASSERT_EQ(one.size(), batched.size());
    ASSERT_EQ(one.size(), whole.size());
    for (std::size_t i = 0; i < one.size(); ++i) {
        ASSERT_EQ(one[i].center, batched[i].center) << i;
        ASSERT_EQ(one[i].diameter, whole[i].diameter) << i;
    }
}

TEST(Resampler, SpacingFollowsTheDiameter) {
    BrushSettings settings;
    settings.size = 10.0;
    settings.spacing = 0.1; // one dab per pixel at full pressure
    StrokeResampler resampler(settings);
    std::vector<Dab> dabs;
    resampler.add({{0.0, 0.0}, 1.0f}, dabs);
    resampler.add({{100.0, 0.0}, 1.0f}, dabs);
    ASSERT_EQ(dabs.size(), 101u);
    EXPECT_EQ(dabs.front().center, (base::DPoint{0.0, 0.0}));
    EXPECT_NEAR(dabs.back().center.x, 100.0, 1e-9);
    settings.pressureSize = false;
    StrokeResampler fixed(settings);
    std::vector<Dab> mouse;
    fixed.add({{0.0, 0.0}, 0.1f}, mouse);
    EXPECT_EQ(mouse.front().diameter, 10.0) << "a mouse (no pressure control) uses the fixed diameter";
}

TEST(Resampler, ZeroPressureStillAdvances) {
    BrushSettings settings;
    StrokeResampler resampler(settings);
    std::vector<Dab> dabs;
    resampler.add({{0.0, 0.0}, 0.0f}, dabs);
    resampler.add({{10.0, 0.0}, 0.0f}, dabs);
    EXPECT_EQ(dabs.size(), 21u) << "0.5 px minimum step";
    EXPECT_EQ(dabCoverage(dabs[3], 0.8, dabs[3].center), 0.0f);
}

TEST(Dab, CoverageBoundsAndSymmetry) {
    const Dab dab{{50.5, 50.5}, 20.0};
    EXPECT_EQ(dabBounds(dab), (base::IRect{39, 39, 62, 62})); // reach 11 around 50.5: [39.5, 61.5)
    for (const double hardness : {0.0, 0.5, 1.0}) {
        EXPECT_EQ(dabCoverage(dab, hardness, {50.5 + 10.6, 50.5}), 0.0f) << "beyond radius + 0.5";
        for (int d = 0; d <= 10; ++d) {
            const float right = dabCoverage(dab, hardness, {50.5 + d, 50.5});
            EXPECT_EQ(right, dabCoverage(dab, hardness, {50.5 - d, 50.5}));
            EXPECT_EQ(right, dabCoverage(dab, hardness, {50.5, 50.5 + d}));
            EXPECT_EQ(right, dabCoverage(dab, hardness, {50.5, 50.5 - d}));
        }
    }
}

TEST(Dab, HardnessExtremes) {
    const Dab dab{{0.0, 0.0}, 20.0};
    EXPECT_EQ(dabCoverage(dab, 1.0, {9.0, 0.0}), 1.0f) << "a hard dab is solid up to its antialiased edge";
    EXPECT_EQ(dabCoverage(dab, 1.0, {10.0, 0.0}), 0.5f);
    EXPECT_EQ(dabCoverage(dab, 0.0, {0.0, 0.0}), 1.0f);
    EXPECT_LT(dabCoverage(dab, 0.0, {5.0, 0.0}), 1.0f);
    EXPECT_GT(dabCoverage(dab, 0.0, {5.0, 0.0}), 0.0f);
    EXPECT_NEAR(dabCoverage(Dab{{0.0, 0.0}, 0.5}, 1.0, {0.0, 0.0}), 0.25f, 1e-6)
        << "sub-pixel dabs scale by area";
}

TEST(Dab, CoverageIsMonotonicInDiameter) {
    for (const double hardness : {0.0, 0.3, 0.8, 1.0}) {
        for (double distance = 0.0; distance < 30.0; distance += 0.37) {
            float previous = 0.0f;
            for (double diameter = 0.25; diameter < 70.0; diameter += 0.5) {
                const float c = dabCoverage(Dab{{0.0, 0.0}, diameter}, hardness, {distance, 0.0});
                ASSERT_GE(c, previous)
                    << "hardness " << hardness << " distance " << distance << " d " << diameter;
                previous = c;
            }
        }
    }
}

TEST(StrokeMask, KeepsTheMaximumCoverage) {
    StrokeMask mask;
    tiles::DirtyRegion dirty;
    mask.addDab({{64.0, 64.0}, 30.0}, 0.0, dirty);
    const float center = mask.tile({0, 0})->sample(0, 64, 64);
    mask.addDab({{64.0, 64.0}, 30.0}, 0.0, dirty);
    EXPECT_EQ(mask.tile({0, 0})->sample(0, 64, 64), center) << "overlapping dabs do not build up";
    mask.addDab({{127.0, 127.0}, 6.0}, 1.0, dirty);
    EXPECT_EQ(dirty.tileCount(), 4u) << "a dab on a tile corner touches four tiles";
    EXPECT_EQ(mask.tileCount(), 4u);
}

TEST(Settings, JsonRoundTripAndValidation) {
    BrushSettings s;
    s.color = {0.25f, 0.5f, 1.0f};
    s.size = 7.5;
    s.mode = PaintMode::Erase;
    const auto back = BrushSettings::fromJson(s.toJson());
    ASSERT_TRUE(back);
    EXPECT_EQ(back->toJson(), s.toJson());
    EXPECT_FALSE(BrushSettings::fromJson({{"size", 0.1}}));
    EXPECT_FALSE(BrushSettings::fromJson({{"opacity", 2}}));
    EXPECT_FALSE(BrushSettings::fromJson({{"mode", "smudge"}}));
    EXPECT_FALSE(samplesFromJson(nlohmann::json::array()));
    EXPECT_FALSE(samplesFromJson({{1, 2, 1.5}}));
    EXPECT_TRUE(samplesFromJson({{1, 2, 0.5}, {3.5, 4, 1, 99}}));
}

} // namespace
} // namespace archmage::brush
