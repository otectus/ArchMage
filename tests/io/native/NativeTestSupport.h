// Helpers shared by the native format tests: scratch directories on /tmp (tmpfs) and on the build tree's
// filesystem (the home filesystem), a layered sample document built through commands, and comparisons.
#pragma once

#include "archmage/brush/Stroke.h"
#include "archmage/commands/MutationExecutor.h"
#include "archmage/native/NativeFile.h"
#include "archmage/native/SaveTransaction.h"

#include <filesystem>
#include <functional>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace archmage::native::testing {

/// A fresh directory removed with its content on destruction.
class ScratchDirectory {
public:
    explicit ScratchDirectory(const std::filesystem::path &parent);
    ~ScratchDirectory();
    ScratchDirectory(const ScratchDirectory &) = delete;
    ScratchDirectory &operator=(const ScratchDirectory &) = delete;

    [[nodiscard]] const std::filesystem::path &path() const { return path_; }
    [[nodiscard]] std::filesystem::path operator/(std::string_view name) const { return path_ / name; }

private:
    std::filesystem::path path_;
};

/// /tmp, and a directory in the build tree (the home filesystem).
[[nodiscard]] std::filesystem::path tmpRoot();
[[nodiscard]] std::filesystem::path homeRoot();

[[nodiscard]] std::vector<brush::StrokeSample> line(base::DPoint from, base::DPoint to, int steps);
[[nodiscard]] commands::CommandEnvelope stroke(const std::string &layer, const brush::BrushSettings &settings,
                                               const std::vector<brush::StrokeSample> &samples);

/// Background (white), "Ink" (painted), "Glaze" (painted, opacity 0.5, offset 17,-9) and a hidden instance
/// of Ink sharing its raster, built through commands.
struct SampleDocument {
    std::unique_ptr<commands::MutationExecutor> executor;
    std::string background;
    std::string ink;
    std::string glaze;
    std::string instance;
};
[[nodiscard]] SampleDocument buildSampleDocument(std::int64_t width, std::int64_t height,
                                                 std::string_view sampleType = "uint8");

/// The canvas as straight RGBA8, rendered by the CPU evaluator.
[[nodiscard]] std::vector<std::byte> composite(const document::DocumentState &state);
[[nodiscard]] bool sameTiles(const tiles::TileMap &a, const tiles::TileMap &b);
[[nodiscard]] std::string fileHash(const std::filesystem::path &file);
[[nodiscard]] std::vector<std::string> directoryEntries(const std::filesystem::path &directory);

[[nodiscard]] base::Result<SaveOutcome> save(const std::shared_ptr<const document::Revision> &revision,
                                             const std::filesystem::path &target, FileIdentity identity,
                                             std::function<base::Status(SaveStep)> hook = {});

/// Runs SQL against a saved file (tests that tamper with or inject content).
void execSql(const std::filesystem::path &file, const std::string &sql);

} // namespace archmage::native::testing
