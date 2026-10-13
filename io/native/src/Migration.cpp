#include "archmage/native/Migration.h"

#include "archmage/native/Format.h"

#include <string>

namespace archmage::native {

const std::vector<MigrationStep> &migrationSteps() {
    static const std::vector<MigrationStep> kSteps{
        // {0, 1, "describe the change", &migrate_0_1_to_0_2},
    };
    return kSteps;
}

base::Status migrate(Database &db, int major, int fromMinor) {
    if (major != kSchemaMajor) {
        return base::fail(base::ErrorCategory::UnsupportedInput, "schema.no_migration",
                          "This document's file format cannot be upgraded by this version",
                          std::to_string(major) + "." + std::to_string(fromMinor));
    }
    for (int minor = fromMinor; minor < kSchemaMinor; ++minor) {
        const MigrationStep *step = nullptr;
        for (const MigrationStep &candidate : migrationSteps()) {
            if (candidate.major == major && candidate.fromMinor == minor) {
                step = &candidate;
            }
        }
        if (step == nullptr) {
            return base::fail(base::ErrorCategory::Internal, "schema.migration_missing",
                              "This document's file format cannot be upgraded by this version",
                              "no step from " + std::to_string(major) + "." + std::to_string(minor));
        }
        if (auto status = step->apply(db); !status) {
            return status;
        }
    }
    return db.exec("UPDATE manifest SET value = '" + std::to_string(kSchemaMinor) + "' WHERE key = 'schema_minor'");
}

} // namespace archmage::native
