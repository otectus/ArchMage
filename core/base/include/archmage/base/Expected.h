// A value or an error (ADR-0101): the subset of C++23 std::expected the engine uses, for C++20.
//
// Accessing value() on an error or error() on a value is a programming error: it prints a message and
// terminates; engine code reports expected failures as values and never throws (ADR-0023).
//
// Ownership and thread safety: a value type owning its contents; same thread safety as T and E.
#pragma once

#include <cstdio>
#include <cstdlib>
#include <functional>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>

namespace archmage::base {

template <class E> class Unexpected {
public:
    constexpr explicit Unexpected(const E &error) : error_(error) {}
    constexpr explicit Unexpected(E &&error) : error_(std::move(error)) {}

    [[nodiscard]] constexpr const E &error() const & noexcept { return error_; }
    [[nodiscard]] constexpr E &error() & noexcept { return error_; }
    [[nodiscard]] constexpr E &&error() && noexcept { return std::move(error_); }

private:
    E error_;
};

template <class E> [[nodiscard]] Unexpected<std::decay_t<E>> unexpected(E &&error) {
    return Unexpected<std::decay_t<E>>(std::forward<E>(error));
}

template <class T, class E> class Expected;

namespace detail {

[[noreturn]] inline void badAccess(const char *what) {
    std::fprintf(stderr, "archmage::base::Expected: %s\n", what);
    std::abort();
}

template <class T> struct IsUnexpected : std::false_type {};
template <class E> struct IsUnexpected<Unexpected<E>> : std::true_type {};
template <class T> struct IsExpected : std::false_type {};
template <class T, class E> struct IsExpected<Expected<T, E>> : std::true_type {};

} // namespace detail

template <class T, class E> class [[nodiscard]] Expected {
    static_assert(!std::is_reference_v<T> && !std::is_reference_v<E>, "Expected holds values");

public:
    using value_type = T;
    using error_type = E;

    constexpr Expected()
        requires std::is_default_constructible_v<T>
        : storage_(std::in_place_index<0>) {}

    template <class U = T>
        requires(std::is_constructible_v<T, U &&> && !std::is_same_v<std::remove_cvref_t<U>, Expected> &&
                 !detail::IsUnexpected<std::remove_cvref_t<U>>::value &&
                 !std::is_same_v<std::remove_cvref_t<U>, std::in_place_t>)
    constexpr Expected(U &&value) : storage_(std::in_place_index<0>, std::forward<U>(value)) {}

    template <class G>
        requires std::is_constructible_v<E, const G &>
    constexpr Expected(const Unexpected<G> &u) : storage_(std::in_place_index<1>, u.error()) {}

    template <class G>
        requires std::is_constructible_v<E, G &&>
    constexpr Expected(Unexpected<G> &&u) : storage_(std::in_place_index<1>, std::move(u).error()) {}

    template <class... Args>
    constexpr explicit Expected(std::in_place_t, Args &&...args)
        : storage_(std::in_place_index<0>, std::forward<Args>(args)...) {}

    [[nodiscard]] constexpr bool has_value() const noexcept { return storage_.index() == 0; }
    constexpr explicit operator bool() const noexcept { return has_value(); }

    constexpr T &value() & { return checkedValue(*this); }
    constexpr const T &value() const & { return checkedValue(*this); }
    constexpr T &&value() && { return std::move(checkedValue(*this)); }

    constexpr const E &error() const & { return checkedError(*this); }
    constexpr E &error() & { return checkedError(*this); }
    constexpr E &&error() && { return std::move(checkedError(*this)); }

    constexpr T &operator*() & { return value(); }
    constexpr const T &operator*() const & { return value(); }
    constexpr T &&operator*() && { return std::move(value()); }
    constexpr T *operator->() { return &value(); }
    constexpr const T *operator->() const { return &value(); }

    template <class U> [[nodiscard]] constexpr T value_or(U &&fallback) const & {
        return has_value() ? std::get<0>(storage_) : static_cast<T>(std::forward<U>(fallback));
    }
    template <class U> [[nodiscard]] constexpr T value_or(U &&fallback) && {
        return has_value() ? std::move(std::get<0>(storage_)) : static_cast<T>(std::forward<U>(fallback));
    }

    /// f(value) must return Expected<U, E>; an error passes through unchanged.
    template <class F> constexpr auto and_then(F &&f) & { return andThenImpl(*this, std::forward<F>(f)); }
    template <class F> constexpr auto and_then(F &&f) const & {
        return andThenImpl(*this, std::forward<F>(f));
    }
    template <class F> constexpr auto and_then(F &&f) && {
        return andThenImpl(std::move(*this), std::forward<F>(f));
    }

    /// f(value) returns U (possibly void); the result is Expected<U, E>.
    template <class F> constexpr auto transform(F &&f) & { return transformImpl(*this, std::forward<F>(f)); }
    template <class F> constexpr auto transform(F &&f) const & {
        return transformImpl(*this, std::forward<F>(f));
    }
    template <class F> constexpr auto transform(F &&f) && {
        return transformImpl(std::move(*this), std::forward<F>(f));
    }

    /// f(error) must return Expected<T, G>; a value passes through unchanged.
    template <class F> constexpr auto or_else(F &&f) const & {
        using R = std::remove_cvref_t<std::invoke_result_t<F, const E &>>;
        return has_value() ? R(std::in_place, std::get<0>(storage_))
                           : std::invoke(std::forward<F>(f), error());
    }
    template <class F> constexpr auto or_else(F &&f) && {
        using R = std::remove_cvref_t<std::invoke_result_t<F, E &&>>;
        return has_value() ? R(std::in_place, std::move(std::get<0>(storage_)))
                           : std::invoke(std::forward<F>(f), std::move(std::get<1>(storage_)));
    }

    /// f(error) returns G; the result is Expected<T, G>.
    template <class F> constexpr auto transform_error(F &&f) const & {
        using G = std::remove_cvref_t<std::invoke_result_t<F, const E &>>;
        if (has_value()) {
            return Expected<T, G>(std::in_place, std::get<0>(storage_));
        }
        return Expected<T, G>(Unexpected<G>(std::invoke(std::forward<F>(f), error())));
    }

private:
    template <class Self> static constexpr auto &checkedValue(Self &self) {
        if (!self.has_value()) {
            detail::badAccess("value() called on an error");
        }
        return std::get<0>(self.storage_);
    }
    template <class Self> static constexpr auto &checkedError(Self &self) {
        if (self.has_value()) {
            detail::badAccess("error() called on a value");
        }
        return std::get<1>(self.storage_);
    }
    template <class Self, class F> static constexpr auto andThenImpl(Self &&self, F &&f) {
        using R = std::remove_cvref_t<std::invoke_result_t<F, decltype(std::forward<Self>(self).value())>>;
        static_assert(detail::IsExpected<R>::value, "and_then must return an Expected");
        if (self.has_value()) {
            return std::invoke(std::forward<F>(f), std::forward<Self>(self).value());
        }
        return R(Unexpected<E>(std::forward<Self>(self).error()));
    }
    template <class Self, class F> static constexpr auto transformImpl(Self &&self, F &&f) {
        using U = std::remove_cv_t<std::invoke_result_t<F, decltype(std::forward<Self>(self).value())>>;
        if (!self.has_value()) {
            return Expected<U, E>(Unexpected<E>(std::forward<Self>(self).error()));
        }
        if constexpr (std::is_void_v<U>) {
            std::invoke(std::forward<F>(f), std::forward<Self>(self).value());
            return Expected<U, E>();
        } else {
            return Expected<U, E>(std::in_place,
                                  std::invoke(std::forward<F>(f), std::forward<Self>(self).value()));
        }
    }

    std::variant<T, E> storage_;
};

template <class E> class [[nodiscard]] Expected<void, E> {
public:
    using value_type = void;
    using error_type = E;

    constexpr Expected() noexcept = default;
    constexpr explicit Expected(std::in_place_t) noexcept {}

    template <class G>
        requires std::is_constructible_v<E, const G &>
    constexpr Expected(const Unexpected<G> &u) : error_(u.error()) {}

    template <class G>
        requires std::is_constructible_v<E, G &&>
    constexpr Expected(Unexpected<G> &&u) : error_(std::move(u).error()) {}

    [[nodiscard]] constexpr bool has_value() const noexcept { return !error_.has_value(); }
    constexpr explicit operator bool() const noexcept { return has_value(); }

    constexpr void value() const {
        if (!has_value()) {
            detail::badAccess("value() called on an error");
        }
    }
    constexpr const E &error() const & { return checkedError(*this); }
    constexpr E &error() & { return checkedError(*this); }
    constexpr E &&error() && { return std::move(checkedError(*this)); }

    template <class F> constexpr auto and_then(F &&f) const & {
        using R = std::remove_cvref_t<std::invoke_result_t<F>>;
        static_assert(detail::IsExpected<R>::value, "and_then must return an Expected");
        return has_value() ? std::invoke(std::forward<F>(f)) : R(Unexpected<E>(*error_));
    }
    template <class F> constexpr auto transform(F &&f) const & {
        using U = std::remove_cv_t<std::invoke_result_t<F>>;
        if (!has_value()) {
            return Expected<U, E>(Unexpected<E>(*error_));
        }
        if constexpr (std::is_void_v<U>) {
            std::invoke(std::forward<F>(f));
            return Expected<U, E>();
        } else {
            return Expected<U, E>(std::in_place, std::invoke(std::forward<F>(f)));
        }
    }
    template <class F> constexpr auto or_else(F &&f) const & {
        using R = std::remove_cvref_t<std::invoke_result_t<F, const E &>>;
        return has_value() ? R() : std::invoke(std::forward<F>(f), *error_);
    }
    template <class F> constexpr auto transform_error(F &&f) const & {
        using G = std::remove_cvref_t<std::invoke_result_t<F, const E &>>;
        return has_value() ? Expected<void, G>()
                           : Expected<void, G>(Unexpected<G>(std::invoke(std::forward<F>(f), *error_)));
    }

private:
    template <class Self> static constexpr auto &checkedError(Self &self) {
        if (self.has_value()) {
            detail::badAccess("error() called on a value");
        }
        return *self.error_;
    }

    std::optional<E> error_;
};

} // namespace archmage::base
