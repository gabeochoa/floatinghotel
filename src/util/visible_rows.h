#pragma once

#include <algorithm>
#include <cmath>
#include <cstddef>
#include <utility>

inline std::pair<size_t, size_t> visible_rows(size_t count, float rowHeight,
    float origin, float scroll, float viewport) {
    if (rowHeight <= 0.f || viewport <= 0.f) return {0, std::min(count, size_t{100})};
    auto clamp = [count](float value) {
        return static_cast<size_t>(std::clamp(value, 0.f, static_cast<float>(count)));
    };
    return {clamp(std::floor((scroll - viewport - origin) / rowHeight)),
            clamp(std::ceil((scroll + viewport * 2.f - origin) / rowHeight))};
}
