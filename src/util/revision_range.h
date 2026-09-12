#pragma once

#include <optional>
#include <string>
#include <utility>

inline std::optional<std::pair<std::string, std::string>> parse_revision_range(const std::string& text) {
    auto dots = text.find("..");
    if (dots == std::string::npos || dots == 0 || dots + 2 == text.size() ||
        text[dots + 2] == '.' || text.find("..", dots + 2) != std::string::npos) return std::nullopt;
    return std::pair{text.substr(0, dots), text.substr(dots + 2)};
}
