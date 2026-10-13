// D-17: checked arithmetic and half-open integer rectangles.

#include "archmage/base/Geometry.h"
#include "archmage/base/Checked.h"

#include <gtest/gtest.h>

#include <limits>

namespace archmage::base {
namespace {

TEST(Checked, DetectsOverflow) {
    constexpr auto kMax = std::numeric_limits<std::int64_t>::max();
    EXPECT_EQ(checkedAdd<std::int64_t>(2, 3), 5);
    EXPECT_FALSE(checkedAdd<std::int64_t>(kMax, 1));
    EXPECT_FALSE(checkedSub<std::int64_t>(std::numeric_limits<std::int64_t>::min(), 1));
    EXPECT_FALSE(checkedMul<std::int64_t>(kMax / 2 + 1, 2));
    EXPECT_EQ(checkedByteCount(300000, 4), std::size_t{1200000});
    EXPECT_FALSE(checkedByteCount(-1, 4));
}

TEST(Checked, AreaOfAHugeSparseExtentOverflowsInsteadOfWrapping) {
    const IRect normal{0, 0, 300000, 300000};
    EXPECT_EQ(normal.checkedArea(), std::int64_t{90'000'000'000});
    const IRect absurd{std::numeric_limits<std::int64_t>::min() / 2, 0,
                       std::numeric_limits<std::int64_t>::max() / 2,
                       std::numeric_limits<std::int64_t>::max() / 2};
    EXPECT_FALSE(absurd.checkedArea());
}

TEST(IRect, ContainmentAndTranslation) {
    const IRect r{-5, -5, 5, 5};
    EXPECT_TRUE(r.contains(IRect{-5, -5, 0, 0}));
    EXPECT_FALSE(r.contains(IRect{-6, 0, 0, 1}));
    EXPECT_TRUE(r.contains(IRect{}));
    EXPECT_TRUE(r.intersects(IRect{4, 4, 10, 10}));
    EXPECT_FALSE(r.intersects(IRect{5, 5, 10, 10}));
    EXPECT_EQ(r.translated(-37, 59), (IRect{-42, 54, -32, 64}));
}

} // namespace
} // namespace archmage::base
