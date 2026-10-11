#include "archmage/document/Version.h"

#include <gtest/gtest.h>

#include <format>
#include <regex>
#include <string>

namespace archmage::document {
namespace {

TEST(Version, MatchesTheCMakeProjectVersion) {
    // A stale generated header would disagree with the version CMake configured.
    EXPECT_EQ(kVersion, ARCHMAGE_EXPECTED_VERSION);
    EXPECT_EQ(kVersion, std::format("{}.{}.{}", kVersionMajor, kVersionMinor, kVersionPatch));
}

TEST(Version, RevisionIsACommitAbbreviationOrUnknown) {
    const std::regex revision{"[0-9a-f]{7,40}(-dirty)?|unknown"};
    EXPECT_TRUE(std::regex_match(std::string{kGitRevision}, revision)) << kGitRevision;
}

TEST(Version, BannerNamesProductVersionAndRevision) {
    EXPECT_EQ(versionBanner(), std::format("ArchMage {} ({})", kVersion, kGitRevision));
}

} // namespace
} // namespace archmage::document
