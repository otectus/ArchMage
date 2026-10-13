#include "archmage/base/Error.h"

namespace archmage::base {

std::string_view categoryName(ErrorCategory category) {
    switch (category) {
    case ErrorCategory::UnsupportedInput:
        return "unsupported_input";
    case ErrorCategory::DamagedInput:
        return "damaged_input";
    case ErrorCategory::Cancelled:
        return "cancelled";
    case ErrorCategory::ResourceExhausted:
        return "resource_exhausted";
    case ErrorCategory::ProviderUnavailable:
        return "provider_unavailable";
    case ErrorCategory::PermissionDenied:
        return "permission_denied";
    case ErrorCategory::Internal:
        return "internal";
    case ErrorCategory::Validation:
        return "validation";
    case ErrorCategory::Conflict:
        return "conflict";
    }
    return "internal";
}

std::string format(const Error &error) {
    std::string text{categoryName(error.category)};
    text += '/';
    text += error.code;
    text += ": ";
    text += error.message;
    if (!error.detail.empty()) {
        text += " (";
        text += error.detail;
        text += ')';
    }
    return text;
}

} // namespace archmage::base
