// ArchMage headless command-line interface (spec App. A.4). Qt-free (D-07).
// S1-0 provides --version only; validate and inspect arrive in S1-4, recover in S1-5,
// render in S1-8.

#include "archmage/document/Version.h"

#include <CLI/CLI.hpp>

#include <exception>
#include <iostream>

namespace {

// Exit codes shared by every subcommand (App. A.4, plan S1-4.8): 0 ok, 1 invalid input, 2 error.
constexpr int kExitOk = 0;
constexpr int kExitError = 2;

int run(int argc, char **argv) {
    CLI::App app{"ArchMage headless command-line interface", "archmage-cli"};
    app.set_version_flag("--version", archmage::document::versionBanner(), "Print the version and exit");

    try {
        app.parse(argc, argv);
    } catch (const CLI::ParseError &error) {
        // Prints the help or version text when requested, otherwise the parse error.
        return app.exit(error) == 0 ? kExitOk : kExitError;
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
