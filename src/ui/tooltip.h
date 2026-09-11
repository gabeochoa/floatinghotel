#pragma once

#include <afterhours/src/plugins/ui.h>
#include "../util/wrap_text.h"

namespace ui {

inline void set_tooltip(afterhours::Entity& entity, const std::string& text) {
    entity.addComponentIfMissing<afterhours::ui::HasTooltip>().text = text;
}

template <class InputAction>
struct RenderWrappedTooltip : afterhours::System<afterhours::ui::UIContext<InputAction>> {
    void for_each_with(afterhours::Entity&, afterhours::ui::UIContext<InputAction>& context, float) override {
        using namespace afterhours;
        auto* state = EntityHelper::get_singleton_cmp<afterhours::ui::TooltipState>();
        auto* fonts = EntityHelper::get_singleton_cmp<afterhours::ui::FontManager>();
        if (!state || !state->is_showing() || !fonts) return;
        auto owner = EntityHelper::getEntityForID(state->showing);
        if (!owner.valid() || !owner->has<afterhours::ui::UIComponent>()) return;
        auto anchor = afterhours::ui::detail::apply_scroll_offset(
            owner.asE(), owner->get<afterhours::ui::UIComponent>().rect());
        const auto font = fonts->get_active_font();
        auto measure = [&](const std::string& text) { return measure_text(font, text.c_str(), 14.f, 1.f).x; };
        float available = std::max(40.f, std::min(620.f, context.screen_width - 24.f));
        auto lines = wrap_measured_text(state->text, available - 16.f, measure);
        float width = 0.f;
        for (const auto& line : lines) width = std::max(width, measure(line));
        width += 16.f;
        float height = static_cast<float>(lines.size()) * 18.f + 10.f;
        auto placed = afterhours::ui::overlay::place(anchor, width, height, context.screen_width,
            context.screen_height, afterhours::ui::overlay::Placement::Below, 4.f);
        RectangleType box{placed.x, placed.y, width, height};
        draw_rectangle_rounded(box, 0.15f, 6,
            context.theme.from_usage(afterhours::ui::Theme::Usage::Surface, false), std::bitset<4>().set());
        draw_rectangle_rounded_lines(box, 0.15f, 6,
            context.theme.from_usage(afterhours::ui::Theme::Usage::Accent, false), std::bitset<4>().set());
        for (size_t i = 0; i < lines.size(); ++i)
            draw_text_ex(font, lines[i].c_str(), Vector2Type{box.x + 8.f, box.y + 5.f + static_cast<float>(i) * 18.f},
                14.f, 1.f, context.theme.from_usage(afterhours::ui::Theme::Usage::Font, false));
    }
};

}
