#pragma once

#include <algorithm>
#include <set>
#include <string>
#include <vector>

namespace reading {

struct HistorySelection {
    std::set<std::string> hashes;
    std::string anchor;

    void select(const std::vector<std::string>& visible, const std::string& hash, bool extend, bool toggle) {
        const auto target = std::find(visible.begin(), visible.end(), hash);
        if (target == visible.end()) return;
        const auto start = std::find(visible.begin(), visible.end(), anchor);
        if (extend && start != visible.end()) {
            if (!toggle) hashes.clear();
            hashes.insert(std::min(start, target), std::max(start, target) + 1);
        } else {
            if (!toggle) hashes.clear();
            if (toggle && hashes.contains(hash)) hashes.erase(hash);
            else hashes.insert(hash);
            anchor = hash;
        }
    }

    template<class Entries> auto visible(const Entries& entries) const {
        std::vector<typename Entries::value_type> result;
        for (const auto& entry : entries) if (hashes.contains(entry.hash)) result.push_back(entry);
        return result;
    }

    template<class Entries> bool contiguous(const Entries& entries) const {
        bool started = false, ended = false;
        size_t count = 0;
        for (const auto& entry : entries) {
            if (hashes.contains(entry.hash)) {
                if (ended) return false;
                started = true;
                ++count;
            } else if (started) ended = true;
        }
        return count > 0 && count == hashes.size();
    }
};

}
