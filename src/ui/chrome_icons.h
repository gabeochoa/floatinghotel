#pragma once

#include "../ecs/ui_imports.h"

namespace ui {

enum class ChromeIcon { Commit, Files, Message, Check, ChevronDown, ChevronRight };

inline void chrome_icon(UIContext<InputAction>& ctx, afterhours::ui::imm::EntityParent parent,
                        ChromeIcon kind, afterhours::Color color, const std::string& name) {
    auto icon = div(ctx, parent, ComponentConfig{}
        .with_size(ComponentSize{pixels(16), pixels(16)}).with_debug_name(name));
    auto box = [&](int id, float x, float y, float width, float height) {
        return div(ctx, mk(icon.ent(), id), ComponentConfig{}
            .with_size(ComponentSize{pixels(width), pixels(height)}).with_absolute_position(x, y)
            .with_border(color, pixels(1)).with_roundness(0.f));
    };
    switch (kind) {
        case ChromeIcon::Commit: {
            div(ctx, mk(icon.ent(), 0), ComponentConfig{}
                .with_size(ComponentSize{pixels(8), pixels(8)}).with_absolute_position(4.f, 4.f)
                .with_border(color, pixels(1.5f)).with_rounded_corners(theme::layout::ROUNDED_CORNERS)
                .with_corner_radius(4.f));
            box(1, 0.f, 7.5f, 4.f, 1.f);
            box(2, 12.f, 7.5f, 4.f, 1.f);
            break;
        }
        case ChromeIcon::Files:
            box(0, 2.f, 1.f, 9.f, 11.f);
            box(1, 5.f, 4.f, 9.f, 11.f);
            break;
        case ChromeIcon::Message:
            box(0, 1.f, 2.f, 14.f, 10.f);
            box(1, 3.f, 11.f, 1.f, 4.f);
            box(2, 4.f, 5.f, 8.f, 1.f);
            box(3, 4.f, 8.f, 5.f, 1.f);
            break;
        case ChromeIcon::Check: {
            auto check = div(ctx, mk(icon.ent(), 0), ComponentConfig{}
                .with_size(ComponentSize{pixels(5), pixels(9)}).with_absolute_position(5.f, 2.f)
                .with_border_bottom(color, pixels(1.5f)).with_border_right(color, pixels(1.5f)).with_roundness(0.f));
            check.ent().addComponentIfMissing<afterhours::ui::HasUIModifiers>().rotation = 45.f;
            break;
        }
        case ChromeIcon::ChevronDown:
        case ChromeIcon::ChevronRight: {
            auto chevron = div(ctx, mk(icon.ent(), 0), ComponentConfig{}
                .with_size(ComponentSize{pixels(5), pixels(5)}).with_absolute_position(5.f, 4.f)
                .with_border_bottom(color, pixels(1)).with_border_right(color, pixels(1)).with_roundness(0.f));
            chevron.ent().addComponentIfMissing<afterhours::ui::HasUIModifiers>().rotation =
                kind == ChromeIcon::ChevronDown ? 45.f : -45.f;
            break;
        }
    }
}

inline afterhours::Color segment_selected_color() {
    return theme::current_theme_name == theme::ThemeName::Dark ? afterhours::Color{51, 55, 64, 255} : theme::SELECTED_BG;
}

}
