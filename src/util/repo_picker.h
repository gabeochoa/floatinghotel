#pragma once

#include <algorithm>
#include <filesystem>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "fuzzy_match.h"

namespace repo_picker {

enum class Section { Open, Pinned, Recent, Missing };

inline std::string section_label(Section section) {
    switch (section) {
        case Section::Open: return "Open tabs";
        case Section::Pinned: return "Pinned";
        case Section::Recent: return "Recent";
        case Section::Missing: return "Missing";
    }
    return {};
}

struct TabInfo {
    std::string path;
    std::string branch;
    bool dirty = false;
    bool dirtyKnown = false;
};

// Async resolution for one stored path: whether the folder still exists and
// the main-repository root it belongs to (parent of `git rev-parse
// --git-common-dir`), which groups linked worktrees under their main repo.
struct Resolved {
    std::string path;
    std::string mainPath;
    bool exists = true;
};

struct Entry {
    std::string path;  // representative path to open
    std::string mainPath;
    std::vector<std::string> members;  // representative first
    Section section = Section::Recent;
    std::string branch;
    bool dirty = false;
    bool dirtyKnown = false;
    bool exists = true;
    bool pinned = false;
    bool open = false;
};

inline std::string normalize(const std::string& path) {
    if (path.empty()) return {};
    std::string text = std::filesystem::path(path).lexically_normal().string();
    while (text.size() > 1 && text.back() == '/') text.pop_back();
    return text;
}

inline std::string shorten_home(const std::string& path, const std::string& home) {
    if (!home.empty() && (path == home || path.starts_with(home + "/")))
        return "~" + path.substr(home.size());
    return path;
}

// Build the deduped, worktree-grouped entry list. Section priority is
// Open > Pinned > Recent; a group takes the best section of its members and
// an open tab as representative when one exists. Entries whose folder is
// gone land in Missing (shown only as a relink offer by callers).
inline std::vector<Entry> build(const std::vector<TabInfo>& tabs,
                                const std::vector<std::string>& pinned,
                                const std::vector<std::string>& recent,
                                const std::map<std::string, Resolved>& resolved) {
    struct Group {
        Entry entry;
        int sectionRank = 3;
        size_t order = 0;
    };
    std::vector<Group> groups;
    std::map<std::string, size_t> byMain;
    size_t order = 0;
    auto add = [&](const std::string& path, Section section, const TabInfo* tab) {
        const std::string norm = normalize(path);
        if (norm.empty()) return;
        const auto found = resolved.find(norm);
        const std::string main = found != resolved.end() && !found->second.mainPath.empty()
                                     ? normalize(found->second.mainPath)
                                     : norm;
        const bool exists = found == resolved.end() || found->second.exists;
        const int rank = section == Section::Open ? 0 : section == Section::Pinned ? 1 : 2;
        auto [it, inserted] = byMain.try_emplace(main, groups.size());
        if (inserted) {
            Group group;
            group.entry.path = path;
            group.entry.mainPath = main;
            group.order = order;
            groups.push_back(std::move(group));
        }
        Group& group = groups[it->second];
        if (std::none_of(group.entry.members.begin(), group.entry.members.end(),
                         [&](const std::string& member) { return normalize(member) == norm; }))
            group.entry.members.push_back(path);
        if (section == Section::Pinned) group.entry.pinned = true;
        if (section == Section::Open) {
            group.entry.open = true;
            // An open tab is always the representative: it is the tab Enter
            // will switch to.
            group.entry.path = path;
            if (tab) {
                group.entry.branch = tab->branch;
                group.entry.dirty = tab->dirty;
                group.entry.dirtyKnown = tab->dirtyKnown;
            }
        }
        if (rank < group.sectionRank) {
            group.sectionRank = rank;
            if (section != Section::Open && !group.entry.open) group.entry.path = path;
            group.order = order;
        }
        (void)exists;  // representative existence is resolved after grouping
        ++order;
    };
    for (const auto& tab : tabs) add(tab.path, Section::Open, &tab);
    for (const auto& path : pinned) add(path, Section::Pinned, nullptr);
    for (const auto& path : recent) add(path, Section::Recent, nullptr);
    // Existence: a group exists if its representative exists; open tabs
    // always exist (their repo is loaded).
    for (auto& group : groups) {
        auto& entry = group.entry;
        if (entry.open) entry.exists = true;
        else {
            const auto found = resolved.find(normalize(entry.path));
            entry.exists = found == resolved.end() || found->second.exists;
        }
        entry.section = !entry.exists ? Section::Missing
                        : group.sectionRank == 0 ? Section::Open
                        : group.sectionRank == 1 ? Section::Pinned
                                                 : Section::Recent;
    }
    std::stable_sort(groups.begin(), groups.end(), [](const Group& a, const Group& b) {
        auto rank = [](Section s) { return s == Section::Open ? 0 : s == Section::Pinned ? 1 : s == Section::Recent ? 2 : 3; };
        if (rank(a.entry.section) != rank(b.entry.section)) return rank(a.entry.section) < rank(b.entry.section);
        return a.order < b.order;
    });
    std::vector<Entry> out;
    for (auto& group : groups) out.push_back(std::move(group.entry));
    return out;
}

// Fuzzy-rank on the ~-shortened path. Empty query keeps build order
// (recency within sections); missing entries always sort last.
inline std::vector<size_t> rank(const std::vector<Entry>& entries, const std::string& query,
                                const std::string& home) {
    std::vector<size_t> indices;
    if (query.empty()) {
        for (size_t i = 0; i < entries.size(); ++i) indices.push_back(i);
        return indices;
    }
    struct Scored { size_t index; int score; };
    std::vector<Scored> scored;
    for (size_t i = 0; i < entries.size(); ++i) {
        auto score = fuzzy::score(query, shorten_home(entries[i].path, home));
        if (!score) score = fuzzy::score(query, entries[i].path);
        if (score) scored.push_back({i, *score});
    }
    std::stable_sort(scored.begin(), scored.end(), [&](const Scored& a, const Scored& b) {
        const bool am = entries[a.index].section == Section::Missing;
        const bool bm = entries[b.index].section == Section::Missing;
        if (am != bm) return bm;
        return a.score > b.score;
    });
    for (const auto& item : scored) indices.push_back(item.index);
    return indices;
}

}  // namespace repo_picker
