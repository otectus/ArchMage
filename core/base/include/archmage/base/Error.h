// Structured domain errors (spec §26.2, ADR-0023, ADR-0101): a category, a stable diagnostic code, a
// user-facing message and a technical detail. Never catch everything and continue silently.
//
// Thread safety: plain value types.
#pragma once

#include "archmage/base/Expected.h"

#include <string>
#include <string_view>

namespace archmage::base {

enum class ErrorCategory {
    UnsupportedInput,    // valid input ArchMage cannot handle (yet)
    DamagedInput,        // corrupt, truncated or inconsistent input
    Cancelled,           // the operation was cancelled
    ResourceExhausted,   // memory, disk, VRAM or another budget ran out
    ProviderUnavailable, // an optional codec, model or service is missing
    PermissionDenied,    // the platform refused access
    Internal,            // a defect in ArchMage
    Validation,          // a command's preconditions do not hold (locked, unknown target, bad value)
    Conflict,            // the request was made against a different revision
};

/// Stable lowercase name, e.g. "damaged_input"; used in logs, CLI JSON and tests.
[[nodiscard]] std::string_view categoryName(ErrorCategory category);

struct Error {
    ErrorCategory category = ErrorCategory::Internal;
    std::string code;    // stable machine-readable code, e.g. "layer.locked"
    std::string message; // explanation for the user
    std::string detail;  // technical diagnostic: offsets, paths, values

    friend bool operator==(const Error &, const Error &) = default;
};

/// "category/code: message (detail)".
[[nodiscard]] std::string format(const Error &error);

template <class T> using Result = Expected<T, Error>;
using Status = Expected<void, Error>;

/// Shorthand for returning an error: `return fail(ErrorCategory::Validation, "layer.locked", "...");`
[[nodiscard]] inline Unexpected<Error> fail(ErrorCategory category, std::string code, std::string message,
                                            std::string detail = {}) {
    return Unexpected<Error>(Error{category, std::move(code), std::move(message), std::move(detail)});
}

} // namespace archmage::base
