#pragma once

#include "../ecs/ui_imports.h"
#include "bindings_dispatch.h"
#include "zoom.h"
#include <afterhours/src/plugins/modal.h>

namespace ui {

inline bool render_keyboard_shortcuts(UIContext<InputAction>& ctx, ecs::LayoutComponent& layout) {
    if (const auto* binding = bindings::find("Cmd+Shift+/"); binding && bindings::chord_pressed(*binding))
        layout.open_modal(reading::focus::Popup::Shortcuts);
    bool active = layout.modal_is(reading::focus::Popup::Shortcuts);
    auto& root = ui_imm::getUIRootEntity();
    const float screenWidth = ctx.screen_width / zoom::get();
    const float screenHeight = ctx.screen_height / zoom::get();
    float width = std::min(620.f, screenWidth - 32.f);
    float height = std::min(540.f, screenHeight - 64.f);
    bool shortcutsOpen = layout.modal_is(reading::focus::Popup::Shortcuts);
    auto modal = afterhours::modal::detail::modal_impl(ctx, mk(root, 595000), shortcutsOpen,
        afterhours::ModalConfig{}.with_size(pixels(width), pixels(height))
            .with_show_close_button(false)
            .with_backdrop_color({0, 0, 0, 0}));
    if (!shortcutsOpen) layout.close_modal(reading::focus::Popup::Shortcuts);
    if (!modal) return active;
    div(ctx, mk(root, 595001), ComponentConfig{}
        .with_size(ComponentSize{pixels(screenWidth), pixels(screenHeight)})
        .with_absolute_position().with_custom_background(afterhours::Color{0, 0, 0, 120})
        .with_roundness(0.f).with_render_layer(998));
    div(ctx, mk(modal.ent(), 0), ComponentConfig{}
        .with_label("Keyboard Shortcuts")
        .with_size(ComponentSize{percent(1.f), pixels(28)})
        .with_font("ui-bold", pixels(16))
        .with_custom_text_color(theme::TEXT_PRIMARY)
        .with_alignment(TextAlignment::Left)
        .with_padding(Padding{.bottom = pixels(4)})
        .with_render_layer(1001)
        .with_debug_name("shortcuts_title"));
    auto body = div(ctx, mk(modal.ent(), 1), ComponentConfig{}
        .with_size(ComponentSize{percent(1.f), pixels(height - 132.f)})
        .with_overflow(Overflow::Scroll, Axis::Y).with_flex_direction(FlexDirection::Column)
        .with_no_wrap().with_gap(pixels(4))
        .with_padding(Padding{.top = pixels(4), .right = pixels(14), .bottom = pixels(16), .left = pixels(0)})
        .with_render_layer(1001).with_debug_name("shortcut_reference"));
    // Generated from the one binding table (ui/bindings.h), grouped by task.
    struct GroupSpec { const char* title; size_t begin; size_t end; };
    const size_t total = std::size(bindings::table);
    const std::array<GroupSpec, 4> groups = {{
        {"NAVIGATE", 0, std::min<size_t>(12, total)},
        {"FIND & SEARCH", std::min<size_t>(12, total), std::min<size_t>(16, total)},
        {"REVIEW & CODE", std::min<size_t>(16, total), std::min<size_t>(24, total)},
        {"TABS & WINDOW", std::min<size_t>(24, total), total},
    }};
    int nodeId = 10;
    const bool narrow = width < 480.f;
    for (const auto& group : groups) {
        if (group.begin >= group.end) continue;
        div(ctx, mk(body.ent(), nodeId++), ComponentConfig{}
            .with_label(group.title)
            .with_size(ComponentSize{percent(1.f), pixels(24)})
            .with_padding(Padding{.top = pixels(8), .bottom = pixels(2)})
            .with_font("ui-bold", pixels(12))
            .with_custom_text_color(theme::SECTION_HEADER_TEXT)
            .with_alignment(TextAlignment::Left)
            .with_render_layer(1001));
        for (size_t i = group.begin; i < group.end; ++i) {
            const auto& binding = bindings::table[i];
            auto row = div(ctx, mk(body.ent(), nodeId++), ComponentConfig{}
                .with_size(ComponentSize{percent(1.f), pixels(narrow ? 52.f : 28.f)})
                .with_flex_direction(narrow ? FlexDirection::Column : FlexDirection::Row)
                .with_align_items(narrow ? AlignItems::FlexStart : AlignItems::Center)
                .with_gap(pixels(12))
                .with_render_layer(1001));
            auto keyCell = div(ctx, mk(row.ent(), 0), ComponentConfig{}
                .with_size(ComponentSize{narrow ? percent(1.f) : pixels(184.f), pixels(24)})
                .with_flex_direction(FlexDirection::Row).with_align_items(AlignItems::Center)
                .with_render_layer(1001));
            div(ctx, mk(keyCell.ent(), 0), ComponentConfig{}
                .with_label(binding.chord)
                .with_size(ComponentSize{children(), pixels(22)})
                .with_padding(Padding{.top = pixels(2), .right = pixels(8), .bottom = pixels(2), .left = pixels(8)})
                .with_custom_background(theme::INPUT_BG)
                .with_border(theme::BORDER, pixels(1))
                .with_rounded_corners(theme::layout::ROUNDED_CORNERS)
                .with_corner_radius(theme::layout::RADIUS_CONTROL)
                .with_font("mono", pixels(12))
                .with_custom_text_color(theme::TEXT_PRIMARY)
                .with_alignment(TextAlignment::Center)
                .with_render_layer(1001));
            div(ctx, mk(row.ent(), 1), ComponentConfig{}
                .with_label(binding.description)
                .with_size(ComponentSize{narrow ? percent(1.f) : expand(), pixels(24)})
                .with_font_size(pixels(12))
                .with_custom_text_color(theme::TEXT_SECONDARY)
                .with_alignment(TextAlignment::Left)
                .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
                .with_render_layer(1001));
        }
    }
    auto footer = div(ctx, mk(modal.ent(), 2), ComponentConfig{}
        .with_size(ComponentSize{percent(1.f), pixels(36)})
        .with_flex_direction(FlexDirection::Row).with_align_items(AlignItems::Center)
        .with_gap(pixels(12))
        .with_padding(Padding{.top = pixels(8)})
        .with_border_top(theme::BORDER, pixels(1))
        .with_render_layer(1001));
    div(ctx, mk(footer.ent(), 0), ComponentConfig{}
        .with_label("Review keys work in the reader.")
        .with_size(ComponentSize{expand(), pixels(24)})
        .with_font_size(pixels(12))
        .with_custom_text_color(theme::TEXT_TERTIARY)
        .with_alignment(TextAlignment::Left)
        .with_render_layer(1001));
    if (button(ctx, mk(footer.ent(), 1), preset::Button("Close")
        .with_size(ComponentSize{pixels(80), pixels(28)})
        .with_border(theme::BORDER, pixels(1))
        .with_render_layer(1001)
        .with_debug_name("shortcuts_close"))) layout.close_modal(reading::focus::Popup::Shortcuts);
    return true;
}

}
