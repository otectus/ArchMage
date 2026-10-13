// Minimal PNG writer for the M0 probes (libpng). The product's PNG codec arrives in io/formats (S1-8).
#pragma once

#include <cstdint>
#include <filesystem>
#include <span>
#include <string>

namespace archmage::probes {

/// Writes 8-bit RGBA rows (width × 4 bytes each, top to bottom); returns an error message or "".
std::string writePngRgba8(const std::filesystem::path &path, std::uint32_t width, std::uint32_t height,
                          std::span<const std::uint8_t> rgba);

} // namespace archmage::probes
