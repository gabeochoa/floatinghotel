#pragma once

#include <string>
#include <utility>

inline std::pair<std::string, std::string> diff_revisions(const std::string& scope) {
    if (scope == "wt") return {"INDEX", ""};
    if (scope == "index") return {"HEAD", "INDEX"};
    if (scope.starts_with("file:")) return {"", scope.substr(5)};
    if (scope.starts_with("compare:")) {
        auto separator = scope.find(':', 8);
        if (separator != std::string::npos) return {scope.substr(8, separator - 8), scope.substr(separator + 1)};
    }
    return {scope + "^", scope};
}
