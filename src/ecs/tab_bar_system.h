#pragma once

#include <filesystem>
#include <string>

#include "../../vendor/afterhours/src/core/system.h"
#include "../input_mapping.h"
#include "../rl.h"
#include "../settings.h"
#include "../ui/presets.h"
#include "../ui/theme.h"
#include "../ui_context.h"
#include "components.h"

namespace ecs {

using afterhours::Entity;
using afterhours::EntityHelper;
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
using afterhours::ui::Margin;
using afterhours::ui::TextAlignment;
using afterhours::ui::HasClickListener;
using afterhours::ui::ClickActivationMode;
using afterhours::ui::resolve_to_pixels;

namespace tab_colors {
    constexpr afterhours::Color STRIP_BG     = {25, 25, 25, 255};
    constexpr afterhours::Color TAB_ACTIVE   = {45, 45, 45, 255};
    constexpr afterhours::Color TAB_HOVER    = {38, 38, 38, 255};
    constexpr afterhours::Color TAB_TEXT     = {160, 160, 160, 255};
    constexpr afterhours::Color TAB_TEXT_ACT = {230, 230, 230, 255};
    constexpr afterhours::Color CLOSE_HOVER  = {80, 80, 80, 255};
    constexpr afterhours::Color BORDER       = {58, 58, 58, 255};
    constexpr afterhours::Color PLUS_TEXT    = {128, 128, 128, 255};
}

struct TabBarSystem : afterhours::System<UIContext<InputAction>> {
    void for_each_with(Entity& /*ctxEntity*/, UIContext<InputAction>& ctx,
                       float) override {
        auto layoutEntities = afterhours::EntityQuery({.force_merge = true})
                                  .whereHasComponent<LayoutComponent>()
                                  .gen();
        if (layoutEntities.empty()) return;
        auto& layout = layoutEntities[0].get().get<LayoutComponent>();

        auto tabStripEntities = afterhours::EntityQuery({.force_merge = true})
                                    .whereHasComponent<TabStripComponent>()
                                    .gen();
        if (tabStripEntities.empty()) return;
        auto& tabStrip = tabStripEntities[0].get().get<TabStripComponent>();

        // Handle Cmd+T (new tab) and Cmd+W (close tab)
        bool cmdDown = afterhours::graphics::is_key_down(343); // LEFT_SUPER
        if (cmdDown && afterhours::graphics::is_key_pressed(84)) { // T
            create_new_tab(tabStrip, layout);
        }
        if (cmdDown && afterhours::graphics::is_key_pressed(87)) { // W
            if (tabStrip.tabOrder.size() > 1) {
                for (size_t i = 0; i < tabStrip.tabOrder.size(); ++i) {
                    auto opt = EntityHelper::getEntityForID(tabStrip.tabOrder[i]);
                    if (opt.valid() && opt->has<ActiveTab>()) {
                        close_tab(tabStrip, tabStrip.tabOrder[i], i, true, layout);
                        break;
                    }
                }
            }
        }

        if (layout.tabStrip.height <= 0.0f) return;

        Entity& uiRoot = ui_imm::getUIRootEntity();
        float stripW = layout.tabStrip.width;
        float stripH = layout.tabStrip.height;

        float screenH = static_cast<float>(afterhours::graphics::get_screen_height());
        auto rpx = [screenH](float design_px) {
            return resolve_to_pixels(h720(design_px), screenH);
        };

        // Tab strip background
        div(ctx, mk(uiRoot, 900),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(stripW), pixels(stripH)})
                .with_absolute_position()
                .with_translate(0, 0)
                .with_custom_background(tab_colors::STRIP_BG)
                .with_border_bottom(tab_colors::BORDER)
                .with_flex_direction(FlexDirection::Row)
                .with_align_items(AlignItems::Center)
                .with_roundness(0.0f)
                .with_render_layer(6)
                .with_debug_name("tab_strip"));

        float tabX = 0.0f;
        float tabH = stripH;
        float minTabW = rpx(80.0f);
        float maxTabW = rpx(200.0f);

        for (size_t i = 0; i < tabStrip.tabOrder.size(); ++i) {
            auto tabId = tabStrip.tabOrder[i];
            auto tabOpt = EntityHelper::getEntityForID(tabId);
            if (!tabOpt.valid() || !tabOpt->has<Tab>()) continue;
            auto& tabEntity = tabOpt.asE();

            auto& tab = tabEntity.get<Tab>();
            bool isActive = tabEntity.has<ActiveTab>();

            float labelW = static_cast<float>(tab.label.size()) * rpx(7.0f) + rpx(40.0f);
            float tabW = std::clamp(labelW, minTabW, maxTabW);

            bool hovered = afterhours::ui::is_mouse_inside(
                ctx.mouse.pos,
                RectangleType{tabX, 0.0f, tabW, tabH});

            afterhours::Color bg = isActive ? tab_colors::TAB_ACTIVE :
                                   hovered ? tab_colors::TAB_HOVER : tab_colors::STRIP_BG;
            afterhours::Color textCol = isActive ? tab_colors::TAB_TEXT_ACT : tab_colors::TAB_TEXT;

            // Tab background
            auto tabDiv = button(ctx, mk(uiRoot, 910 + static_cast<int>(i)),
                ComponentConfig{}
                    .with_label(tab.label)
                    .with_size(ComponentSize{pixels(tabW), pixels(tabH)})
                    .with_absolute_position()
                    .with_translate(tabX, 0.0f)
                    .with_custom_background(bg)
                    .with_custom_text_color(textCol)
                    .with_font_size(afterhours::ui::FontSize::Medium)
                    .with_alignment(TextAlignment::Left)
                    .with_padding(Padding{.left = pixels(rpx(10.0f)), .right = pixels(rpx(24.0f))})
                    .with_justify_content(JustifyContent::Center)
                    .with_align_items(AlignItems::Center)
                    .with_click_activation(ClickActivationMode::Press)
                    .with_roundness(0.0f)
                    .with_render_layer(6)
                    .with_debug_name("tab_" + tab.label));

            // Click to activate tab
            bool clicked = hovered && ctx.mouse.just_pressed;
            (void)tabDiv;
            if (clicked && !isActive) {
                switch_to_tab(tabEntity, layout);
            }

            // Close button (only show when > 1 tab)
            if (tabStrip.tabOrder.size() > 1) {
                float closeW = rpx(16.0f);
                float closeX = tabX + tabW - closeW - rpx(4.0f);
                float closeY = (tabH - closeW) * 0.5f;

                bool closeHovered = afterhours::ui::is_mouse_inside(
                    ctx.mouse.pos,
                    RectangleType{closeX, closeY, closeW, closeW});

                auto closeBtn = button(ctx, mk(uiRoot, 950 + static_cast<int>(i)),
                    ComponentConfig{}
                        .with_label("\xc3\x97")
                        .with_size(ComponentSize{pixels(closeW), pixels(closeW)})
                        .with_absolute_position()
                        .with_translate(closeX, closeY)
                        .with_custom_background(closeHovered ? tab_colors::CLOSE_HOVER : bg)
                        .with_custom_text_color(closeHovered ? tab_colors::TAB_TEXT_ACT : tab_colors::TAB_TEXT)
                        .with_font_size(afterhours::ui::h720(16))
                        .with_alignment(TextAlignment::Center)
                        .with_justify_content(JustifyContent::Center)
                        .with_align_items(AlignItems::Center)
                        .with_click_activation(ClickActivationMode::Press)
                        .with_roundness(rpx(2.0f))
                        .with_render_layer(7)
                        .with_debug_name("tab_close"));

                (void)closeBtn;
                if (closeHovered && ctx.mouse.just_pressed) {
                    close_tab(tabStrip, tabId, i, isActive, layout);
                    ctx.mouse.just_pressed = false;
                    return;
                }
            }

            // Right border between tabs
            if (i < tabStrip.tabOrder.size() - 1) {
                div(ctx, mk(uiRoot, 970 + static_cast<int>(i)),
                    ComponentConfig{}
                        .with_size(ComponentSize{pixels(1), pixels(tabH * 0.5f)})
                        .with_absolute_position()
                        .with_translate(tabX + tabW, tabH * 0.25f)
                        .with_custom_background(tab_colors::BORDER)
                        .with_roundness(0.0f)
                        .with_render_layer(6)
                        .with_debug_name("tab_divider"));
            }

            tabX += tabW;
        }

        // "+" button to add new tab
        float plusW = rpx(28.0f);
        auto plusBtn = button(ctx, mk(uiRoot, 999),
            ComponentConfig{}
                .with_label("+")
                .with_size(ComponentSize{pixels(plusW), pixels(tabH)})
                .with_absolute_position()
                .with_translate(tabX + rpx(2.0f), 0.0f)
                .with_custom_background(tab_colors::STRIP_BG)
                .with_custom_text_color(tab_colors::PLUS_TEXT)
                .with_font_size(afterhours::ui::FontSize::Large)
                .with_alignment(TextAlignment::Center)
                .with_justify_content(JustifyContent::Center)
                .with_align_items(AlignItems::Center)
                .with_click_activation(ClickActivationMode::Press)
                .with_custom_hover_bg(tab_colors::TAB_HOVER)
                .with_roundness(0.0f)
                .with_render_layer(6)
                .with_debug_name("tab_add"));

        bool plusHovered = afterhours::ui::is_mouse_inside(
            ctx.mouse.pos,
            RectangleType{tabX + rpx(2.0f), 0.0f, plusW, tabH});
        (void)plusBtn;
        if (plusHovered && ctx.mouse.just_pressed) {
            create_new_tab(tabStrip, layout);
        }
    }

    static void switch_to_tab(Entity& newTab, LayoutComponent& layout) {
        auto activeQ = afterhours::EntityQuery({.force_merge = true})
            .whereHasComponent<Tab>()
            .whereHasComponent<ActiveTab>().gen();

        if (!activeQ.empty()) {
            auto& oldTab = activeQ[0].get();
            // Save current layout state into the outgoing tab
            auto& oldTabComp = oldTab.get<Tab>();
            oldTabComp.sidebarMode = layout.sidebarMode;
            oldTabComp.fileViewMode = layout.fileViewMode;
            oldTabComp.diffViewMode = layout.diffViewMode;
            oldTabComp.sidebarVisible = layout.sidebarVisible;
            oldTab.removeComponent<ActiveTab>();
        }

        newTab.addComponent<ActiveTab>();

        // Update last active repo in settings
        if (newTab.has<RepoComponent>()) {
            auto& repo = newTab.get<RepoComponent>();
            if (!repo.repoPath.empty()) {
                Settings::get().set_last_active_repo(repo.repoPath);
            }
        }

        // Load incoming tab state into layout
        auto& tab = newTab.get<Tab>();
        layout.sidebarMode = tab.sidebarMode;
        layout.fileViewMode = tab.fileViewMode;
        layout.diffViewMode = tab.diffViewMode;
        layout.sidebarVisible = tab.sidebarVisible;
    }

    static void create_new_tab(TabStripComponent& tabStrip, LayoutComponent& layout) {
        // Save current tab state
        auto activeQ = afterhours::EntityQuery({.force_merge = true})
            .whereHasComponent<Tab>()
            .whereHasComponent<ActiveTab>().gen();
        if (!activeQ.empty()) {
            auto& oldTab = activeQ[0].get();
            auto& oldTabComp = oldTab.get<Tab>();
            oldTabComp.sidebarMode = layout.sidebarMode;
            oldTabComp.fileViewMode = layout.fileViewMode;
            oldTabComp.diffViewMode = layout.diffViewMode;
            oldTabComp.sidebarVisible = layout.sidebarVisible;
            oldTab.removeComponent<ActiveTab>();
        }

        auto& newEntity = EntityHelper::createEntity();
        newEntity.addComponent<Tab>();
        newEntity.addComponent<ActiveTab>();
        newEntity.addComponent<RepoComponent>();
        newEntity.addComponent<CommitEditorComponent>();

        tabStrip.tabOrder.push_back(newEntity.id);

        // Reset layout to defaults for new tab
        layout.sidebarMode = LayoutComponent::SidebarMode::Changes;
        layout.fileViewMode = LayoutComponent::FileViewMode::Flat;
        layout.diffViewMode = LayoutComponent::DiffViewMode::Inline;
        layout.sidebarVisible = true;
    }

    static void close_tab(TabStripComponent& tabStrip, afterhours::EntityID tabId,
                           size_t index, bool wasActive, LayoutComponent& layout) {
        tabStrip.tabOrder.erase(tabStrip.tabOrder.begin() + static_cast<long>(index));

        auto tabOpt = EntityHelper::getEntityForID(tabId);
        if (tabOpt.valid()) {
            tabOpt.asE().cleanup = true;
        }

        if (wasActive && !tabStrip.tabOrder.empty()) {
            size_t newIndex = (index < tabStrip.tabOrder.size()) ? index : tabStrip.tabOrder.size() - 1;
            auto newId = tabStrip.tabOrder[newIndex];
            auto newOpt = EntityHelper::getEntityForID(newId);
            if (newOpt.valid() && newOpt->has<Tab>()) {
                switch_to_tab(newOpt.asE(), layout);
            }
        }
    }
};

// TabSyncSystem: Keeps the active Tab component in sync with LayoutComponent
// view modes. Runs early each frame to capture changes from menu/toolbar actions.
struct TabSyncSystem : afterhours::System<> {
    void once(float) override {
        auto activeQ = afterhours::EntityQuery({.force_merge = true})
            .whereHasComponent<Tab>()
            .whereHasComponent<ActiveTab>().gen();
        if (activeQ.empty()) return;

        auto layoutQ = afterhours::EntityQuery({.force_merge = true})
            .whereHasComponent<LayoutComponent>().gen();
        if (layoutQ.empty()) return;

        auto& tab = activeQ[0].get().get<Tab>();
        auto& layout = layoutQ[0].get().get<LayoutComponent>();

        // Continuously sync layout -> tab so switching away captures latest state
        tab.sidebarMode = layout.sidebarMode;
        tab.fileViewMode = layout.fileViewMode;
        tab.diffViewMode = layout.diffViewMode;
        tab.sidebarVisible = layout.sidebarVisible;

        // Update tab label from repo if available
        if (activeQ[0].get().has<RepoComponent>()) {
            auto& repo = activeQ[0].get().get<RepoComponent>();
            if (!repo.repoPath.empty()) {
                std::filesystem::path p(repo.repoPath);
                std::string base = p.filename().string();
                if (!repo.currentBranch.empty()) {
                    tab.label = base + " (" + repo.currentBranch + ")";
                } else {
                    tab.label = base;
                }
            }
        }
    }
};

}  // namespace ecs
