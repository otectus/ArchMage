// Overflow-checked integer arithmetic for sizes and extents (D-17, spec §6.3 "checked arithmetic for
// allocation sizes"). Each function returns nullopt instead of wrapping.
//
// Thread safety: pure functions.
#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>

#if !defined(__GNUC__) && !defined(__clang__)
#error                                                                                                       \
    "archmage/base/Checked.h relies on __builtin_*_overflow; add a portable implementation for this compiler"
#endif

namespace archmage::base {

template <class T> [[nodiscard]] constexpr std::optional<T> checkedAdd(T a, T b) {
    T result{};
    if (__builtin_add_overflow(a, b, &result)) {
        return std::nullopt;
    }
    return result;
}

template <class T> [[nodiscard]] constexpr std::optional<T> checkedSub(T a, T b) {
    T result{};
    if (__builtin_sub_overflow(a, b, &result)) {
        return std::nullopt;
    }
    return result;
}

template <class T> [[nodiscard]] constexpr std::optional<T> checkedMul(T a, T b) {
    T result{};
    if (__builtin_mul_overflow(a, b, &result)) {
        return std::nullopt;
    }
    return result;
}

/// Byte count of `count` elements of `elementBytes` each, or nullopt on overflow or a negative count.
[[nodiscard]] constexpr std::optional<std::size_t> checkedByteCount(std::int64_t count,
                                                                    std::size_t elementBytes) {
    if (count < 0) {
        return std::nullopt;
    }
    return checkedMul(static_cast<std::size_t>(count), elementBytes);
}

} // namespace archmage::base
