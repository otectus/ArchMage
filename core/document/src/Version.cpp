#include "archmage/document/Version.h"

#include <format>

namespace archmage::document {

std::string versionBanner() { return std::format("{} {} ({})", kProductName, kVersion, kGitRevision); }

} // namespace archmage::document
