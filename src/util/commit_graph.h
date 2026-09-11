#pragma once

#include <algorithm>
#include <map>
#include <sstream>
#include <string>
#include <vector>
#include "../ecs/components.h"

namespace commit_graph {

struct Row {
    size_t lane = 0;
    bool incoming = false;
    std::vector<size_t> continuing;
    std::vector<size_t> parents;
};

struct Graph {
    size_t columns = 1;
    std::map<std::string, Row> rows;
};

inline Graph build(const std::vector<ecs::CommitEntry>& commits) {
    Graph graph;
    std::vector<std::string> lanes;
    auto allocate = [&](const std::string& hash) {
        auto empty = std::find(lanes.begin(), lanes.end(), "");
        if (empty == lanes.end()) { lanes.push_back(hash); return lanes.size() - 1; }
        *empty = hash;
        return static_cast<size_t>(empty - lanes.begin());
    };
    for (const auto& commit : commits) {
        Row row;
        auto current = std::find(lanes.begin(), lanes.end(), commit.hash);
        row.incoming = current != lanes.end();
        row.lane = row.incoming ? static_cast<size_t>(current - lanes.begin()) : allocate(commit.hash);
        for (size_t i = 0; i < lanes.size(); ++i)
            if (i != row.lane && !lanes[i].empty()) row.continuing.push_back(i);
        lanes[row.lane].clear();
        std::istringstream parents(commit.parentHashes);
        std::string parent;
        while (parents >> parent) {
            auto existing = std::find(lanes.begin(), lanes.end(), parent);
            size_t lane;
            if (existing != lanes.end()) lane = static_cast<size_t>(existing - lanes.begin());
            else if (row.parents.empty() && lanes[row.lane].empty()) { lane = row.lane; lanes[lane] = parent; }
            else lane = allocate(parent);
            row.parents.push_back(lane);
        }
        graph.columns = std::max(graph.columns, lanes.size());
        graph.rows.emplace(commit.hash, std::move(row));
        while (!lanes.empty() && lanes.back().empty()) lanes.pop_back();
    }
    return graph;
}

}
