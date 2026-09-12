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
};

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
    auto append = [&](auto&& self, const std::string& parent) -> void {
        auto children = nodes[parent].children;
        std::stable_sort(children.begin(), children.end(), [&](const auto& left, const auto& right) {
            const auto& a = nodes[left];
            const auto& b = nodes[right];
            if (a.rank != b.rank) return a.rank < b.rank;
            return left < right;
        });
        for (const auto& child : children) {
            const auto& node = nodes[child];
            rows.push_back(node.row);
            if (node.row.directory && !collapsed.contains(child)) self(self, child);
        }
    };
    append(append, "");
    return rows;
}

}
