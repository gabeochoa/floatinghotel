#pragma once

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace fuzzy {

inline std::optional<int> score(std::string_view query, std::string_view path) {
    size_t at = 0;
    int value = 0;
    int previous = -2;
    for (size_t i = 0; i < path.size() && at < query.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(path[i])) !=
            std::tolower(static_cast<unsigned char>(query[at]))) continue;
        value += previous + 1 == static_cast<int>(i) ? 8 : 1;
        if (i == 0 || path[i - 1] == '/' || path[i - 1] == '_' || path[i - 1] == '-') value += 12;
        previous = static_cast<int>(i);
        ++at;
    }
    if (at != query.size()) return std::nullopt;
    return value * 100 - static_cast<int>(std::min<size_t>(path.size(), 10000));
}

inline std::vector<std::string> rank(const std::vector<std::string>& paths, const std::string& query) {
    std::vector<std::pair<int, std::string>> scored;
    for (const auto& path : paths)
        if (auto value = score(query, path)) scored.emplace_back(*value, path);
    std::sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) {
        return a.first == b.first ? a.second < b.second : a.first > b.first;
    });
    std::vector<std::string> results;
    for (auto& [value, path] : scored) results.push_back(std::move(path));
    return results;
}

}
