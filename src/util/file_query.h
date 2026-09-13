#pragma once

#include <algorithm>
#include <charconv>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace file_query {

struct Position {
    int line = 1;
    int column = 1;
    bool operator==(const Position&) const = default;
};

struct Query {
    std::string path;
    std::optional<Position> position;
    std::string error;
};

inline std::optional<int> positive(std::string_view text) {
    int value = 0;
    auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || value < 1) return {};
    return value;
}

inline Query line(std::string_view text) {
    Query query;
    const auto colon = text.find(':');
    const auto row = positive(text.substr(0, colon));
    const auto column = colon == std::string_view::npos ? std::optional<int>{1} : positive(text.substr(colon + 1));
    if (row && column) query.position = Position{*row, *column};
    else if (!text.empty()) query.error = "Enter a positive line number and optional column: line[:column]";
    return query;
}

inline Query parse(const std::string& text, const std::vector<std::string>& paths) {
    if (std::find(paths.begin(), paths.end(), text) != paths.end()) return {text};
    const auto last = text.rfind(':');
    if (last == std::string::npos) return {text};
    auto prefix = text.substr(0, last);
    Query query;
    const auto previous = prefix.rfind(':');
    if (std::find(paths.begin(), paths.end(), prefix) != paths.end() || previous == std::string::npos) {
        query = line(std::string_view(text).substr(last + 1));
        query.path = prefix;
    } else {
        const auto middle = std::string_view(prefix).substr(previous + 1);
        const bool numeric = !middle.empty() && std::all_of(middle.begin(), middle.end(), [](char c) { return c >= '0' && c <= '9'; });
        if (numeric || middle.starts_with('-') || middle.empty()) {
            query = line(std::string_view(text).substr(previous + 1));
            query.path = prefix.substr(0, previous);
        } else {
            query = line(std::string_view(text).substr(last + 1));
            query.path = prefix;
        }
    }
    if (!query.position && query.error.empty()) query.error = "Enter a line after the colon";
    if (query.path.empty()) query.error = "Enter a file path before the line number";
    return query;
}

}
