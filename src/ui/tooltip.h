#pragma once

#include <afterhours/src/plugins/ui.h>
#include "../util/wrap_text.h"

namespace ui {

struct TruncatedTooltip : afterhours::BaseComponent {
    std::vector<afterhours::EntityID> labels;
};

inline void set_tooltip(afterhours::Entity& entity, const std::string& text) {
    entity.addComponentIfMissing<afterhours::ui::HasTooltip>().text = text;
    if (entity.has<TruncatedTooltip>()) entity.removeComponent<TruncatedTooltip>();
}

inline void set_truncated_tooltip(afterhours::Entity& entity, const std::string& text,
                                  afterhours::Entity& label) {
    entity.addComponentIfMissing<afterhours::ui::HasTooltip>().text = text;
    entity.addComponentIfMissing<TruncatedTooltip>().labels = {label.id};
}

inline bool tooltip_label_truncated(afterhours::Entity& entity,
                                    afterhours::ui::FontManager& fonts,
                                    const afterhours::ui::Theme& theme, float screenHeight) {
    using namespace afterhours;
    using namespace afterhours::ui;
    if (!entity.has<UIComponent>() || !entity.has<HasLabel>()) return false;
    const auto& component = entity.get<UIComponent>();
    const auto& label = entity.get<HasLabel>();
    if (component.should_hide || entity.has<ShouldHide>() || label.label.empty()) return false;
    const auto rect = component.rect();
    if (rect.width <= 0.f || rect.height <= 0.f) return false;
    const auto previousFont = fonts.active_font;
    if (component.font_name != UIComponent::UNSET_FONT)
        fonts.set_active(fonts.resolve_weighted(component.font_name, component.font_weight));
    const auto inset = resolve_text_inset(theme, label.text_inset);
    const float size = component.font_size_explicitly_set
        ? resolve_to_pixels(component.font_size, screenHeight, component.resolved_scaling_mode, theme.ui_scale) : 0.f;
    const auto position = position_text_ex(fonts, label.label.c_str(), rect, label.alignment,
        inset, size, label.letter_spacing, label.text_overflow, false);
    const float width = measure_text(fonts.get_active_font(), label.label.c_str(),
        position.rect.height, 1.f + label.letter_spacing).x;
    fonts.set_active(previousFont);
    return position.rect.height >= 1.f && width > std::max(0.f, rect.width - 2.f * inset.x);
}

template <class InputAction>
struct FilterTruncatedTooltips : afterhours::System<afterhours::ui::UIContext<InputAction>> {
    void for_each_with(afterhours::Entity&, afterhours::ui::UIContext<InputAction>& context, float) override {
        using namespace afterhours;
        auto* state = EntityHelper::get_singleton_cmp<afterhours::ui::TooltipState>();
        if (!state || !state->is_showing()) return;
        auto owner = afterhours::ui::UICollectionHolder::getEntityForID(state->showing);
        if (!owner.valid() || !owner->has<TruncatedTooltip>()) return;
        auto* fonts = EntityHelper::get_singleton_cmp<afterhours::ui::FontManager>();
        if (fonts) for (auto id : owner->get<TruncatedTooltip>().labels) {
            auto label = afterhours::ui::UICollectionHolder::getEntityForID(id);
            if (label.valid() && tooltip_label_truncated(label.asE(), *fonts, context.theme, context.screen_height)) return;
        }
        state->showing = -1;
        state->text.clear();
    }
};

template <class InputAction>
struct RenderWrappedTooltip : afterhours::System<afterhours::ui::UIContext<InputAction>> {
    void for_each_with(afterhours::Entity&, afterhours::ui::UIContext<InputAction>& context, float) override {
        using namespace afterhours;
        auto* state = EntityHelper::get_singleton_cmp<afterhours::ui::TooltipState>();
        auto* fonts = EntityHelper::get_singleton_cmp<afterhours::ui::FontManager>();
        if (!state || !state->is_showing() || !fonts) return;
        auto owner = afterhours::ui::UICollectionHolder::getEntityForID(state->showing);
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
