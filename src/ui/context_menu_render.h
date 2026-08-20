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

    const float sw = static_cast<float>(afterhours::graphics::get_screen_width());
    const float sh = static_cast<float>(afterhours::graphics::get_screen_height());
    const auto rpx = [sh](float design_px) {
        return afterhours::ui::resolve_to_pixels(h720(design_px), sh);
    };

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
    const float panelW = std::max(widest, rpx(140.0f));

    // Flip rather than clip near an edge, so a right-click on the last row in
    // the window still gets a usable menu.
    float x = state.x;
    float y = state.y;
    if (x + panelW > sw) x = std::max(0.f, sw - panelW);
    if (y + height > sh) y = std::max(0.f, y - height);

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

    float itemY = y + padding;
    int clicked = -1;
    for (int i = 0; i < static_cast<int>(state.items.size()); ++i) {
        const auto& item = state.items[static_cast<size_t>(i)];

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
                ctx.mouse.pos, RectangleType{itemX, itemY, itemW, itemH});

        afterhours::Color textColor = ITEM_TEXT;
        if (!item.enabled)
            textColor = DISABLED_TEXT;
        else if (item.isDestructive)
            textColor = hovered ? ITEM_HOVER_TEXT : DESTRUCTIVE_TEXT;
        else if (hovered)
            textColor = ITEM_HOVER_TEXT;

        button(ctx, mk(uiRoot, 9900 + i),
            ComponentConfig{}
                .with_label("  " + item.label)
                .with_size(ComponentSize{pixels(itemW), pixels(itemH)})
                .with_absolute_position()
                .with_translate(itemX, itemY)
                .with_custom_background(hovered ? ITEM_HOVER_BG : PANEL_BG)
                .with_custom_text_color(textColor)
                .with_font_size(afterhours::ui::FontSize::Medium)
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
                    .with_custom_background(hovered ? ITEM_HOVER_BG : PANEL_BG)
                    .with_custom_text_color(hovered ? ITEM_HOVER_TEXT
                                                    : SHORTCUT_TEXT)
                    .with_font_size(afterhours::ui::FontSize::Medium)
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
        if (hovered) state.hoveredIndex = i;
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
