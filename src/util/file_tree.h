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
    std::map<std::string, Row> nodes;
    for (size_t i = 0; i < paths.size(); ++i) {
        const auto& path = paths[i];
        size_t depth = 0;
        for (size_t slash = path.find('/'); slash != std::string::npos; slash = path.find('/', slash + 1)) {
            auto directory = path.substr(0, slash + 1);
            nodes.try_emplace(directory, Row{directory, i, depth++, true});
        }
        if (!path.empty() && path.back() != '/') nodes[path] = Row{path, i, depth, false};
    }
    std::vector<Row> rows;
    for (const auto& [path, row] : nodes) {
        bool hidden = false;
        for (size_t slash = path.find('/'); slash != std::string::npos && slash + 1 < path.size(); slash = path.find('/', slash + 1)) {
            if (collapsed.contains(path.substr(0, slash + 1))) { hidden = true; break; }
        }
        if (!hidden) rows.push_back(row);
    }
    return rows;
}

}
