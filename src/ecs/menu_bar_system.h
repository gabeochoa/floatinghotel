#pragma once

#include <algorithm>
#include <string>
#include <vector>

#include "../../vendor/afterhours/src/core/system.h"
#include "../input_mapping.h"
#include "../rl.h"
#include "../ui/menu_setup.h"
#include "../ui/theme.h"
#include "../ui_context.h"
#include "components.h"

namespace ecs {

using afterhours::Entity;
using afterhours::ui::UIContext;
using afterhours::ui::imm::ComponentConfig;
using afterhours::ui::imm::div;
using afterhours::ui::imm::button;
using afterhours::ui::imm::mk;
using afterhours::ui::pixels;
using afterhours::ui::h720;
using afterhours::ui::w1280;
using afterhours::ui::percent;
using afterhours::ui::children;
using afterhours::ui::FlexDirection;
using afterhours::ui::AlignItems;
using afterhours::ui::JustifyContent;
using afterhours::ui::ComponentSize;
using afterhours::ui::Padding;
using afterhours::ui::TextAlignment;
using afterhours::ui::HasClickListener;
using afterhours::ui::ClickActivationMode;
using afterhours::ui::resolve_to_pixels;

// Colors from the mockup spec
namespace menu_colors {
    constexpr afterhours::Color BAR_BG        = {51, 51, 51, 255};       // #333333
    constexpr afterhours::Color HEADER_TEXT    = {204, 204, 204, 255};   // #CCCCCC
    constexpr afterhours::Color ACTIVE_BG      = {45, 45, 45, 255};     // #2D2D2D
    constexpr afterhours::Color ACTIVE_TEXT    = {255, 255, 255, 255};   // #FFFFFF
    constexpr afterhours::Color DROPDOWN_BG    = {45, 45, 45, 255};     // #2D2D2D
    constexpr afterhours::Color DROPDOWN_BORDER = {58, 58, 58, 255};    // #3A3A3A
    constexpr afterhours::Color ITEM_HOVER_BG  = {4, 57, 94, 255};      // Selected blue
    constexpr afterhours::Color ITEM_TEXT       = {204, 204, 204, 255};  // #CCCCCC
    constexpr afterhours::Color ITEM_HOVER_TEXT = {255, 255, 255, 255};  // #FFFFFF
    constexpr afterhours::Color SHORTCUT_TEXT   = {128, 128, 128, 255};  // #808080
    constexpr afterhours::Color SEPARATOR       = {58, 58, 58, 255};    // #3A3A3A
    constexpr afterhours::Color DISABLED_TEXT   = {90, 90, 90, 255};
}

struct MenuBarSystem : afterhours::System<UIContext<InputAction>> {
    std::vector<menu_setup::Menu> menus_;
    bool initialized_ = false;

    // Track header positions for hover-to-switch and dropdown placement
    struct HeaderRect {
        float x, y, width, height;
    };
    std::vector<HeaderRect> headerRects_;

    void for_each_with(Entity& /*ctxEntity*/, UIContext<InputAction>& ctx,
                       float) override {
        auto menuEntities = afterhours::EntityQuery({.force_merge = true})
                                .whereHasComponent<MenuComponent>()
                                .gen();
        if (menuEntities.empty()) return;
        auto& menu = menuEntities[0].get().get<MenuComponent>();

        auto layoutEntities = afterhours::EntityQuery({.force_merge = true})
                                  .whereHasComponent<LayoutComponent>()
                                  .gen();
        if (layoutEntities.empty()) return;
        auto& layout = layoutEntities[0].get().get<LayoutComponent>();

        if (!initialized_) {
            menus_ = menu_setup::createMenuBar();
            initialized_ = true;
        }

        Entity& uiRoot = ui_imm::getUIRootEntity();
        float barW = layout.menuBar.width;
        float barH = layout.menuBar.height;

        // Menu bar background (render_layer 10 so it draws above sidebar/toolbar)
        div(ctx, mk(uiRoot, 1000),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(barW), pixels(barH)})
                .with_absolute_position()
                .with_translate(0, 0)
                .with_custom_background(menu_colors::BAR_BG)
                .with_flex_direction(FlexDirection::Row)
                .with_align_items(AlignItems::Center)
                .with_roundness(0.0f)
                .with_render_layer(5)
                .with_debug_name("menu_bar"));

        // Check if any menu is currently open
        bool anyMenuOpen = (menu.activeMenuIndex >= 0);

        // Render menu headers and track positions.
        // Font scales with screen height (h720), so header widths must also
        // scale with height to keep text from overflowing.
        float screenH = static_cast<float>(afterhours::graphics::get_screen_height());
        auto rpx = [screenH](float design_px) {
            return resolve_to_pixels(h720(design_px), screenH);
        };
        float charW = rpx(10.0f);   // ~10px per char at 720p, 18px font
        float hdrPad = rpx(24.0f);  // padding in screen pixels

        headerRects_.clear();
        headerRects_.resize(menus_.size());
        float headerX = rpx(static_cast<float>(theme::layout::PADDING));
        bool headerInteracted = false;

        for (int i = 0; i < static_cast<int>(menus_.size()); ++i) {
            bool isActive = (menu.activeMenuIndex == i);

            // Header width in screen pixels (scaled with font)
            float headerW = static_cast<float>(menus_[i].label.length()) * charW + hdrPad;

            headerRects_[i] = {headerX, 0.0f, headerW, barH};

            // Check mouse hover over this header
            bool mouseOverHeader = afterhours::ui::is_mouse_inside(
                ctx.mouse.pos,
                RectangleType{headerX, 0.0f, headerW, barH});

            bool highlighted = isActive || (anyMenuOpen && mouseOverHeader);

            auto headerResult = button(ctx, mk(uiRoot, 1010 + i),
                ComponentConfig{}
                    .with_label(menus_[i].label)
                    .with_size(ComponentSize{pixels(headerW), pixels(barH)})
                    .with_absolute_position()
                    .with_translate(headerX, 0.0f)
                    .with_custom_background(highlighted ? menu_colors::ACTIVE_BG : menu_colors::BAR_BG)
                    .with_custom_text_color(highlighted ? menu_colors::ACTIVE_TEXT : menu_colors::HEADER_TEXT)
                    .with_alignment(TextAlignment::Center)
                    .with_justify_content(JustifyContent::Center)
                    .with_align_items(AlignItems::Center)
                    .with_click_activation(ClickActivationMode::Press)
                    .with_roundness(0.0f)
                    .with_render_layer(5)
                    .with_debug_name("menu_header_" + menus_[i].label));

            // Handle header click: toggle this menu
            // Use direct mouse-position check against known header rect.
            // We avoid using headerResult (button's HasClickListener) because
            // it fires one frame late and can cause a double-toggle where the
            // direct check opens the menu and the delayed result closes it.
            (void)headerResult;  // suppress unused warning
            bool clicked = (mouseOverHeader && ctx.mouse.just_pressed);
            if (clicked) {
                if (isActive) {
                    menu.activeMenuIndex = -1;
                } else {
                    menu.activeMenuIndex = i;
                }
                headerInteracted = true;
            }

            // Hover-to-switch when a menu is already open
            if (anyMenuOpen && !isActive && mouseOverHeader) {
                menu.activeMenuIndex = i;
                headerInteracted = true;
            }

            headerX += headerW;
        }

        // Render dropdown for the active menu
        bool itemInteracted = false;
        if (menu.activeMenuIndex >= 0 && menu.activeMenuIndex < static_cast<int>(menus_.size())) {
            int menuIdx = menu.activeMenuIndex;
            const auto& menuDef = menus_[menuIdx];

            // Calculate dropdown position (below the header)
            float dropdownX = headerRects_[menuIdx].x;
            float dropdownY = barH;

            // Calculate dropdown dimensions using resolve_to_pixels
            float ITEM_HEIGHT = rpx(24.0f);
            float SEPARATOR_HEIGHT = rpx(9.0f);
            float DROPDOWN_PADDING = rpx(4.0f);

            float dropdownHeight = DROPDOWN_PADDING * 2.0f;
            for (const auto& item : menuDef.items) {
                dropdownHeight += item.isSeparator ? SEPARATOR_HEIGHT : ITEM_HEIGHT;
            }

            // Calculate dropdown width based on content (scaled with font)
            float maxWidth = rpx(180.0f);
            for (const auto& item : menuDef.items) {
                if (item.isSeparator) continue;
                float labelW = static_cast<float>(item.label.length()) * charW;
                float shortcutW = item.shortcut.empty() ? 0.0f : static_cast<float>(item.shortcut.length()) * charW + rpx(32.0f);
                float totalW = labelW + shortcutW + rpx(40.0f);
                if (totalW > maxWidth) maxWidth = totalW;
            }

            // Dropdown background with border
            div(ctx, mk(uiRoot, 1100 + menuIdx),
                ComponentConfig{}
                    .with_size(ComponentSize{pixels(maxWidth), pixels(dropdownHeight)})
                    .with_absolute_position()
                    .with_translate(dropdownX, dropdownY)
                    .with_custom_background(menu_colors::DROPDOWN_BG)
                    .with_border(menu_colors::DROPDOWN_BORDER, h720(1.0f))
                    .with_roundness(0.0f)
                    .with_render_layer(50)
                    .with_debug_name("dropdown_" + menuDef.label));

            // Render each menu item
            float itemY = dropdownY + DROPDOWN_PADDING;

            for (int itemIdx = 0; itemIdx < static_cast<int>(menuDef.items.size()); ++itemIdx) {
                const auto& item = menuDef.items[itemIdx];

                if (item.isSeparator) {
                    // Separator line
                    div(ctx, mk(uiRoot, 1200 + menuIdx * 100 + itemIdx),
                        ComponentConfig{}
                            .with_size(ComponentSize{pixels(maxWidth - rpx(8.0f)), pixels(rpx(1.0f))})
                            .with_absolute_position()
                            .with_translate(dropdownX + rpx(4.0f), itemY + rpx(4.0f))
                            .with_custom_background(menu_colors::SEPARATOR)
                            .with_roundness(0.0f)
                            .with_render_layer(51)
                            .with_debug_name("menu_separator"));
                    itemY += SEPARATOR_HEIGHT;
                } else {
                    // Build label with shortcut aligned right
                    std::string fullLabel = "  " + item.label;
                    if (!item.shortcut.empty()) {
                        // Pad to push shortcut right
                        size_t currentLen = fullLabel.length();
                        size_t targetLen = 24;
                        if (currentLen < targetLen) {
                            fullLabel += std::string(targetLen - currentLen, ' ');
                        }
                        fullLabel += item.shortcut;
                    }

                    // Check hover
                    bool hovered = afterhours::ui::is_mouse_inside(
                        ctx.mouse.pos,
                        RectangleType{dropdownX + rpx(2.0f), itemY, maxWidth - rpx(4.0f), ITEM_HEIGHT}) && item.enabled;

                    auto itemResult = button(ctx, mk(uiRoot, 1500 + menuIdx * 100 + itemIdx),
                        ComponentConfig{}
                            .with_label(fullLabel)
                            .with_size(ComponentSize{pixels(maxWidth - rpx(4.0f)), pixels(ITEM_HEIGHT)})
                            .with_absolute_position()
                            .with_translate(dropdownX + rpx(2.0f), itemY)
                            .with_custom_background(hovered ? menu_colors::ITEM_HOVER_BG : menu_colors::DROPDOWN_BG)
                            .with_custom_text_color(
                                !item.enabled ? menu_colors::DISABLED_TEXT :
                                hovered ? menu_colors::ITEM_HOVER_TEXT : menu_colors::ITEM_TEXT)
                            .with_alignment(TextAlignment::Left)
                            .with_justify_content(JustifyContent::Center)
                            .with_click_activation(ClickActivationMode::Press)
                            .with_roundness(0.0f)
                            .with_render_layer(51)
                            .with_debug_name("menu_item_" + item.label));

                    // Handle item click
                    if (itemResult && item.enabled) {
                        if (item.action) {
                            item.action();
                        }
                        menu.activeMenuIndex = -1;
                        itemInteracted = true;
                    }

                    itemY += ITEM_HEIGHT;
                }
            }
        }

        // Close menus on click outside
        if (anyMenuOpen && !headerInteracted && !itemInteracted) {
            if (ctx.mouse.just_pressed) {
                bool clickInMenu = false;

                // Check header rects
                for (int i = 0; i < static_cast<int>(headerRects_.size()); ++i) {
                    auto& r = headerRects_[i];
                    if (afterhours::ui::is_mouse_inside(ctx.mouse.pos,
                            RectangleType{r.x, r.y, r.width, r.height})) {
                        clickInMenu = true;
                        break;
                    }
                }

                // Check dropdown rect (using same scaled metrics)
                if (!clickInMenu && menu.activeMenuIndex >= 0 && menu.activeMenuIndex < static_cast<int>(menus_.size())) {
                    int menuIdx = menu.activeMenuIndex;
                    float dropdownX = headerRects_[menuIdx].x;
                    float dropdownY = barH;

                    float dropdownHeight = rpx(8.0f); // padding
                    for (const auto& item : menus_[menuIdx].items) {
                        dropdownHeight += item.isSeparator ? rpx(9.0f) : rpx(24.0f);
                    }

                    float maxWidth = rpx(180.0f);
                    for (const auto& item : menus_[menuIdx].items) {
                        if (item.isSeparator) continue;
                        float totalW = static_cast<float>(item.label.length()) * charW +
                                       (item.shortcut.empty() ? 0.0f : static_cast<float>(item.shortcut.length()) * charW + rpx(32.0f)) + rpx(40.0f);
                        if (totalW > maxWidth) maxWidth = totalW;
                    }

                    if (afterhours::ui::is_mouse_inside(ctx.mouse.pos,
                            RectangleType{dropdownX, dropdownY, maxWidth, dropdownHeight})) {
                        clickInMenu = true;
                    }
                }

                if (!clickInMenu) {
                    menu.activeMenuIndex = -1;
                }
            }
        }

        // When a dropdown is open, consume mouse clicks so they don't pass
        // through to elements underneath
        if (anyMenuOpen) {
            ctx.mouse.just_pressed = false;
            ctx.mouse.just_released = false;
        }
    }
};

}  // namespace ecs
