#pragma once

#include <algorithm>
#include <map>
#include <set>
#include <string>
#include <vector>

namespace file_tree {

struct Row {
    std::string path;
    size_t sourceIndex = 0;
    size_t depth = 0;
    bool directory = false;
    std::string label;
};

inline bool directory_collapsed(const Row& row, const std::set<std::string>& collapsed) {
    const auto start = row.path.size() - row.label.size() - 1;
    for (auto slash = row.path.find('/', start); slash != std::string::npos; slash = row.path.find('/', slash + 1))
        if (collapsed.contains(row.path.substr(0, slash + 1))) return true;
    return false;
}

inline void toggle_directory(const Row& row, std::set<std::string>& collapsed) {
    if (!directory_collapsed(row, collapsed)) { collapsed.insert(row.path); return; }
    const auto start = row.path.size() - row.label.size() - 1;
    for (auto slash = row.path.find('/', start); slash != std::string::npos; slash = row.path.find('/', slash + 1))
        collapsed.erase(row.path.substr(0, slash + 1));
}

inline void toggle_directory(const std::vector<Row>& rows, const std::string& path, std::set<std::string>& collapsed) {
    toggle_directory(*std::find_if(rows.begin(), rows.end(), [&](const auto& row) { return row.path == path; }), collapsed);
}

inline std::vector<Row> flatten(const std::vector<std::string>& paths,
                                const std::set<std::string>& collapsed) {
    struct Node {
        Row row;
        size_t rank = 0;
        std::vector<std::string> children;
    };
    std::map<std::string, Node> nodes;
    nodes[""] = Node{};
    for (size_t i = 0; i < paths.size(); ++i) {
        const auto& path = paths[i];
        size_t depth = 0;
        std::string parent;
        for (size_t slash = path.find('/'); slash != std::string::npos; slash = path.find('/', slash + 1)) {
            auto directory = path.substr(0, slash + 1);
            auto [it, inserted] = nodes.try_emplace(directory, Node{Row{directory, i, depth, true}, i, {}});
            if (inserted) nodes[parent].children.push_back(directory);
            else it->second.rank = std::min(it->second.rank, i);
            parent = directory;
            ++depth;
        }
        if (!path.empty() && path.back() != '/') {
            bool inserted = nodes.insert_or_assign(path, Node{Row{path, i, depth, false}, i, {}}).second;
            if (inserted) nodes[parent].children.push_back(path);
        }
    }
    std::vector<Row> rows;
    auto append = [&](auto&& self, const std::string& parent, size_t depth) -> void {
        auto children = nodes[parent].children;
        std::stable_sort(children.begin(), children.end(), [&](const auto& left, const auto& right) {
            const auto& a = nodes[left];
            const auto& b = nodes[right];
            if (a.rank != b.rank) return a.rank < b.rank;
            return left < right;
        });
        for (const auto& child : children) {
            const auto* node = &nodes[child];
            while (node->row.directory && node->children.size() == 1 && nodes[node->children.front()].row.directory)
                node = &nodes[node->children.front()];
            auto row = node->row;
            row.depth = depth;
            if (row.directory) row.label = row.path.substr(parent.size(), row.path.size() - parent.size() - 1);
            rows.push_back(row);
            if (row.directory && !directory_collapsed(row, collapsed)) self(self, row.path, depth + 1);
        }
    };
    append(append, "", 0);
    return rows;
}

}
