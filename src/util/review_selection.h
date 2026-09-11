#pragma once

#include <algorithm>
#include <limits>
#include <optional>
#include <utility>
#include <vector>

namespace review_selection {

struct Range {
    int first = 0;
    int last = 0;
    bool oldSide = false;
};

inline std::optional<Range> range(const std::vector<std::pair<int, int>>& lines) {
    if (lines.empty()) return std::nullopt;
    bool oldOnly = false, newOnly = false;
    for (const auto& [oldLine, newLine] : lines) {
        oldOnly |= newLine == 0;
        newOnly |= oldLine == 0;
    }
    if (oldOnly && newOnly) return std::nullopt;
    Range out{std::numeric_limits<int>::max(), 0, oldOnly};
    for (const auto& [oldLine, newLine] : lines) {
        int number = oldOnly ? oldLine : newLine;
        if (number <= 0) return std::nullopt;
        out.first = std::min(out.first, number);
        out.last = std::max(out.last, number);
    }
    return out;
}

}
