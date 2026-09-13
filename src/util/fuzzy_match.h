#pragma once

#include <algorithm>
#include <cctype>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include "code_wrap.h"

namespace fuzzy {

using Range = std::pair<size_t, size_t>;

inline size_t filename_start(std::string_view path) {
    const auto slash = path.rfind('/');
    return slash == std::string_view::npos ? 0 : slash + 1;
}

inline std::optional<int> score(std::string_view query, std::string_view path,
                                std::vector<Range>* matches = nullptr) {
    size_t at = 0;
    int value = 0;
    size_t previousEnd = std::string_view::npos;
    for (size_t i = 0; i < path.size() && at < query.size();) {
        const auto end = code_wrap::next_codepoint(path, i);
        const auto queryEnd = code_wrap::next_codepoint(query, at);
        const auto character = path.substr(i, end - i);
        const auto target = query.substr(at, queryEnd - at);
        const bool same = character == target || (character.size() == 1 && target.size() == 1 &&
            std::tolower(static_cast<unsigned char>(character[0])) == std::tolower(static_cast<unsigned char>(target[0])));
        if (same) {
            value += previousEnd == i ? 8 : 1;
            if (i == 0 || path[i - 1] == '/' || path[i - 1] == '_' || path[i - 1] == '-') value += 12;
            if (matches) matches->emplace_back(i, end);
            previousEnd = end;
            at = queryEnd;
        }
        i = end;
    }
    if (at != query.size()) return std::nullopt;
    return value * 100 - static_cast<int>(std::min<size_t>(path.size(), 10000));
}

inline std::vector<Range> matched_ranges(std::string_view query, std::string_view path) {
    std::vector<Range> ranges;
    const auto start = filename_start(path);
    if (score(query, path.substr(start), &ranges)) {
        for (auto& [begin, end] : ranges) { begin += start; end += start; }
    } else {
        ranges.clear();
        if (!score(query, path, &ranges)) ranges.clear();
    }
    return ranges;
}

inline std::vector<std::string> rank(const std::vector<std::string>& paths, const std::string& query,
                                     const std::vector<std::string>& recent = {}) {
    struct Candidate { bool filename; int value; std::string path; };
    std::vector<Candidate> scored;
    std::unordered_map<std::string, int> recency;
    if (query.empty())
        for (size_t i = 0; i < recent.size(); ++i) recency.try_emplace(recent[i], static_cast<int>(recent.size() - i));
    for (const auto& path : paths) {
        if (query.empty()) {
            auto found = recency.find(path);
            scored.push_back({false, found == recency.end() ? 0 : found->second, path});
        } else if (auto value = score(query, std::string_view(path).substr(filename_start(path)))) {
            scored.push_back({true, *value, path});
        } else if (auto value = score(query, path)) scored.push_back({false, *value, path});
    }
    std::sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) {
        if (a.filename != b.filename) return a.filename > b.filename;
        return a.value == b.value ? a.path < b.path : a.value > b.value;
    });
    std::vector<std::string> results;
    for (auto& candidate : scored) results.push_back(std::move(candidate.path));
    return results;
}

}
