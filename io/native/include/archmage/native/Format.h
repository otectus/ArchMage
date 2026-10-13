// Native format constants and the schema (D-13, spec §10.1).
#pragma once

#include <cstdint>
#include <string_view>

namespace archmage::native {

inline constexpr std::string_view kExtension = ".archmage";
inline constexpr std::string_view kMimeType = "application/x-archmage"; // provisional (spec §10.1)
inline constexpr std::string_view kApplicationIdText = "archmage";
inline constexpr std::int64_t kApplicationId = 0x414D4147; // SQLite application_id, "AMAG"

/// Schema written by this version. Major 0 until the M2 exit (ADR-0013); every change bumps the minor.
inline constexpr int kSchemaMajor = 0;
inline constexpr int kSchemaMinor = 1;

/// The DDL of the current schema (io/native/schema/0001.sql, embedded at build time).
[[nodiscard]] std::string_view schemaSql();

// Media types of the resources a Stage 1 file holds.
inline constexpr std::string_view kTilePlaneMedia = "application/vnd.archmage.tile-plane";
inline constexpr std::string_view kIccMedia = "application/vnd.iccprofile";
inline constexpr std::string_view kPreviewMedia = "application/vnd.archmage.rgba8"; // straight RGBA8 rows

} // namespace archmage::native
