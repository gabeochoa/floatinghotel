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
    int endLine = 0;  // last line of a `10-20` range; 0 = single line
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

inline Query line(std::string_view text, int currentLine = 0) {
    Query query;
    if (text.starts_with('+') || text.starts_with('-')) {
        const auto colon = text.find(':');
        const auto delta = positive(text.substr(1, colon == std::string_view::npos ? colon : colon - 1));
        const auto column = colon == std::string_view::npos ? std::optional<int>{1} : positive(text.substr(colon + 1));
        if (!delta || !column) query.error = "Enter a relative move such as +5 or -5, with optional column: +5[:column]";
        else if (currentLine < 1) query.error = "Relative moves need an open file position";
        else {
            const int row = text.starts_with('+') ? currentLine + *delta : currentLine - *delta;
            if (row < 1) query.error = "That move goes before line 1";
            else query.position = Position{row, *column};
        }
        return query;
    }
    const auto colon = text.find(':');
    const auto head = text.substr(0, colon);
    const auto dash = head.find('-');
    const auto row = positive(head.substr(0, dash));
    const auto end = dash == std::string_view::npos ? row : positive(head.substr(dash + 1));
    const auto column = colon == std::string_view::npos ? std::optional<int>{1} : positive(text.substr(colon + 1));
    if (row && end && column && *end >= *row)
        query.position = Position{*row, *column, dash == std::string_view::npos ? 0 : *end};
    else if (!text.empty()) query.error = "Enter a positive line number and optional column: line[:column], a range line-line, or +N / -N";
    return query;
}

inline Query parse_literal(const std::string& text, const std::vector<std::string>& paths) {
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
        const auto dash = middle.find('-');
        const bool range = dash != std::string_view::npos && dash > 0 &&
            positive(middle.substr(0, dash)) && positive(middle.substr(dash + 1));
        if (numeric || range || middle.starts_with('-') || middle.empty()) {
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

// Paths pasted from `git diff` carry an `a/` or `b/` side prefix. Drop it when
// the prefixed path is not itself a file but the remainder is.
inline Query parse(const std::string& text, const std::vector<std::string>& paths) {
    auto query = parse_literal(text, paths);
    if (!(text.starts_with("a/") || text.starts_with("b/"))) return query;
    const auto known = [&](const std::string& path) { return std::find(paths.begin(), paths.end(), path) != paths.end(); };
    if (known(query.path)) return query;
    auto stripped = parse_literal(text.substr(2), paths);
    return known(stripped.path) ? stripped : query;
}

}
