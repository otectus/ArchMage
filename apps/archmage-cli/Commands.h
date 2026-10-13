// archmage-cli subcommands beyond validate and inspect (spec App. A.4). Qt-free (D-07).
#pragma once

#include "archmage/base/Error.h"

#include <nlohmann/json.hpp>

#include <optional>
#include <string>

namespace archmage::cli {

// Exit codes shared by every subcommand (App. A.4, plan S1-4.8): 0 ok, 1 invalid input, 2 error.
inline constexpr int kExitOk = 0;
inline constexpr int kExitInvalid = 1;
inline constexpr int kExitError = 2;

[[nodiscard]] nlohmann::json errorRecord(const base::Error &error);
/// Prints a structured error (JSON document with --json, one stderr line otherwise); returns 2.
int reportError(const base::Error &error, bool asJson);

struct RecoverOptions {
    std::string root;     // recovery root; empty: the default location
    std::string session;  // empty: list candidates
    std::string document; // needed when the session journaled several documents
    std::string output;   // the file to write; never overwritten
    bool asJson = false;
};
/// `recover`: lists recoverable documents, or writes one to a new file (plan S1-5.9).
int runRecover(const RecoverOptions &options);

struct SelfTestSaveOptions {
    std::string root;
    std::string target;
    std::string fault;          // ARCHMAGE_FAULT grammar, armed for the second phase
    std::string phase = "journal"; // journal: armed before the journal write; save: before the save
    bool fallback = false;      // save through the backup fallback
};
/// Hidden `selftest-save` (AT-013, plan S1-5.5): saves and journals a document, then repeats both with a
/// fault armed. Prints "committed", "journaled" and "saved" lines as each step is acknowledged.
int runSelfTestSave(const SelfTestSaveOptions &options);

struct SelfTestDiskFullOptions {
    std::string root;
    std::string target;  // an existing document, opened and grown
    std::string saveAs;  // where the retry goes
    std::string mode = "rlimit"; // rlimit: RLIMIT_FSIZE below the new size; enospc: enospc@tile_write
};
/// Hidden `selftest-disk-full` (AT-014, plan S1-5.6).
int runSelfTestDiskFull(const SelfTestDiskFullOptions &options);

} // namespace archmage::cli
