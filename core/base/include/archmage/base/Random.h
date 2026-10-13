#pragma once

#include <cstddef>
#include <span>

namespace archmage::base {

/// Fills `out` from the operating system's cryptographically secure generator (getrandom on Linux).
/// Terminates with a message if the platform generator fails; identifiers must never repeat (D-16).
/// Thread safety: safe from any thread.
void secureRandomBytes(std::span<std::byte> out);

} // namespace archmage::base
