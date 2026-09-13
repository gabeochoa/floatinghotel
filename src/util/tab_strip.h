#pragma once

#include <algorithm>
#include <vector>

namespace reading {

inline std::vector<float> tab_widths(const std::vector<float>& desired, float viewport) {
    std::vector<float> widths;
    widths.reserve(desired.size());
    const float maximum = std::max(1.f, std::min(360.f, viewport));
    const float minimum = std::min(160.f, maximum);
    for (float width : desired) widths.push_back(std::clamp(width, minimum, maximum));
    return widths;
}

inline float reveal_tab(float offset, float start, float width, float viewport, float content) {
    if (start < offset) offset = start;
    else if (start + width > offset + viewport) offset = start + width - viewport;
    return std::clamp(offset, 0.f, std::max(0.f, content - viewport));
}

}
