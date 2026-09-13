#pragma once

#include <bitset>
#include <map>
#include <optional>
#include <string>
#include <vector>

namespace ecs {

struct SearchMatch {
    std::string file;
    int line = 0;
    std::string text;
    std::string revision;
    size_t excerptStart = 0;
    std::bitset<512> highlighted;
};

struct SearchMatching {
    bool regularExpression = false;
    bool caseSensitive = true;
    bool wholeWord = false;
};

struct SearchQuery {
    std::string repoPath;
    std::string revision;
    std::string text;
    bool changedOnly = false;
    std::vector<std::string> paths;
    std::vector<std::string> removedPaths;
    std::string beforeRevision;
    SearchMatching matching;
    std::string includeGlob;
    std::string excludeGlob;
};

struct SearchResult {
    std::string revision;
    std::vector<SearchMatch> matches;
    std::string error;
    bool truncated = false;
    size_t capturedBytes = 0;
};

struct SearchPreview {
    SearchMatch match;
    std::vector<std::pair<int, std::string>> lines;
    std::string error;
    bool changedSinceSearch = false;
};

struct SearchFileGroup {
    std::string file;
    std::string revision;
    std::vector<size_t> matches;
    bool collapsed = false;
};

struct SearchResultRow {
    size_t group = 0;
    std::optional<size_t> match;
};

}

namespace search_results {

inline std::vector<ecs::SearchFileGroup> group(const std::vector<ecs::SearchMatch>& matches) {
    std::map<std::pair<std::string, std::string>, size_t> indices;
    std::vector<ecs::SearchFileGroup> groups;
    for (size_t i = 0; i < matches.size(); ++i) {
        const auto& match = matches[i];
        auto [entry, inserted] = indices.try_emplace({match.file, match.revision}, groups.size());
        if (inserted) groups.push_back({match.file, match.revision});
        groups[entry->second].matches.push_back(i);
    }
    return groups;
}

inline std::vector<ecs::SearchResultRow> visible_rows(const std::vector<ecs::SearchFileGroup>& groups) {
    std::vector<ecs::SearchResultRow> rows;
    for (size_t i = 0; i < groups.size(); ++i) {
        rows.push_back({i, {}});
        if (!groups[i].collapsed) for (auto match : groups[i].matches) rows.push_back({i, match});
    }
    return rows;
}

}
