// Schema migrations (D-13, spec §10.5, plan S1-4.7): an ordered registry of steps, each moving a file of
// one schema (major, minor) to the next minor. Reading an older file copies it into memory and migrates
// the copy, so opening never changes the user's file; the next save writes the current schema.
//
// Every minor version this code has ever written keeps a fixture in tests/io/native/fixtures/, and a test
// opens each one (NativeMigrationTest); adding a minor without its fixture and its step fails that test.
//
// Thread safety: the registry is immutable; migrate() works on the caller's database.
#pragma once

#include "archmage/base/Error.h"
#include "archmage/native/Sqlite.h"

#include <string_view>
#include <vector>

namespace archmage::native {

struct MigrationStep {
    int major = 0;
    int fromMinor = 0; // migrates (major, fromMinor) to (major, fromMinor + 1)
    std::string_view description;
    base::Status (*apply)(Database &db) = nullptr;
};

/// Steps in order. Empty while schema 0.1 is the only version written.
[[nodiscard]] const std::vector<MigrationStep> &migrationSteps();

/// Applies the steps from (major, fromMinor) up to the current schema, then updates the manifest's
/// schema_minor. A gap in the registry is an Internal error.
[[nodiscard]] base::Status migrate(Database &db, int major, int fromMinor);

} // namespace archmage::native
