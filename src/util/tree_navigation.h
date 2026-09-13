#pragma once

#include "file_tree.h"
#include <optional>

namespace file_tree {

enum class Key { Up, Down, Left, Right, Enter };

struct Move {
    std::string path;
    std::optional<std::string> toggle;
    bool open = false;
    bool keep = false;
};

struct NavigationState {
    std::string context;
    std::string path;
    bool pendingFocus = false;
    bool pendingReveal = false;
};

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
                if (current->directory && !collapsed.contains(path)) return Move{path, path};
                for (size_t i = index; i > 0; --i)
                    if (rows[i - 1].directory && path.starts_with(rows[i - 1].path)) { index = i - 1; break; }
                break;
            case Key::Right:
                if (current->directory && collapsed.contains(path)) return Move{path, path};
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

}
