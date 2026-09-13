#pragma once

// Rendering for the context menu, split from context_menu.cpp so that file
// stays free of the UI stack -- tests/unit/test_context_menu.cpp links it on
// its own to exercise the open/close state machine.

#include <algorithm>

#include <afterhours/src/plugins/ui/text_measure.h>

#include "context_menu.h"

namespace ui {

namespace {

// Above the menu bar's dropdowns (100-102): a context menu opened while a
// dropdown is up still has to win.
constexpr int LAYER_PANEL = 110;
constexpr int LAYER_ITEM = 111;

constexpr afterhours::Color PANEL_BG = {45, 45, 45, 255};
constexpr afterhours::Color PANEL_BORDER = {58, 58, 58, 255};
constexpr afterhours::Color ITEM_TEXT = {204, 204, 204, 255};
constexpr afterhours::Color ITEM_HOVER_BG = {4, 57, 94, 255};
constexpr afterhours::Color ITEM_HOVER_TEXT = {255, 255, 255, 255};
constexpr afterhours::Color SHORTCUT_TEXT = {128, 128, 128, 255};
constexpr afterhours::Color SEPARATOR_COL = {58, 58, 58, 255};
constexpr afterhours::Color DISABLED_TEXT = {90, 90, 90, 255};
constexpr afterhours::Color DESTRUCTIVE_TEXT = {235, 94, 94, 255};

}  // namespace

inline void render_context_menu(UIContext<InputAction>& ctx,
                         afterhours::Entity& uiRoot) {
    auto& state = get_context_menu_state();
    if (!state.isOpen || state.items.empty()) return;

    const float scale = ctx.theme.ui_scale;
    const float sw = static_cast<float>(afterhours::graphics::get_screen_width()) / scale;
    const float sh = static_cast<float>(afterhours::graphics::get_screen_height()) / scale;
    const auto rpx = [](float design_px) { return design_px; };
    auto mouse = ctx.mouse.pos;
    mouse.x /= scale;
    mouse.y /= scale;

    const float itemH = rpx(24.0f);
    const float sepH = rpx(9.0f);
    const float padding = rpx(4.0f);

    // Real glyph widths, not chars * a guessed advance: the labels here are
    // proportional and vary in width per file ("Unstage" vs "Copy Path"),
    // and a guess either clips the text or leaves a ragged gap.
    const float fontSize = rpx(14.0f);
    const auto textW = [&](const std::string& t) {
        return afterhours::ui::measure_text_line(
                   t, afterhours::ui::UIComponent::DEFAULT_FONT, fontSize).x;
    };

    float widest = 0.f;
    float height = padding * 2.f;
    for (const auto& item : state.items) {
        if (item.isSeparator) {
            height += sepH;
            continue;
        }
        height += itemH;
        float w = textW(item.label) + rpx(32.0f);
        if (!item.shortcutText.empty())
            w += textW(item.shortcutText) + rpx(24.0f);
        widest = std::max(widest, w);
    }
    const float panelW = std::min(sw, std::max(widest, rpx(140.0f)));
    const float contentHeight = height;
    height = std::min(height, std::max(itemH + padding * 2.f, sh - 8.f));

    // Flip rather than clip near an edge, so a right-click on the last row in
    // the window still gets a usable menu.
    float x = state.x / scale;
    float y = state.y / scale;
    if (x + panelW > sw) x = std::max(0.f, sw - panelW);
    if (y + height > sh) y = std::max(0.f, y - height);
    y = std::clamp(y, 0.f, std::max(0.f, sh - height));
    const auto wheel = afterhours::input::get_mouse_wheel_move_v();
    const bool pointerMoved = ctx.mouse.delta.x != 0.f || ctx.mouse.delta.y != 0.f || wheel.y != 0.f;
    if (afterhours::ui::is_mouse_inside(mouse, RectangleType{x, y, panelW, height}))
        state.scrollOffset -= wheel.y * itemH;
    if (pointerMoved) state.hoveredIndex = -1;
    const int direction = afterhours::input::is_key_pressed(264) ? 1 :
        afterhours::input::is_key_pressed(265) ? -1 : 0;
    if (direction) {
        const int count = static_cast<int>(state.items.size());
        int index = state.hoveredIndex;
        if (index < 0) index = direction > 0 ? -1 : 0;
        for (int attempt = 0; attempt < count; ++attempt) {
            index = (index + direction + count) % count;
            if (state.items[static_cast<size_t>(index)].enabled && !state.items[static_cast<size_t>(index)].isSeparator) {
                state.hoveredIndex = index;
                float top = 0.f;
                for (int row = 0; row < index; ++row) top += state.items[static_cast<size_t>(row)].isSeparator ? sepH : itemH;
                if (top < state.scrollOffset) state.scrollOffset = top;
                if (top + itemH > state.scrollOffset + height - padding * 2.f)
                    state.scrollOffset = top + itemH - height + padding * 2.f;
                break;
            }
        }
    }
    state.scrollOffset = std::clamp(state.scrollOffset, 0.f, std::max(0.f, contentHeight - height));
    if (afterhours::input::is_key_pressed(257) && state.hoveredIndex >= 0) {
        const auto& item = state.items[static_cast<size_t>(state.hoveredIndex)];
        if (item.enabled && !item.isSeparator) {
            auto action = item.action;
            close_context_menu();
            if (action) action();
            return;
        }
    }

    div(ctx, mk(uiRoot, 9800),
        ComponentConfig{}
            .with_size(ComponentSize{pixels(panelW), pixels(height)})
            .with_absolute_position()
            .with_translate(x, y)
            .with_custom_background(PANEL_BG)
            .with_border(PANEL_BORDER, h720(1.0f))
            .with_corner_radius(rpx(4.0f))
            .with_render_layer(LAYER_PANEL)
            .with_debug_name("context_menu"));

    float itemY = y + padding - state.scrollOffset;
    int clicked = -1;
    for (int i = 0; i < static_cast<int>(state.items.size()); ++i) {
        const auto& item = state.items[static_cast<size_t>(i)];

        const float rowHeight = item.isSeparator ? sepH : itemH;
        if (itemY < y + padding - .1f || itemY + rowHeight > y + height - padding + .1f) {
            itemY += rowHeight;
            continue;
        }
        if (item.isSeparator) {
            div(ctx, mk(uiRoot, 9810 + i),
                ComponentConfig{}
                    .with_size(ComponentSize{pixels(panelW - rpx(8.0f)),
                                             pixels(rpx(1.0f))})
                    .with_absolute_position()
                    .with_translate(x + rpx(4.0f), itemY + rpx(4.0f))
                    .with_custom_background(SEPARATOR_COL)
                    .with_roundness(0.0f)
                    .with_render_layer(LAYER_ITEM)
                    .with_debug_name("context_menu_separator"));
            itemY += sepH;
            continue;
        }

        const float itemW = panelW - rpx(4.0f);
        const float itemX = x + rpx(2.0f);
        const bool hovered =
            item.enabled &&
            afterhours::ui::is_mouse_inside(
                mouse, RectangleType{itemX, itemY, itemW, itemH});

        const bool highlighted = state.hoveredIndex == i || (state.hoveredIndex < 0 && hovered);
        afterhours::Color textColor = ITEM_TEXT;
        if (!item.enabled)
            textColor = DISABLED_TEXT;
        else if (item.isDestructive)
            textColor = highlighted ? ITEM_HOVER_TEXT : DESTRUCTIVE_TEXT;
        else if (highlighted)
            textColor = ITEM_HOVER_TEXT;

        button(ctx, mk(uiRoot, 9900 + i),
            ComponentConfig{}
                .with_label("  " + item.label)
                .with_size(ComponentSize{pixels(itemW), pixels(itemH)})
                .with_absolute_position()
                .with_translate(itemX, itemY)
                .with_custom_background(highlighted ? ITEM_HOVER_BG : PANEL_BG)
                .with_custom_hover_bg(highlighted ? ITEM_HOVER_BG : PANEL_BG)
                .with_custom_text_color(textColor)
                .with_font_size(pixels(14))
                .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
                .with_alignment(TextAlignment::Left)
                .with_justify_content(JustifyContent::Center)
                .with_click_activation(
                    afterhours::ui::ClickActivationMode::Press)
                .with_roundness(0.0f)
                .with_render_layer(LAYER_ITEM)
                .with_debug_name("context_menu_item_" + item.label));

        if (!item.shortcutText.empty()) {
            const float shortcutW = textW(item.shortcutText) + rpx(16.0f);
            div(ctx, mk(uiRoot, 9950 + i),
                ComponentConfig{}
                    .with_label(item.shortcutText)
                    .with_size(ComponentSize{pixels(shortcutW), pixels(itemH)})
                    .with_absolute_position()
                    .with_translate(itemX + itemW - shortcutW, itemY)
                    .with_custom_background(highlighted ? ITEM_HOVER_BG : PANEL_BG)
                    .with_custom_text_color(hovered ? ITEM_HOVER_TEXT
                                                    : SHORTCUT_TEXT)
                    .with_font_size(pixels(14))
                    .with_alignment(TextAlignment::Right)
                    .with_padding(Padding{.right = w1280(8.0f)})
                    .with_justify_content(JustifyContent::Center)
                    .with_roundness(0.0f)
                    .with_render_layer(LAYER_ITEM + 1)
                    .with_debug_name("context_menu_shortcut_" + item.label));
        }

        // Direct mouse check, like the menu bar's items: the button result is
        // one frame late and the click-outside close below would eat it first.
        if (hovered && ctx.mouse.just_pressed) clicked = i;
        if (hovered && !direction && pointerMoved) state.hoveredIndex = i;
        itemY += itemH;
    }

    if (clicked >= 0) {
        auto action = state.items[static_cast<size_t>(clicked)].action;
        close_context_menu();
        ctx.mouse.just_pressed = false;
        if (action) action();
        return;
    }

    // A click anywhere else closes it, including on whatever opened it.
    if (ctx.mouse.just_pressed) {
        close_context_menu();
        ctx.mouse.just_pressed = false;
    }
}

} // namespace ui
