#pragma once

#include <algorithm>

namespace review_layout {

enum class Sidebar { Hidden, Collapsed, Expanded };

inline float window_width(float sidebarWidth, float reviewWidth, bool collapsed) {
    return sidebarWidth + (collapsed ? 0.f : std::max(368.f, reviewWidth));
}

inline float sidebar_width(float viewportWidth, float requestedWidth,
                           float minimumWidth, Sidebar state) {
    if (state == Sidebar::Hidden) return 0.f;
    if (state == Sidebar::Collapsed) return viewportWidth;
    const float limit = std::max(0.f, viewportWidth - 368.f);
    return std::min(std::max(requestedWidth, minimumWidth), limit);
}

}
