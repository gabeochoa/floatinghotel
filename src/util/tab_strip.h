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
    // A few tabs that only just overflow used to leave a clipped sliver of
    // the previous tab (label visible for ~27px) at the strip edge. Shrink
    // them to fit instead, taking the excess off the widest tabs first and
    // never below a readable floor; many tabs still scroll at the floor.
    float total = 0.f;
    for (float width : widths) total += width;
    if (total > viewport && viewport > 0.f) {
        const float floor = std::min(128.f, maximum);
        const float excess = total - viewport;
        float reducible = 0.f;
        for (float width : widths) reducible += std::max(0.f, width - floor);
        if (reducible > 0.f)
            for (float& width : widths) {
                const float above = std::max(0.f, width - floor);
                width -= std::min(above, excess * above / reducible);
            }
    }
    return widths;
}

inline float reveal_tab(float offset, float start, float width, float viewport, float content) {
    if (start < offset) offset = start;
    else if (start + width > offset + viewport) offset = start + width - viewport;
    return std::clamp(offset, 0.f, std::max(0.f, content - viewport));
}

// Reveal the active tab without leaving a sliver of the previous tab at the
// left edge: land on a tab boundary that keeps the active tab fully visible.
// When it is already visible, snap the current offset to the nearest valid
// boundary instead of jumping back to the first one.
inline float reveal_tab_snapped(const std::vector<float>& widths, size_t active, float viewport,
                                float current = 0.f) {
    if (widths.empty() || active >= widths.size() || viewport <= 0.f) return 0.f;
    std::vector<float> starts;
    starts.reserve(widths.size());
    float content = 0.f;
    for (float width : widths) {
        starts.push_back(content);
        content += width;
    }
    if (content <= viewport) return 0.f;
    const float maxOffset = std::max(0.f, content - viewport);
    const float activeStart = starts[active];
    if (widths[active] >= viewport)
        return std::clamp(activeStart, 0.f, maxOffset);
    const float activeEnd = activeStart + widths[active];
    const float required = activeEnd - viewport;
    auto first_at_least_required = [&] {
        for (float start : starts) {
            if (start > activeStart) break;
            if (start >= required) return start;
        }
        return activeStart;
    };

    current = std::clamp(current, 0.f, maxOffset);
    if (activeStart < current) return std::clamp(activeStart, 0.f, maxOffset);
    if (activeEnd > current + viewport)
        return std::clamp(first_at_least_required(), 0.f, maxOffset);

    float result = 0.f;
    for (float start : starts) {
        if (start > current) break;
        result = start;
    }
    if (result < required) result = first_at_least_required();
    return std::clamp(result, 0.f, maxOffset);
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
