#include "Commands.h"

#include <iostream>

namespace archmage::cli {

nlohmann::json errorRecord(const base::Error &error) {
    return {{"category", std::string(base::categoryName(error.category))},
            {"code", error.code},
            {"message", error.message},
            {"detail", error.detail}};
}

int reportError(const base::Error &error, bool asJson) {
    if (asJson) {
        std::cout << nlohmann::json{{"ok", false}, {"error", errorRecord(error)}}.dump(2) << '\n';
    } else {
        std::cerr << "archmage-cli: " << base::format(error) << '\n';
    }
    return kExitError;
}

} // namespace archmage::cli
