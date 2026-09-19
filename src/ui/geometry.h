#pragma once

#include "../rl.h"
#include <afterhours/src/plugins/ui/systems.h>

namespace ui {

inline RectangleType screen_rect(afterhours::Entity& entity) {
    using namespace afterhours::ui;
    auto rect = entity.get<UIComponent>().rect();
    if (entity.has<HasUIModifiers>()) rect = entity.get<HasUIModifiers>().apply_modifier(rect);
    return detail::apply_scroll_offset(entity, rect);
}

inline RectangleType visible_rect(afterhours::Entity& entity) {
    auto rect = screen_rect(entity);
    const auto [clipped, clip] = afterhours::ui::detail::compute_intersected_clip_rect(entity);
    if (clipped) rect = afterhours::ui::detail::intersect_rects(rect, clip);
    return afterhours::ui::detail::intersect_rects(rect, {0.f, 0.f,
        static_cast<float>(afterhours::graphics::get_screen_width()),
        static_cast<float>(afterhours::graphics::get_screen_height())});
}

struct ReadingViewport : afterhours::BaseComponent {
    float topInset = 0.f;
};

inline RectangleType reading_rect(afterhours::Entity& entity) {
    auto rect = visible_rect(entity);
    const float inset = entity.has<ReadingViewport>() ?
        std::clamp(entity.get<ReadingViewport>().topInset, 0.f, rect.height) : 0.f;
    rect.y += inset;
    rect.height -= inset;
    return rect;
}

}
