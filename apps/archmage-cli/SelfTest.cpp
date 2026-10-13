// Hidden self-test subcommands that resilience tests run as child processes (plan S1-5.5, S1-5.6). They
// print one line per acknowledged step and flush it at once, so a parent that kills or loses the child
// still knows what was acknowledged.

#include "Commands.h"

#include "archmage/brush/Stroke.h"
#include "archmage/commands/MutationExecutor.h"
#include "archmage/native/FaultPoints.h"
#include "archmage/native/Recovery.h"
#include "archmage/native/SaveTransaction.h"
#include "archmage/base/Sha256.h"

#include <chrono>
#include <csignal>
#include <fstream>
#include <iostream>
#include <iterator>

#include <sys/resource.h>

namespace archmage::cli {
namespace {

using commands::CommandEnvelope;
using commands::MutationExecutor;
using nlohmann::json;

void say(const std::string &line) { std::cout << line << std::endl; } // endl: flushed before anything else

int failWith(const base::Error &error) {
    say("error " + base::format(error));
    return kExitError;
}

CommandEnvelope stroke(const std::string &layer, std::array<float, 3> color, base::DPoint from, base::DPoint to,
                       double size) {
    brush::BrushSettings settings;
    settings.color = color;
    settings.size = size;
    std::vector<brush::StrokeSample> samples;
    for (int i = 0; i <= 40; ++i) {
        const double t = i / 40.0;
        samples.push_back({{from.x + (to.x - from.x) * t, from.y + (to.y - from.y) * t}, 1.0f, i * 4'166'667LL});
    }
    json parameters = settings.toJson();
    parameters["samples"] = brush::samplesToJson(samples);
    return CommandEnvelope::make("archmage.raster.paintStroke", parameters, {layer});
}

base::Result<commands::CommandResult> commit(MutationExecutor &executor, CommandEnvelope envelope) {
    auto result = executor.execute(std::move(envelope));
    if (result) {
        say("committed " + result->revisionId.toString() + " " + std::to_string(result->revision));
    }
    return result;
}

base::Status journal(native::DocumentJournal &journal, MutationExecutor &executor) {
    auto flushed = journal.flush();
    if (flushed) {
        say("journaled " + executor.current()->id.toString() + " " + std::to_string(executor.current()->sequence));
    }
    return flushed;
}

std::string fileHash(const std::filesystem::path &path) {
    std::ifstream in(path, std::ios::binary);
    const std::vector<char> bytes{std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>()};
    return base::Sha256::of(std::as_bytes(std::span(bytes))).hex();
}

native::RecoverySession::Options manualJournal() {
    native::RecoverySession::Options options;
    options.runThread = false;
    options.compactEvery = 1; // every write also compacts and checkpoints: every journal fault point is reached
    options.previewMaxSize = 64;
    options.previewInterval = std::chrono::milliseconds(0);
    return options;
}

} // namespace

int runSelfTestSave(const SelfTestSaveOptions &options) {
    const auto spec = native::parseFaultSpec(options.fault);
    if (!spec) {
        return failWith({base::ErrorCategory::Validation, "selftest.fault", "Invalid fault", options.fault});
    }
    auto session = native::RecoverySession::start(options.root, manualJournal());
    if (!session) {
        return failWith(session.error());
    }
    auto created = commands::createDocument(CommandEnvelope::make(
        "archmage.document.create", {{"width", 384}, {"height", 256}, {"title", "Self-test"}}));
    if (!created) {
        return failWith(created.error());
    }
    MutationExecutor executor(created->state, "New Document");
    auto tracked = (*session)->track(executor);
    if (!tracked) {
        return failWith(tracked.error());
    }
    native::DocumentJournal &journalRef = **tracked;
    const auto added = executor.execute(CommandEnvelope::make("archmage.layer.add", {{"name", "Paint"}}));
    if (!added) {
        return failWith(added.error());
    }
    const std::string layer = added->result["layerId"].get<std::string>();
    const std::filesystem::path target = options.target;
    const auto save = [&](bool fallback) -> base::Status {
        native::SaveRequest request;
        request.target = target;
        request.revision = executor.current();
        request.identity = native::identityForSave(*executor.current()->state, std::nullopt);
        request.previewMaxSize = 128;
        if (fallback) {
            request.filesystem = native::FilesystemInfo{"forced", true, false};
        }
        auto saved = native::saveDocument(request);
        if (!saved) {
            return base::unexpected(saved.error());
        }
        say("saved " + request.revision->id.toString() + " " + std::to_string(request.revision->sequence));
        executor.markSaved(saved->sequence);
        return journalRef.markSaved(*request.revision, target);
    };

    // Phase 1, no fault: revision A committed, journaled and saved; revision B committed and journaled.
    if (auto a = commit(executor, stroke(layer, {0.8f, 0.1f, 0.1f}, {10, 10}, {370, 240}, 30.0)); !a) {
        return failWith(a.error());
    }
    if (auto status = journal(journalRef, executor); !status) {
        return failWith(status.error());
    }
    if (auto status = save(false); !status) {
        return failWith(status.error());
    }
    if (auto b = commit(executor, stroke(layer, {0.1f, 0.6f, 0.2f}, {370, 10}, {10, 240}, 24.0)); !b) {
        return failWith(b.error());
    }
    if (auto status = journal(journalRef, executor); !status) {
        return failWith(status.error());
    }

    // Phase 2: revision C, with the fault armed before the journal write or before the save.
    if (options.phase == "journal") {
        native::FaultInjector::global().arm(*spec);
    }
    if (auto c = commit(executor, stroke(layer, {0.1f, 0.2f, 0.9f}, {190, 5}, {190, 250}, 40.0)); !c) {
        return failWith(c.error());
    }
    if (auto status = journal(journalRef, executor); !status) {
        say("journal_failed " + base::format(status.error()));
    }
    if (options.phase == "save") {
        native::FaultInjector::global().arm(*spec);
    }
    if (auto status = save(options.fallback); !status) {
        say("save_failed " + base::format(status.error()));
    }
    say("survived");
    return kExitOk;
}

int runSelfTestDiskFull(const SelfTestDiskFullOptions &options) {
    const std::filesystem::path target = options.target;
    auto opened = native::readDocument(target);
    if (!opened || !opened->state) {
        return failWith(opened ? base::Error{base::ErrorCategory::UnsupportedInput, "selftest.read_only", "", ""}
                               : opened.error());
    }
    auto session = native::RecoverySession::start(options.root, manualJournal());
    if (!session) {
        return failWith(session.error());
    }
    MutationExecutor executor(*opened->state, "Open");
    auto tracked = (*session)->track(executor, target);
    if (!tracked) {
        return failWith(tracked.error());
    }
    native::DocumentJournal &journalRef = **tracked;
    const std::uintmax_t originalSize = std::filesystem::file_size(target);
    const std::string originalHash = fileHash(target);

    // Grow the document well beyond the original file's size, journal it, then run out of space.
    const auto added = executor.execute(CommandEnvelope::make("archmage.layer.add", {{"name", "Growth"}}));
    if (!added) {
        return failWith(added.error());
    }
    const std::string layer = added->result["layerId"].get<std::string>();
    const auto width = static_cast<double>(executor.current()->state->width);
    const auto height = static_cast<double>(executor.current()->state->height);
    for (int i = 0; i < 12; ++i) {
        const float shade = static_cast<float>(i) / 12.0f;
        const double y = height * (i + 0.5) / 12.0;
        if (auto painted = commit(executor, stroke(layer, {shade, 1.0f - shade, 0.5f}, {0, y}, {width, height - y}, 18.0));
            !painted) {
            return failWith(painted.error());
        }
    }
    if (auto status = journal(journalRef, executor); !status) {
        return failWith(status.error());
    }

    rlimit previous{};
    if (options.mode == "rlimit") {
        std::signal(SIGXFSZ, SIG_IGN); // a write beyond the limit fails with EFBIG instead of killing us
        getrlimit(RLIMIT_FSIZE, &previous);
        const rlimit limited{static_cast<rlim_t>(originalSize), previous.rlim_max};
        if (setrlimit(RLIMIT_FSIZE, &limited) != 0) {
            return failWith({base::ErrorCategory::Internal, "selftest.rlimit", "setrlimit failed", ""});
        }
        say("limit " + std::to_string(originalSize));
    } else {
        native::FaultInjector::global().arm({native::FaultPoint::TileWrite, native::FaultMode::Enospc, 1});
    }

    native::SaveRequest request;
    request.target = target;
    request.revision = executor.current();
    request.identity = native::identityForSave(*executor.current()->state, native::FileIdentity{opened->info.documentId, {}});
    auto failed = native::saveDocument(request);
    if (failed) {
        say("save_succeeded");
        return kExitError;
    }
    say("save_failed " + std::string(base::categoryName(failed.error().category)) + " " + failed.error().code);
    say("detail " + failed.error().detail);

    // Space is back (the user freed some, or chose another disk): nothing was lost meanwhile.
    if (options.mode == "rlimit") {
        setrlimit(RLIMIT_FSIZE, &previous);
    } else {
        native::FaultInjector::global().disarm();
    }
    say(std::string("original ") + (fileHash(target) == originalHash ? "intact" : "changed"));
    say(std::string("modified ") + (executor.isModified() ? "true" : "false"));
    if (auto more = commit(executor, CommandEnvelope::make("archmage.layer.rename", {{"name", "After the failure"}},
                                                           {layer}));
        !more) {
        return failWith(more.error());
    }
    if (auto status = journal(journalRef, executor); !status) {
        return failWith(status.error());
    }
    native::SaveRequest elsewhere;
    elsewhere.target = options.saveAs;
    elsewhere.revision = executor.current();
    elsewhere.identity = native::identityForSaveAs(*executor.current()->state,
                                                   native::FileIdentity{opened->info.documentId, {}});
    auto saved = native::saveDocument(elsewhere);
    if (!saved) {
        return failWith(saved.error());
    }
    say("saved_as " + saved->path.string());
    if (auto status = (*session)->untrack(*tracked, true); !status) {
        return failWith(status.error());
    }
    return kExitOk;
}

} // namespace archmage::cli
