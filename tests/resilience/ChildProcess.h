// Runs a child process and captures its combined output (resilience tests: AT-013, AT-014).
#pragma once

#include <string>
#include <vector>

namespace archmage::testing {

struct ChildResult {
    int exitCode = -1; // valid when signal == 0
    int signal = 0;    // the signal that ended the child, or 0
    std::string output; // stdout and stderr, interleaved
    std::vector<std::string> lines;
};

/// Runs `args` (args[0] is the program path) with the current environment plus `environment`
/// ("NAME=value" entries) and waits for it.
[[nodiscard]] ChildResult runChild(const std::vector<std::string> &args,
                                   const std::vector<std::string> &environment = {});

} // namespace archmage::testing
