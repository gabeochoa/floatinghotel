#pragma once

#include <algorithm>

namespace review_layout {

enum class Sidebar { Hidden, Collapsed, Expanded, Animating };

inline float sidebar_width(float viewportWidth, float requestedWidth,
                           float minimumWidth, Sidebar state) {
    if (state == Sidebar::Hidden) return 0.f;
    const float limit = state == Sidebar::Collapsed || state == Sidebar::Animating
        ? viewportWidth : std::max(0.f, viewportWidth - 368.f);
    return std::min(std::max(requestedWidth, minimumWidth), limit);
}

}
