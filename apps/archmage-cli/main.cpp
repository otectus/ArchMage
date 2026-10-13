// ArchMage headless command-line interface (spec App. A.4, §23.3). Qt-free (D-07).
// validate and inspect arrive in S1-4, recover in S1-5, render in S1-8.
//
// Output contract: human-readable text by default; --json prints one JSON document on stdout. Errors are
// structured records {category, code, message, detail}: in the JSON document with --json, otherwise one
// line on stderr. Nothing waits for input.

#include "Commands.h"

#include "archmage/document/Version.h"
#include "archmage/native/NativeFile.h"

#include <CLI/CLI.hpp>
#include <nlohmann/json.hpp>

#include <exception>
#include <iostream>
#include <string>

namespace {

using archmage::base::Error;
using archmage::cli::errorRecord;
using archmage::cli::kExitError;
using archmage::cli::kExitInvalid;
using archmage::cli::kExitOk;
using archmage::cli::reportError;
using nlohmann::json;

int runValidate(const std::string &file, bool deep, bool asJson) {
    using archmage::native::ValidationLevel;
    const ValidationLevel level = deep ? ValidationLevel::Deep : ValidationLevel::Standard;
    auto report = archmage::native::validateFile(file, level);
    if (!report) {
        return reportError(report.error(), asJson);
    }
    const int exitCode = report->valid() ? kExitOk : kExitInvalid;
    if (asJson) {
        json problems = json::array();
        for (const Error &problem : report->problems) {
            problems.push_back(errorRecord(problem));
        }
        json out{{"ok", report->valid()},
                 {"file", file},
                 {"level", deep ? "deep" : "standard"},
                 {"schema", {{"major", report->info.schemaMajor}, {"minor", report->info.schemaMinor}}},
                 {"readOnly", report->info.readOnly},
                 {"counts",
                  {{"objects", report->objects},
                   {"relations", report->relations},
                   {"resources", report->resources},
                   {"tilePlanes", report->tilePlanes},
                   {"resourcesVerified", report->resourcesVerified}}},
                 {"problems", problems},
                 {"notes", report->notes}};
        if (report->info.readOnly) {
            out["readOnlyReason"] = report->info.readOnlyReason;
        }
        std::cout << out.dump(2) << '\n';
        return exitCode;
    }
    if (report->valid()) {
        std::cout << "valid: " << file << " (format " << report->info.schemaMajor << '.' << report->info.schemaMinor
                  << ", " << report->objects << " objects, " << report->tilePlanes << " tile planes, "
                  << report->resources << " resources";
        if (deep) {
            std::cout << ", " << report->resourcesVerified << " verified";
        }
        std::cout << ")\n";
    } else {
        std::cout << "invalid: " << file << '\n';
        for (const Error &problem : report->problems) {
            std::cout << "  " << archmage::base::format(problem) << '\n';
        }
    }
    for (const std::string &note : report->notes) {
        std::cout << "note: " << note << '\n';
    }
    return exitCode;
}

int runInspect(const std::string &file, bool asJson) {
    auto inspected = archmage::native::inspectFile(file);
    if (!inspected) {
        // A file that is not an ArchMage document, or is damaged, is invalid input rather than an error.
        const auto category = inspected.error().category;
        const bool invalid = category == archmage::base::ErrorCategory::DamagedInput ||
                             category == archmage::base::ErrorCategory::UnsupportedInput;
        const int code = reportError(inspected.error(), asJson);
        return invalid ? kExitInvalid : code;
    }
    const json &out = *inspected;
    if (asJson) {
        std::cout << out.dump(2) << '\n';
        return kExitOk;
    }
    std::cout << file << ": ArchMage document, format " << out["schema"]["major"] << '.' << out["schema"]["minor"]
              << '\n';
    const json &manifest = out["manifest"];
    for (const char *key : {"document_id", "derived_from", "current_revision", "writer", "saved_at"}) {
        if (manifest.contains(key)) {
            std::cout << "  " << key << ": " << manifest[key].get<std::string>() << '\n';
        }
    }
    if (out.value("readOnly", false)) {
        std::cout << "  read-only: " << out["readOnlyExplanation"].get<std::string>() << '\n';
    }
    if (out.contains("layers")) {
        std::cout << "  layers (bottom to top):\n";
        for (const json &layer : out["layers"]) {
            std::cout << "    " << layer["index"] << ' ' << layer.value("name", std::string("?")) << " ["
                      << layer["type"].get<std::string>() << "]\n";
        }
        std::cout << "  resources: " << out["resources"]["count"] << " (" << out["resources"]["storedBytes"]
                  << " bytes stored, " << out["resources"]["rawBytes"] << " raw)\n";
    }
    return kExitOk;
}

int run(int argc, char **argv) {
    CLI::App app{"ArchMage headless command-line interface", "archmage-cli"};
    app.set_version_flag("--version", archmage::document::versionBanner(), "Print the version and exit");
    app.require_subcommand(0, 1);

    std::string validateFile;
    bool deep = false;
    bool validateJson = false;
    CLI::App *validate = app.add_subcommand("validate", "Check an .archmage file's structure, references and content");
    validate->add_option("file", validateFile, "The .archmage file")->required();
    validate->add_flag("--deep", deep, "Also run SQLite's integrity check and verify every stored resource");
    validate->add_flag("--json", validateJson, "Print the report as JSON");

    std::string inspectFile;
    bool inspectJson = false;
    CLI::App *inspect = app.add_subcommand("inspect", "Describe an .archmage file without loading its pixels");
    inspect->add_option("file", inspectFile, "The .archmage file")->required();
    inspect->add_flag("--json", inspectJson, "Print the description as JSON");

    archmage::cli::RecoverOptions recoverOptions;
    CLI::App *recover = app.add_subcommand(
        "recover", "List documents left by sessions that did not end normally, or write one to a new file");
    recover->add_option("--session", recoverOptions.session, "Session to recover from (see the list)");
    recover->add_option("--document", recoverOptions.document, "Document within the session, when it has several");
    recover->add_option("--output", recoverOptions.output, "New file to write; an existing file is never replaced");
    recover->add_option("--root", recoverOptions.root, "Recovery folder (default: the standard location)");
    recover->add_flag("--json", recoverOptions.asJson, "Print the list or the result as JSON");

    // Child processes of the resilience tests (AT-013, AT-014); not for users.
    archmage::cli::SelfTestSaveOptions saveTest;
    CLI::App *selftestSave = app.add_subcommand("selftest-save", "Resilience test child (AT-013)")->group("");
    selftestSave->add_option("--root", saveTest.root)->required();
    selftestSave->add_option("--target", saveTest.target)->required();
    selftestSave->add_option("--fault", saveTest.fault)->required();
    selftestSave->add_option("--phase", saveTest.phase)->check(CLI::IsMember({"journal", "save"}));
    selftestSave->add_flag("--fallback", saveTest.fallback);
    archmage::cli::SelfTestDiskFullOptions diskTest;
    CLI::App *selftestDisk = app.add_subcommand("selftest-disk-full", "Resilience test child (AT-014)")->group("");
    selftestDisk->add_option("--root", diskTest.root)->required();
    selftestDisk->add_option("--target", diskTest.target)->required();
    selftestDisk->add_option("--save-as", diskTest.saveAs)->required();
    selftestDisk->add_option("--mode", diskTest.mode)->check(CLI::IsMember({"rlimit", "enospc"}));

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError &error) {
        // Prints the help or version text when requested, otherwise the parse error.
        return app.exit(error) == 0 ? kExitOk : kExitError;
    }

    if (validate->parsed()) {
        return runValidate(validateFile, deep, validateJson);
    }
    if (inspect->parsed()) {
        return runInspect(inspectFile, inspectJson);
    }
    if (recover->parsed()) {
        return archmage::cli::runRecover(recoverOptions);
    }
    if (selftestSave->parsed()) {
        return archmage::cli::runSelfTestSave(saveTest);
    }
    if (selftestDisk->parsed()) {
        return archmage::cli::runSelfTestDiskFull(diskTest);
    }
    std::cout << app.help();
    return kExitOk;
}

} // namespace

int main(int argc, char **argv) {
    // CLI11 reports misuse by throwing; contain it here (D-23) instead of terminating.
    try {
        return run(argc, argv);
    } catch (const std::exception &error) {
        std::cerr << "archmage-cli: internal error: " << error.what() << '\n';
        return kExitError;
    }
}
