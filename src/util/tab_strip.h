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

inline size_t tab_insertion(const std::vector<float>& widths, float pointer) {
    float start = 0.f;
    for (size_t i = 0; i < widths.size(); ++i) {
        if (pointer < start + widths[i] * .5f) return i;
        start += widths[i];
    }
    return widths.size();
}

inline float tab_edge_scroll(float pointer, float viewport, float seconds) {
    const float edge = std::min(24.f, viewport * .25f);
    if (edge <= 0.f) return 0.f;
    const float direction = pointer < edge ? -std::clamp((edge - pointer) / edge, 0.f, 1.f)
        : pointer > viewport - edge ? std::clamp((pointer - viewport + edge) / edge, 0.f, 1.f) : 0.f;
    return direction * 400.f * std::clamp(seconds, 0.f, .05f);
}

}
