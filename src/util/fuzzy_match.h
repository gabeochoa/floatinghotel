#pragma once

#include <algorithm>
#include <cctype>
#include <functional>
#include <optional>
#include <string>
#include <stop_token>
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

// Optimal-alignment subsequence score (Zed-style): a memoised search picks
// the alignment maximising boundary and consecutive-run bonuses instead of
// the greedy leftmost one. Boundary bonuses: after `/` 12, `-`/`_`/camelCase
// 10, `.` 8 (plus 1 per matched char, 8 for continuing a run).
inline std::optional<int> score(std::string_view query, std::string_view path,
                                std::vector<Range>* matches = nullptr) {
    struct Char { size_t begin, end; };
    std::vector<Char> q, p;
    for (size_t at = 0; at < query.size();) { const auto e = code_wrap::next_codepoint(query, at); q.push_back({at, e}); at = e; }
    for (size_t at = 0; at < path.size();) { const auto e = code_wrap::next_codepoint(path, at); p.push_back({at, e}); at = e; }
    if (q.empty()) return 0;
    if (p.size() < q.size()) return std::nullopt;
    auto same = [&](size_t i, size_t j) {
        const auto a = path.substr(p[j].begin, p[j].end - p[j].begin);
        const auto b = query.substr(q[i].begin, q[i].end - q[i].begin);
        return a == b || (a.size() == 1 && b.size() == 1 &&
            std::tolower(static_cast<unsigned char>(a[0])) == std::tolower(static_cast<unsigned char>(b[0])));
    };
    auto bonus = [&](size_t j) {
        if (j == 0) return 12;
        const char prev = path[p[j - 1].end - 1];
        if (prev == '/') return 12;
        if (prev == '-' || prev == '_') return 10;
        if (prev == '.') return 8;
        const auto a = path.substr(p[j - 1].begin, p[j - 1].end - p[j - 1].begin);
        const auto b = path.substr(p[j].begin, p[j].end - p[j].begin);
        if (a.size() == 1 && b.size() == 1 && std::islower(static_cast<unsigned char>(a[0])) &&
            std::isupper(static_cast<unsigned char>(b[0]))) return 10;
        return 0;
    };
    constexpr int kNone = -1000000;
    const size_t n = q.size(), m = p.size();
    std::vector<int> memo((n + 1) * (m + 1) * 2, kNone - 1);
    auto cell = [&](size_t i, size_t j, bool run) -> int& { return memo[(i * (m + 1) + j) * 2 + (run ? 1 : 0)]; };
    std::function<int(size_t, size_t, bool)> solve = [&](size_t i, size_t j, bool run) -> int {
        if (i == n) return 0;
        if (j == m) return kNone;
        int& slot = cell(i, j, run);
        if (slot != kNone - 1) return slot;
        int best = solve(i, j + 1, false);
        if (same(i, j)) {
            const int rest = solve(i + 1, j + 1, true);
            if (rest != kNone) best = std::max(best, (run ? 8 : 1) + bonus(j) + rest);
        }
        return slot = best;
    };
    const int value = solve(0, 0, false);
    if (value == kNone) return std::nullopt;
    if (matches) {
        size_t i = 0, j = 0;
        bool run = false;
        while (i < n && j < m) {
            const int skip = solve(i, j + 1, false);
            int take = kNone;
            if (same(i, j)) {
                const int rest = solve(i + 1, j + 1, true);
                if (rest != kNone) take = (run ? 8 : 1) + bonus(j) + rest;
            }
            if (take != kNone && take >= skip) {
                matches->emplace_back(p[j].begin, p[j].end);
                ++i; ++j; run = true;
            } else { ++j; run = false; }
        }
        if (i != n) { matches->clear(); return std::nullopt; }
    }
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

// Shortens `text` to at most `maxChars` codepoints with a middle ellipsis,
// keeping the head and tail. Returns the text unchanged when it fits.
inline std::string elide_middle(std::string_view text, size_t maxChars) {
    if (maxChars < 8) return std::string(text);
    std::vector<size_t> offsets{0};
    for (size_t at = 0; at < text.size();) { at = code_wrap::next_codepoint(text, at); offsets.push_back(at); }
    if (offsets.size() - 1 <= maxChars) return std::string(text);
    const size_t head = maxChars / 2, tail = maxChars - head - 1;
    return std::string(text.substr(0, offsets[head])) + "…" +
           std::string(text.substr(offsets[offsets.size() - 1 - tail]));
}

inline size_t common_dir_prefix(std::string_view a, std::string_view b) {
    size_t i = 0;
    while (i < a.size() && i < b.size() && a[i] == b[i]) ++i;
    while (i > 0 && a[i - 1] != '/') --i;
    return i;
}

inline std::vector<std::string> rank(const std::vector<std::string>& paths, const std::string& query,
                                     const std::vector<std::string>& recent = {},
                                     const std::string& activeDir = {},
                                     std::stop_token stop = {}) {
    struct Candidate { bool exact; int recentRank; bool filename; int value; size_t proximity; std::string path; };
    std::vector<Candidate> scored;
    std::unordered_map<std::string, int> recency;
    for (size_t i = 0; i < recent.size(); ++i) recency.try_emplace(recent[i], static_cast<int>(recent.size() - i));
    size_t checked = 0;
    for (const auto& path : paths) {
        if ((++checked & 1023) == 0 && stop.stop_requested()) return {};
        const auto found = recency.find(path);
        int recentRank = found == recency.end() ? -1 : found->second;
        // Non-empty queries only blend the last 20 opened files.
        if (!query.empty() && recentRank >= 0 && recentRank <= static_cast<int>(recent.size()) - 20) recentRank = -1;
        const size_t proximity = activeDir.empty() ? 0 : common_dir_prefix(path, activeDir);
        if (query.empty()) {
            scored.push_back({false, recentRank, false, recentRank < 0 ? 0 : recentRank, proximity, path});
        } else if (path == query) {
            scored.push_back({true, recentRank, true, 0, proximity, path});
        } else if (auto value = score(query, std::string_view(path).substr(filename_start(path)))) {
            scored.push_back({false, recentRank, true, *value, proximity, path});
        } else if (auto full = score(query, path)) scored.push_back({false, -1, false, *full, proximity, path});
    }
    std::sort(scored.begin(), scored.end(), [](const auto& a, const auto& b) {
        if (a.exact != b.exact) return a.exact > b.exact;
        // Recently opened files that match on filename sit above fuzzy hits,
        // most recent first; the current document (ranked highest) is among
        // them and the picker skips it for initial focus.
        const bool aRecent = a.recentRank >= 0 && a.filename, bRecent = b.recentRank >= 0 && b.filename;
        if (aRecent != bRecent) return aRecent > bRecent;
        if (aRecent && a.recentRank != b.recentRank) return a.recentRank > b.recentRank;
        if (a.filename != b.filename) return a.filename > b.filename;
        if (a.value != b.value) return a.value > b.value;
        // Tie-break: closer to the active document's directory first.
        if (a.proximity != b.proximity) return a.proximity > b.proximity;
        return a.path < b.path;
    });
    std::vector<std::string> results;
    for (auto& candidate : scored) results.push_back(std::move(candidate.path));
    return results;
}

}
