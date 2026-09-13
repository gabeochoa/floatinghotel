#pragma once

#include "../ecs/ui_imports.h"
#include "zoom.h"
#include <afterhours/src/plugins/modal.h>

namespace ui {

inline bool render_keyboard_shortcuts(UIContext<InputAction>& ctx, ecs::LayoutComponent& layout) {
    using afterhours::input;
    bool command = input::is_key_down(343) || input::is_key_down(347) || input::is_key_down(341) || input::is_key_down(345);
    bool shift = input::is_key_down(340) || input::is_key_down(344);
    if (command && shift && input::is_key_pressed(47)) layout.shortcutsOpen = true;
    bool active = layout.shortcutsOpen;
    auto& root = ui_imm::getUIRootEntity();
    const float screenWidth = ctx.screen_width / zoom::get();
    const float screenHeight = ctx.screen_height / zoom::get();
    float width = std::min(620.f, screenWidth - 32.f);
    float height = std::min(580.f, screenHeight - 32.f);
    auto modal = afterhours::modal::detail::modal_impl(ctx, mk(root, 595000), layout.shortcutsOpen,
        afterhours::ModalConfig{}.with_size(pixels(width), pixels(height))
            .with_title("Keyboard Shortcuts").with_show_close_button(false)
            .with_backdrop_color({0, 0, 0, 0}));
    if (!modal) return active;
    modal.cmp().absolute_pos_x = (ctx.screen_width - width * zoom::get()) * 0.5f;
    modal.cmp().absolute_pos_y = (ctx.screen_height - height * zoom::get()) * 0.5f;
    div(ctx, mk(root, 595001), ComponentConfig{}
        .with_size(ComponentSize{pixels(screenWidth), pixels(screenHeight)})
        .with_absolute_position().with_custom_background(afterhours::Color{0, 0, 0, 120})
        .with_roundness(0.f).with_render_layer(998));
    auto body = div(ctx, mk(modal.ent(), 1), ComponentConfig{}
        .with_size(ComponentSize{percent(1.f), pixels(height - 180.f)})
        .with_overflow(Overflow::Scroll, Axis::Y).with_flex_direction(FlexDirection::Column)
        .with_no_wrap().with_render_layer(1001).with_debug_name("shortcut_reference"));
    const std::pair<std::string, std::string> bindings[] = {
        {"Cmd+Shift+/", "Open this reference"},
        {"Cmd+P", "Find a file"},
        {"Cmd+F", "Find text in the current diff"},
        {"Cmd+Shift+F", "Search repository contents"},
        {"Enter / Shift+Enter", "Next / previous find match"},
        {"Alt+Left / Alt+Right", "Back / Forward"},
        {"j or n / k", "Next / previous review hunk"},
        {"a / c", "Approve / comment on the hunk"},
        {"Cmd+C", "Copy selected code"},
        {"Cmd+Enter", "Export from the feedback panel"},
        {"Cmd+= / Cmd+-", "Increase / decrease code text size"},
        {"Cmd+0", "Reset code text size"},
        {"Tab / Shift+Tab", "Move keyboard focus"},
        {"Cmd+W", "Close the current document"},
        {"Escape", "Dismiss temporary panels or dialogs"},
    };
    for (size_t i = 0; i < std::size(bindings); ++i) {
        bool narrow = width < 480.f;
        auto row = div(ctx, mk(body.ent(), static_cast<int>(i)), ComponentConfig{}
            .with_size(ComponentSize{percent(1.f), pixels(narrow ? 48.f : 26.f)})
            .with_flex_direction(narrow ? FlexDirection::Column : FlexDirection::Row)
            .with_render_layer(1001));
        div(ctx, mk(row.ent(), 0), ComponentConfig{}.with_label(bindings[i].first)
            .with_size(ComponentSize{narrow ? percent(1.f) : pixels(185.f), pixels(24)})
            .with_font_size(FontSize::Small).with_custom_text_color(theme::TEXT_PRIMARY).with_render_layer(1001));
        div(ctx, mk(row.ent(), 1), ComponentConfig{}.with_label(bindings[i].second)
            .with_size(ComponentSize{narrow ? percent(1.f) : expand(), pixels(24)})
            .with_font_size(FontSize::Small).with_custom_text_color(theme::TEXT_SECONDARY).with_render_layer(1001));
    }
    div(ctx, mk(modal.ent(), 2), ComponentConfig{}
        .with_label("Review keys work in the reader. View menu: collapse or expand the reading panel.")
        .with_size(ComponentSize{percent(1.f), pixels(32)})
        .with_text_overflow(afterhours::ui::TextOverflow::Wrap).with_font_size(FontSize::Small).with_render_layer(1001));
    if (button(ctx, mk(modal.ent(), 3), preset::Button("Close")
        .with_size(ComponentSize{pixels(80), pixels(28)}).with_render_layer(1001)
        .with_debug_name("shortcuts_close"))) layout.shortcutsOpen = false;
    return true;
}

}
