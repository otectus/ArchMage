// ADR-0101: Expected and the structured Error.

#include "archmage/base/Expected.h"
#include "archmage/base/Error.h"

#include <gtest/gtest.h>

#include <memory>
#include <string>

namespace archmage::base {
namespace {

Result<int> parsePositive(int v) {
    if (v <= 0) {
        return fail(ErrorCategory::Validation, "value.not_positive", "Value must be positive",
                    std::to_string(v));
    }
    return v;
}

TEST(Expected, HoldsAValueOrAnError) {
    const Result<int> ok = parsePositive(3);
    ASSERT_TRUE(ok.has_value());
    EXPECT_TRUE(static_cast<bool>(ok));
    EXPECT_EQ(*ok, 3);
    EXPECT_EQ(ok.value_or(9), 3);

    const Result<int> bad = parsePositive(-1);
    ASSERT_FALSE(bad.has_value());
    EXPECT_EQ(bad.error().category, ErrorCategory::Validation);
    EXPECT_EQ(bad.error().code, "value.not_positive");
    EXPECT_EQ(bad.value_or(9), 9);
}

TEST(Expected, SameValueAndErrorTypeStayDistinct) {
    const Expected<std::string, std::string> value{std::string("v")};
    const Expected<std::string, std::string> error{unexpected(std::string("e"))};
    EXPECT_TRUE(value.has_value());
    EXPECT_EQ(*value, "v");
    EXPECT_FALSE(error.has_value());
    EXPECT_EQ(error.error(), "e");
}

TEST(Expected, MonadicOperations) {
    const auto doubled = parsePositive(4).and_then([](int v) { return parsePositive(v * 2); });
    EXPECT_EQ(*doubled, 8);
    const auto stopped = parsePositive(-4).and_then([](int v) { return parsePositive(v * 2); });
    EXPECT_EQ(stopped.error().code, "value.not_positive");

    const Expected<std::string, Error> text =
        parsePositive(5).transform([](int v) { return std::to_string(v); });
    EXPECT_EQ(*text, "5");
    const Status asStatus = parsePositive(5).transform([](int) {});
    EXPECT_TRUE(asStatus.has_value());

    const Result<int> recovered = parsePositive(0).or_else([](const Error &) -> Result<int> { return 1; });
    EXPECT_EQ(*recovered, 1);

    const Expected<int, std::string> renamed =
        parsePositive(0).transform_error([](const Error &e) { return e.code; });
    EXPECT_EQ(renamed.error(), "value.not_positive");
}

TEST(Expected, MoveOnlyValues) {
    Expected<std::unique_ptr<int>, Error> owned{std::make_unique<int>(7)};
    ASSERT_TRUE(owned);
    std::unique_ptr<int> taken = std::move(owned).value();
    EXPECT_EQ(*taken, 7);
    auto chained = Expected<std::unique_ptr<int>, Error>{std::make_unique<int>(2)}.transform(
        [](std::unique_ptr<int> &&p) { return *p * 10; });
    EXPECT_EQ(*chained, 20);
}

TEST(Expected, VoidSpecialization) {
    const Status ok;
    EXPECT_TRUE(ok.has_value());
    const Status bad = fail(ErrorCategory::Cancelled, "job.cancelled", "Cancelled");
    ASSERT_FALSE(bad);
    EXPECT_EQ(bad.error().category, ErrorCategory::Cancelled);
    const Result<int> next = ok.and_then([]() -> Result<int> { return 5; });
    EXPECT_EQ(*next, 5);
    const Result<int> skipped = bad.and_then([]() -> Result<int> { return 5; });
    EXPECT_EQ(skipped.error().code, "job.cancelled");
}

TEST(ExpectedDeathTest, WrongAccessTerminates) {
    const Result<int> bad = parsePositive(0);
    EXPECT_DEATH((void)bad.value(), "value\\(\\) called on an error");
    const Result<int> good = parsePositive(1);
    EXPECT_DEATH((void)good.error(), "error\\(\\) called on a value");
}

TEST(Error, EveryCategoryFormats) {
    const ErrorCategory all[] = {
        ErrorCategory::UnsupportedInput,  ErrorCategory::DamagedInput,        ErrorCategory::Cancelled,
        ErrorCategory::ResourceExhausted, ErrorCategory::ProviderUnavailable, ErrorCategory::PermissionDenied,
        ErrorCategory::Internal,          ErrorCategory::Validation,          ErrorCategory::Conflict};
    const char *names[] = {"unsupported_input",  "damaged_input",        "cancelled",
                           "resource_exhausted", "provider_unavailable", "permission_denied",
                           "internal",           "validation",           "conflict"};
    for (std::size_t i = 0; i < std::size(all); ++i) {
        EXPECT_EQ(categoryName(all[i]), names[i]);
        const Error e{all[i], "x.code", "Message", "detail"};
        EXPECT_EQ(format(e), std::string(names[i]) + "/x.code: Message (detail)");
    }
    EXPECT_EQ(format(Error{ErrorCategory::Internal, "c", "m", ""}), "internal/c: m");
}

} // namespace
} // namespace archmage::base
