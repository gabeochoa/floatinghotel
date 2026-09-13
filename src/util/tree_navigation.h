#pragma once

#include "file_tree.h"
#include <chrono>
#include <cstdint>
#include <optional>
#include <string_view>
#include <unordered_set>

namespace file_tree {

enum class Key { Up, Down, Left, Right, Enter };

struct Move {
    std::string path;
    std::optional<std::string> toggle;
    bool open = false;
    bool keep = false;
};

struct TypeSelectState {
    std::string prefix;
    std::chrono::steady_clock::time_point lastInput{};
};

struct ViewportAnchor {
    std::string path;
    float fraction = 0.f;
};

inline std::string surviving_path(const std::vector<Row>& previous, const std::vector<Row>& current,
                                   const std::string& path) {
    if (path.empty() || current.empty()) return path;
    std::unordered_set<std::string_view> present;
    for (const auto& row : current) present.insert(row.path);
    if (present.contains(path)) return path;
    auto old = std::find_if(previous.begin(), previous.end(), [&](const auto& row) { return row.path == path; });
    if (old != previous.end()) {
        if (old->directory) {
            for (const auto& row : current)
                if (row.directory && row.path.starts_with(path)) return row.path;
        }
        const auto index = static_cast<size_t>(old - previous.begin());
        for (size_t distance = 1; distance < previous.size(); ++distance) {
            if (index + distance < previous.size() && present.contains(previous[index + distance].path))
                return previous[index + distance].path;
            if (distance <= index && present.contains(previous[index - distance].path))
                return previous[index - distance].path;
        }
        return current[std::min(index, current.size() - 1)].path;
    }
    std::string parent;
    for (const auto& row : current)
        if (row.directory && path.starts_with(row.path) && row.path.size() > parent.size()) parent = row.path;
    return parent.empty() ? current.front().path : parent;
}

struct NavigationState {
    std::string context;
    std::string path;
    bool pendingFocus = false;
    bool pendingReveal = false;
    TypeSelectState typing;
    std::optional<ViewportAnchor> viewport;
    std::optional<float> viewportOffset;
    std::optional<std::uint64_t> navigationGeneration;
    std::string revealPath;
    int revealEntity = -1;
};

inline bool reveal_navigation(NavigationState& state, const std::string& context, std::uint64_t generation,
        const std::string& path, const std::vector<std::string>& paths, std::set<std::string>& collapsed) {
    if (state.context != context) state = {context};
    if (state.navigationGeneration == generation) return false;
    state.navigationGeneration = generation;
    state.pendingReveal = false;
    if (state.path != path) state.pendingFocus = false;
    if (path.empty() || std::find(paths.begin(), paths.end(), path) == paths.end()) return false;
    state.path = state.revealPath = path;
    state.pendingReveal = true;
    bool expanded = false;
    for (size_t slash = path.find('/'); slash != std::string::npos; slash = path.find('/', slash + 1))
        expanded |= collapsed.erase(path.substr(0, slash + 1)) != 0;
    return expanded;
}

inline std::optional<Move> navigate(const std::vector<Row>& rows, const std::set<std::string>& collapsed,
                                    const std::string& path, Key key) {
    if (rows.empty()) return {};
    auto current = std::find_if(rows.begin(), rows.end(), [&](const auto& row) { return row.path == path; });
    size_t index = current == rows.end() ? (key == Key::Up ? rows.size() - 1 : 0) : static_cast<size_t>(current - rows.begin());
    const auto previous = index;
    if (current != rows.end()) {
        switch (key) {
            case Key::Up: if (index > 0) --index; break;
            case Key::Down: if (index + 1 < rows.size()) ++index; break;
            case Key::Left:
                if (current->directory && !directory_collapsed(*current, collapsed)) return Move{path, path};
                for (size_t i = index; i > 0; --i)
                    if (rows[i - 1].directory && path.starts_with(rows[i - 1].path)) { index = i - 1; break; }
                break;
            case Key::Right:
                if (current->directory && directory_collapsed(*current, collapsed)) return Move{path, path};
                if (current->directory && index + 1 < rows.size() && rows[index + 1].depth > current->depth) ++index;
                break;
            case Key::Enter:
                if (current->directory) return Move{path, path};
                return Move{path, {}, true, true};
        }
        if (index == previous) return {};
    }
    return Move{rows[index].path, {}, !rows[index].directory, key == Key::Enter};
}

inline std::optional<Move> type_select(const std::vector<Row>& rows, const std::string& path,
        TypeSelectState& state, std::string input, std::chrono::steady_clock::time_point now) {
    if (input.empty() || rows.empty()) return {};
    auto fold = [](std::string_view value) {
        std::string result(value);
        for (auto& c : result) if (c >= 'A' && c <= 'Z') c = static_cast<char>(c + ('a' - 'A'));
        return result;
    };
    input = fold(input);
    if (now < state.lastInput || now - state.lastInput >= std::chrono::milliseconds(700)) state.prefix.clear();
    const bool cycle = state.prefix.empty() || state.prefix == input;
    state.prefix = cycle ? input : state.prefix + input;
    state.lastInput = now;
    const auto current = std::find_if(rows.begin(), rows.end(), [&](const auto& row) { return row.path == path; });
    const size_t start = current == rows.end() ? 0 : (static_cast<size_t>(current - rows.begin()) + (cycle ? 1 : 0)) % rows.size();
    for (size_t offset = 0; offset < rows.size(); ++offset) {
        const auto& row = rows[(start + offset) % rows.size()];
        std::string_view name(row.path);
        if (name.ends_with('/')) name.remove_suffix(1);
        const auto slash = name.find_last_of('/');
        if (slash != std::string_view::npos) name.remove_prefix(slash + 1);
        if (row.directory) name = row.label;
        if (fold(name).starts_with(state.prefix)) return Move{row.path, {}, !row.directory};
    }
    return {};
}

}
