#pragma once

#include <algorithm>
#include <cstdlib>

#include "../../vendor/afterhours/src/plugins/toast.h"
#include "../ui/context_menu_render.h"
#include "../ui/menu_setup.h"
#include "../ui/zoom.h"
#include "ui_imports.h"
#include "../platform/native_menu.h"

namespace app_state { extern bool testModeEnabled; }

namespace ecs {

// Colors from the mockup spec
namespace menu_colors {
    constexpr afterhours::Color BAR_BG        = {30, 30, 30, 255};       // #1E1E1E (matches WINDOW_BG)
    constexpr afterhours::Color HEADER_TEXT    = {170, 170, 170, 255};   // brighter than secondary so menus don't read disabled
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

    std::vector<menu_setup::Menu> current_menus() const {
        auto menus = menus_;
        if (auto* repo = find_singleton<RepoComponent, ActiveTab>(); repo && repo->reviewWorkspace)
            for (auto& menu : menus)
                if (menu.label == "Repository")
                    for (auto& item : menu.items) item.enabled = false;
        if (auto* layout = find_singleton<LayoutComponent>())
            for (auto& menu : menus)
                for (auto& item : menu.items)
                    if (item.label == "Collapse reading panel" && layout->shelfCollapsed) item.label = "Expand reading panel";
        return menus;
    }

    std::vector<native_menu::Menu> native_menus(const std::vector<menu_setup::Menu>& menus) const {
        std::vector<native_menu::Menu> result;
        auto* layout = find_singleton<LayoutComponent>();
        auto* repo = find_singleton<RepoComponent, ActiveTab>();
        for (size_t m = 0; m < menus.size(); ++m) {
            native_menu::Menu menu{menus[m].label, {}};
            for (size_t i = 0; i < menus[m].items.size(); ++i) {
                const auto& item = menus[m].items[i];
                if (item.label == "Quit") continue;
                bool checked = false;
                if (item.label == "Copy With Location (toggle)") checked = Settings::get().get_copy_with_location();
                if (item.label == "Review Workspace (toggle)") checked = repo && repo->reviewWorkspace;
                if (layout) {
                    if (item.label == "Toggle Sidebar") checked = layout->sidebarVisible;
                    if (item.label == "Toggle Command Log") checked = layout->commandLogVisible;
                    if (item.label == "Inline Diff") checked = layout->diffViewMode == LayoutComponent::DiffViewMode::Inline;
                    if (item.label == "Side-by-Side Diff") checked = layout->diffViewMode == LayoutComponent::DiffViewMode::SideBySide;
                }
                menu.items.push_back({static_cast<native_menu::CommandId>(1 + m * 256 + i), item.label,
                    item.shortcut, item.enabled, checked, item.isSeparator});
            }
            if (!menu.items.empty()) result.push_back(std::move(menu));
        }
        return result;
    }

    void drain_notices(UIContext<InputAction>& ctx, MenuComponent& menu) {
        for (const auto& notice : menu.pendingToasts) {
            switch (notice.kind) {
                case MenuComponent::Notice::Kind::Info: afterhours::toast::send_info(ctx, notice.message); break;
                case MenuComponent::Notice::Kind::Success: afterhours::toast::send_success(ctx, notice.message); break;
                case MenuComponent::Notice::Kind::Error: afterhours::toast::send_error(ctx, notice.message); break;
            }
        }
        menu.pendingToasts.clear();
    }

    // Track header positions for hover-to-switch and dropdown placement
    struct HeaderRect {
        float x, y, width, height;
    };
    std::vector<HeaderRect> headerRects_;

    void for_each_with(Entity& /*ctxEntity*/, UIContext<InputAction>& ctx,
                       float) override {
        auto* menuPtr = find_singleton<MenuComponent>();
        if (!menuPtr) return;
        auto& menu = *menuPtr;

        auto* layoutPtr = find_singleton<LayoutComponent>();
        if (!layoutPtr) return;
        auto& layout = *layoutPtr;

        if (!initialized_) {
            menus_ = menu_setup::createMenuBar();
            initialized_ = true;
            if (!app_state::testModeEnabled || std::getenv("FH_NATIVE_MENUS"))
                native_menu::install("floatinghotel", native_menu::CommandId{0}, native_menus(current_menus()));
        }

        Entity& uiRoot = ui_imm::getUIRootEntity();
        if (native_menu::is_installed()) {
            for (auto command : native_menu::drain_commands()) {
                const auto available = current_menus();
                const auto id = static_cast<std::uint32_t>(command);
                if (id == 0) { afterhours::graphics::request_quit(); continue; }
                const auto menuIndex = (id - 1) / 256;
                const auto itemIndex = (id - 1) % 256;
                if (menuIndex < available.size() && itemIndex < available[menuIndex].items.size()) {
                    const auto& item = available[menuIndex].items[itemIndex];
                    if (item.enabled && item.action) item.action();
                }
            }
            native_menu::refresh(native_menus(current_menus()));
            menu.activeMenuIndex = -1;
            drain_notices(ctx, menu);
            ui::render_context_menu(ctx, uiRoot);
            return;
        }
        float barW = layout.menuBar.width;   // sidebar-width column for item/overflow math
        float barH = layout.menuBar.height;
        float barY = layout.menuBar.y;
        // Background spans the menu-bar rect width (full window when not in
        // sidebar-only mode). Use the layout-resolved width, NOT a direct
        // get_screen_width() — that returns physical px on retina and made the
        // full-width bar mis-size / drop the menu on HiDPI displays.
        float barBgW = barW;

        // Menu bar background (render_layer 10 so it draws above sidebar/toolbar)
        div(ctx, mk(uiRoot, 1000),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(barBgW), pixels(barH)})
                .with_absolute_position()
                .with_translate(0, barY)
                .with_custom_background(theme::SIDEBAR_BG)
                .with_flex_direction(FlexDirection::Row)
                .with_align_items(AlignItems::Center)
                .with_roundness(0.0f)
                .with_render_layer(10)
                .with_debug_name("menu_bar"));

        // Check if any menu is currently open
        bool anyMenuOpen = (menu.activeMenuIndex >= 0);

        // Render menu headers and track positions.
        // Font scales with screen height (h720), so header widths must also
        // scale with height to keep text from overflowing.
        const float scale = ui::zoom::get();
        auto mouse = ctx.mouse.pos;
        mouse.x /= scale;
        mouse.y /= scale;
        auto rpx = [](float design_px) { return design_px; };
        float charW = rpx(10.0f);   // ~10px per char at 720p, 18px font
        float hdrPad = rpx(24.0f);  // padding in screen pixels
                                    // (keep 24: smaller values let all menus fit
                                    //  the sidebar bar, removing the "More"
                                    //  overflow the menu-nav e2e flows depend on)

        // Collapse menus that don't fit the (sidebar-width) bar into a trailing
        // "More" menu, so the whole bar always fits the sidebar. The "More"
        // dropdown lists the overflowed menus' items (with a disabled section
        // label per menu when more than one overflows).
        auto headerWidth = [&](const std::string& label) {
            return static_cast<float>(label.length()) * charW + hdrPad;
        };
        std::vector<menu_setup::Menu> renderMenus;
        auto availableMenus = current_menus();
        {
            float startX = rpx(static_cast<float>(theme::layout::PADDING));
            float total = startX;
            for (auto& m : availableMenus) total += headerWidth(m.label);
            if (total <= barW) {
                renderMenus = availableMenus;
            } else {
                float moreW = headerWidth("More");
                float x = startX;
                std::vector<int> overflow;
                for (int i = 0; i < static_cast<int>(availableMenus.size()); ++i) {
                    float wi = headerWidth(availableMenus[i].label);
                    if (x + wi + moreW <= barW) {
                        renderMenus.push_back(availableMenus[i]);
                        x += wi;
                    } else {
                        for (int j = i; j < static_cast<int>(availableMenus.size()); ++j)
                            overflow.push_back(j);
                        break;
                    }
                }
                menu_setup::Menu more;
                more.label = "More";
                bool multi = overflow.size() > 1;
                for (size_t k = 0; k < overflow.size(); ++k) {
                    const auto& m = availableMenus[overflow[k]];
                    if (multi) {
                        if (k > 0) more.items.push_back(menu_setup::MenuItem::separator());
                        // Disabled section label (enabled=false => no action).
                        more.items.push_back(
                            menu_setup::MenuItem{m.label, "", false, false, nullptr});
                    }
                    for (const auto& it : m.items) more.items.push_back(it);
                }
                renderMenus.push_back(std::move(more));
            }
        }

        headerRects_.clear();
        headerRects_.resize(renderMenus.size());
        float headerX = rpx(static_cast<float>(theme::layout::PADDING));
        bool headerInteracted = false;

        for (int i = 0; i < static_cast<int>(renderMenus.size()); ++i) {
            bool isActive = (menu.activeMenuIndex == i);

            // Header width in screen pixels (scaled with font)
            float headerW = headerWidth(renderMenus[i].label);

            headerRects_[i] = {headerX, barY, headerW, barH};

            // Check mouse hover over this header
            bool mouseOverHeader = afterhours::ui::is_mouse_inside(
                mouse,
                RectangleType{headerX, barY, headerW, barH});

            bool highlighted = isActive || (anyMenuOpen && mouseOverHeader);

            auto headerResult = button(ctx, mk(uiRoot, 1010 + i),
                ComponentConfig{}
                    .with_label(renderMenus[i].label)
                    .with_padding(Padding{.left = pixels(0)})
                    .with_size(ComponentSize{pixels(headerW), pixels(barH)})
                    .with_absolute_position()
                    .with_translate(headerX, barY)
                    .with_custom_background(highlighted ? menu_colors::ACTIVE_BG : menu_colors::BAR_BG)
                    .with_custom_text_color(highlighted ? menu_colors::ACTIVE_TEXT : menu_colors::HEADER_TEXT)
                    .with_font_size(pixels(14))
                    .with_alignment(TextAlignment::Center)
                    .with_justify_content(JustifyContent::Center)
                    .with_align_items(AlignItems::Center)
                    .with_click_activation(ClickActivationMode::Press)
                    .with_roundness(0.0f)
                    .with_render_layer(10)
                    .with_debug_name("menu_header_" + renderMenus[i].label));

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
        if (menu.activeMenuIndex >= 0 && menu.activeMenuIndex < static_cast<int>(renderMenus.size())) {
            int menuIdx = menu.activeMenuIndex;
            const auto& menuDef = renderMenus[menuIdx];

            // Calculate dropdown position (below the header)
            float dropdownX = headerRects_[menuIdx].x;
            float dropdownY = barY + barH;

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
            // Entity IDs 9000+ to ensure dropdown draws above toolbar (5000) and sidebar (2000)
            div(ctx, mk(uiRoot, 9100 + menuIdx),
                ComponentConfig{}
                    .with_size(ComponentSize{pixels(maxWidth), pixels(dropdownHeight)})
                    .with_absolute_position()
                    .with_translate(dropdownX, dropdownY)
                    .with_custom_background(menu_colors::DROPDOWN_BG)
                    .with_border(menu_colors::DROPDOWN_BORDER, h720(1.0f))
                    .with_roundness(0.0f)
                    .with_render_layer(100)
                    .with_debug_name("dropdown_" + menuDef.label));

            // Render each menu item
            float itemY = dropdownY + DROPDOWN_PADDING;

            for (int itemIdx = 0; itemIdx < static_cast<int>(menuDef.items.size()); ++itemIdx) {
                const auto& item = menuDef.items[itemIdx];

                if (item.isSeparator) {
                    // Separator line
                    div(ctx, mk(uiRoot, 9200 + menuIdx * 100 + itemIdx),
                        ComponentConfig{}
                            .with_size(ComponentSize{pixels(maxWidth - rpx(8.0f)), pixels(rpx(1.0f))})
                            .with_absolute_position()
                            .with_translate(dropdownX + rpx(4.0f), itemY + rpx(4.0f))
                            .with_custom_background(menu_colors::SEPARATOR)
                            .with_roundness(0.0f)
                            .with_render_layer(101)
                            .with_debug_name("menu_separator"));
                    itemY += SEPARATOR_HEIGHT;
                } else {
                    // Check hover
                    bool hovered = afterhours::ui::is_mouse_inside(
                        mouse,
                        RectangleType{dropdownX + rpx(2.0f), itemY, maxWidth - rpx(4.0f), ITEM_HEIGHT}) && item.enabled;

                    float itemW = maxWidth - rpx(4.0f);
                    float itemX = dropdownX + rpx(2.0f);

                    // Label button (left-aligned, handles clicks)
                    afterhours::Color labelColor = !item.enabled ? menu_colors::DISABLED_TEXT :
                        hovered ? menu_colors::ITEM_HOVER_TEXT : menu_colors::ITEM_TEXT;

                    auto itemResult = button(ctx, mk(uiRoot, 9500 + menuIdx * 100 + itemIdx),
                        ComponentConfig{}
                            .with_label("  " + item.label)
                            .with_padding(Padding{.left = pixels(0)})
                            .with_size(ComponentSize{pixels(itemW), pixels(ITEM_HEIGHT)})
                            .with_absolute_position()
                            .with_translate(itemX, itemY)
                            .with_custom_background(hovered ? menu_colors::ITEM_HOVER_BG : menu_colors::DROPDOWN_BG)
                            .with_custom_text_color(labelColor)
                            .with_font_size(pixels(14))
                            .with_alignment(TextAlignment::Left)
                            .with_justify_content(JustifyContent::Center)
                            .with_click_activation(ClickActivationMode::Press)
                            .with_roundness(0.0f)
                            .with_render_layer(101)
                            .with_debug_name("menu_item_" + item.label));

                    // Shortcut text (narrow, right-positioned, no overlap with label)
                    if (!item.shortcut.empty()) {
                        float shortcutW = static_cast<float>(item.shortcut.length()) * charW + rpx(16.0f);
                        float shortcutX = itemX + itemW - shortcutW;
                        div(ctx, mk(uiRoot, 9700 + menuIdx * 100 + itemIdx),
                            ComponentConfig{}
                                .with_label(item.shortcut)
                                .with_size(ComponentSize{pixels(shortcutW), pixels(ITEM_HEIGHT)})
                                .with_absolute_position()
                                .with_translate(shortcutX, itemY)
                                .with_custom_background(hovered ? menu_colors::ITEM_HOVER_BG : menu_colors::DROPDOWN_BG)
                                .with_custom_text_color(
                                    hovered ? menu_colors::ITEM_HOVER_TEXT : menu_colors::SHORTCUT_TEXT)
                                .with_font_size(pixels(14))
                                .with_alignment(TextAlignment::Right)
                                .with_padding(Padding{.right = w1280(8.0f)})
                                .with_justify_content(JustifyContent::Center)
                                .with_roundness(0.0f)
                                .with_render_layer(102)
                                .with_debug_name("menu_shortcut_" + item.label));
                    }

                    // Handle item click via direct mouse check (same pattern
                    // as headers — avoids the one-frame-late button result
                    // which gets swallowed by the click consumption below).
                    (void)itemResult;
                    bool itemClicked = hovered && ctx.mouse.just_pressed;
                    if (itemClicked && item.enabled) {
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

        drain_notices(ctx, menu);

        // Close menus on click outside
        if (anyMenuOpen && !headerInteracted && !itemInteracted) {
            if (ctx.mouse.just_pressed) {
                bool clickInMenu = false;

                // Check header rects
                for (int i = 0; i < static_cast<int>(headerRects_.size()); ++i) {
                    auto& r = headerRects_[i];
                    if (afterhours::ui::is_mouse_inside(mouse,
                            RectangleType{r.x, r.y, r.width, r.height})) {
                        clickInMenu = true;
                        break;
                    }
                }

                // Check dropdown rect (using same scaled metrics)
                if (!clickInMenu && menu.activeMenuIndex >= 0 && menu.activeMenuIndex < static_cast<int>(renderMenus.size())) {
                    int menuIdx = menu.activeMenuIndex;
                    float dropdownX = headerRects_[menuIdx].x;
                    float dropdownY = barY + barH;

                    float dropdownHeight = rpx(8.0f); // padding
                    for (const auto& item : renderMenus[menuIdx].items) {
                        dropdownHeight += item.isSeparator ? rpx(9.0f) : rpx(24.0f);
                    }

                    float maxWidth = rpx(180.0f);
                    for (const auto& item : renderMenus[menuIdx].items) {
                        if (item.isSeparator) continue;
                        float totalW = static_cast<float>(item.label.length()) * charW +
                                       (item.shortcut.empty() ? 0.0f : static_cast<float>(item.shortcut.length()) * charW + rpx(32.0f)) + rpx(40.0f);
                        if (totalW > maxWidth) maxWidth = totalW;
                    }

                    if (afterhours::ui::is_mouse_inside(mouse,
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

        // Last thing this system builds, and this system runs last, so the
        // context menu lands above every other overlay.
        ui::render_context_menu(ctx, uiRoot);
    }
};

}  // namespace ecs
