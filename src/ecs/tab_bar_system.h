#pragma once

#include <filesystem>

#include "../settings.h"
#include "../review_store.h"
#include "../ui/zoom.h"
#include "ui_imports.h"

namespace app_state { extern bool testModeEnabled; }

#ifdef __APPLE__
extern "C" bool metal_choose_folder(char*, int);
#endif

namespace ecs {

// Human-friendly repo name for tab labels: resolves "." / relative / trailing-
// slash paths to the actual folder name (e.g. "." -> "floatinghotel").
inline std::string repo_display_name(const std::string& path) {
    std::error_code ec;
    std::filesystem::path p = std::filesystem::weakly_canonical(path, ec);
    if (ec || p.empty()) p = std::filesystem::absolute(path, ec);
    std::string base = p.filename().string();
    if (base.empty()) base = p.parent_path().filename().string();
    if (base.empty() || base == ".") base = path;
    return base;
}

namespace tab_colors {
    constexpr afterhours::Color STRIP_BG     = {27, 29, 33, 255};
    constexpr afterhours::Color TAB_ACTIVE   = {43, 52, 65, 255};
    constexpr afterhours::Color TAB_INACTIVE = {33, 36, 42, 255};
    constexpr afterhours::Color TAB_HOVER    = {39, 43, 51, 255};
    constexpr afterhours::Color TAB_TEXT     = {156, 162, 175, 255};
    constexpr afterhours::Color TAB_TEXT_ACT = {228, 230, 235, 255};
    constexpr afterhours::Color CLOSE_HOVER  = {80, 86, 98, 255};
    constexpr afterhours::Color BORDER       = {44, 47, 54, 255};
    constexpr afterhours::Color BORDER_ACT   = {70, 82, 100, 255};
    constexpr afterhours::Color DOT          = {110, 118, 130, 255};
    constexpr afterhours::Color PLUS_TEXT    = {156, 162, 175, 255};
}

struct TabBarSystem : afterhours::System<UIContext<InputAction>> {
    void for_each_with(Entity& /*ctxEntity*/, UIContext<InputAction>& ctx,
                       float) override {
        auto* layoutP = find_singleton<LayoutComponent>();
        if (!layoutP) return;
        auto& layout = *layoutP;

        auto* tabStripP = find_singleton<TabStripComponent>();
        if (!tabStripP) return;
        auto& tabStrip = *tabStripP;

        bool cmdDown = afterhours::input::is_key_down(afterhours::keys::LEFT_SUPER) ||
                       afterhours::input::is_key_down(afterhours::keys::RIGHT_SUPER) ||
                       afterhours::input::is_key_down(afterhours::keys::LEFT_CONTROL) ||
                       afterhours::input::is_key_down(afterhours::keys::RIGHT_CONTROL);
        const bool shiftDown = afterhours::input::is_key_down(afterhours::keys::LEFT_SHIFT) ||
                               afterhours::input::is_key_down(afterhours::keys::RIGHT_SHIFT);
        if (cmdDown && !shiftDown && afterhours::input::is_key_pressed(afterhours::keys::T)) {
            create_new_tab(tabStrip, layout);
        }
        // Open Repository: Cmd+O, the File menu, and the welcome screen all
        // funnel into MenuComponent::pendingDialog (that enum existed for
        // this but nothing ever set or consumed it). Consumed here, once.
        if (cmdDown && !shiftDown && afterhours::input::is_key_pressed(afterhours::keys::O)) {
            if (auto* menu = find_singleton<MenuComponent>())
                menu->pendingDialog = MenuComponent::PendingDialog::OpenRepo;
        }
        if (auto* menu = find_singleton<MenuComponent>();
            menu && menu->pendingDialog == MenuComponent::PendingDialog::OpenRepo) {
            menu->pendingDialog = MenuComponent::PendingDialog::None;
            std::string chosen;
            if (app_state::testModeEnabled) {
                if (const char* path = std::getenv("FH_TEST_OPEN_PATH")) chosen = path;
            } else {
#ifdef __APPLE__
                char buffer[4096] = {};
                if (metal_choose_folder(buffer, static_cast<int>(sizeof(buffer)))) chosen = buffer;
#endif
            }
            if (!chosen.empty()) open_repository(chosen, layout);
        }
        if (auto* repo = find_singleton<RepoComponent, ActiveTab>()) {
            if (cmdDown && afterhours::input::is_key_pressed(afterhours::keys::W)) navigation::close(*repo, repo->workspace().active_id());
            if (cmdDown && shiftDown && afterhours::input::is_key_pressed(afterhours::keys::T)) navigation::reopen_closed(*repo);
        }

        if (layout.tabStrip.height <= 0.0f) return;

        Entity& uiRoot = ui_imm::getUIRootEntity();
        float stripW = layout.tabStrip.width;
        float stripH = layout.tabStrip.height;

        auto mouse = ctx.mouse.pos;
        mouse.x /= ui::zoom::get();
        mouse.y /= ui::zoom::get();
        const bool middlePressed = afterhours::input::is_mouse_button_pressed(2);

        // Tab strip background
        div(ctx, mk(uiRoot, 900),
            ComponentConfig{}.with_skip_grid_snap()
                .with_size(ComponentSize{pixels(stripW), pixels(stripH)})
                .with_absolute_position()
                .with_translate(0, layout.tabStrip.y)
                .with_custom_background(tab_colors::STRIP_BG)
                .with_border_bottom(tab_colors::BORDER)
                .with_flex_direction(FlexDirection::Row)
                .with_align_items(AlignItems::Center)
                .with_roundness(0.0f)
                .with_render_layer(6)
                .with_debug_name("tab_strip"));

        // Tabs are inset chips, not full-height slabs: 4px in from the strip
        // top/bottom, 6px gaps, 6px radius — the browser/Slack treatment in
        // the reference screenshots, scaled to this 28px strip. Grouping is
        // carried by each chip's own fill + hairline border, so the old
        // between-tab divider divs are gone.
        const float chipInset = 4.f;
        const float chipGap = 6.f;
        float tabX = chipGap;
        float tabH = stripH - chipInset * 2.f;
        float chipY = layout.tabStrip.y + chipInset;
        const float plusW = std::min(26.f, stripW);
        const float availableTabsW = std::max(0.f, stripW - plusW - chipGap * 2.f -
            chipGap * static_cast<float>(std::max<size_t>(1, tabStrip.tabOrder.size())));
        const float maxTabW = std::min(200.f, availableTabsW /
            static_cast<float>(std::max<size_t>(1, tabStrip.tabOrder.size())));
        const float minTabW = std::min(80.f, maxTabW);

        for (size_t i = 0; i < tabStrip.tabOrder.size(); ++i) {
            auto tabId = tabStrip.tabOrder[i];
            auto tabOpt = EntityHelper::getEntityForID(tabId);
            if (!tabOpt.valid() || !tabOpt->has<Tab>()) continue;
            auto& tabEntity = tabOpt.asE();

            auto& tab = tabEntity.get<Tab>();
            bool isActive = tabEntity.has<ActiveTab>();

            float labelW = static_cast<float>(tab.label.size()) * 7.f + 48.f;
            float tabW = std::clamp(labelW, minTabW, maxTabW);

            bool hovered = afterhours::ui::is_mouse_inside(
                mouse,
                RectangleType{tabX, chipY, tabW, tabH});

            afterhours::Color bg = isActive ? tab_colors::TAB_ACTIVE :
                                   hovered ? tab_colors::TAB_HOVER : tab_colors::TAB_INACTIVE;
            afterhours::Color textCol = isActive ? tab_colors::TAB_TEXT_ACT : tab_colors::TAB_TEXT;

            // Tab chip: rounded, hairline border (brighter on the active
            // chip), identity dot on the left where the references put a
            // favicon, semibold label only when active. The label is a child
            // div, not the button's own label: the button label ignores its
            // left padding here (text landed at +3px with 22px padding, under
            // the dot), while a positioned child lands exactly.
            auto tabDiv = button(ctx, mk(uiRoot, 910 + static_cast<int>(i)),
                ComponentConfig{}.with_skip_grid_snap()
                    .with_size(ComponentSize{pixels(tabW), pixels(tabH)})
                    .with_absolute_position()
                    .with_translate(tabX, chipY)
                    .with_custom_background(bg)
                    .with_click_activation(ClickActivationMode::Press)
                    .with_border(isActive ? tab_colors::BORDER_ACT : tab_colors::BORDER,
                                 pixels(1))
                    .with_rounded_corners(theme::layout::ROUNDED_CORNERS)
                    .with_corner_radius(6.f)
                    .with_render_layer(6)
                    .with_debug_name("tab_" + tab.label));

            div(ctx, mk(uiRoot, 940 + static_cast<int>(i)),
                ComponentConfig{}.with_skip_grid_snap()
                    .with_label(tab.label)
                    .with_size(ComponentSize{pixels(std::max(0.f, tabW - 24.f - (tabW >= 48.f ? 24.f : 0.f))), pixels(tabH)})
                    .with_absolute_position()
                    .with_translate(tabX + 24.f, chipY)
                    .with_transparent_bg()
                    .with_custom_text_color(textCol)
                    .with_font_size(pixels(12))
                    .with_font_weight(isActive ? afterhours::colors::FontWeight::SemiBold
                                               : afterhours::colors::FontWeight::Regular)
                    .with_alignment(TextAlignment::Left)
                    .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
                    .with_justify_content(JustifyContent::Center)
                    .with_align_items(AlignItems::Center)
                    .with_roundness(0.0f)
                    .with_render_layer(7)
                    .with_debug_name("tab_label"));

            if (tabW >= 48.f) {
                div(ctx, mk(uiRoot, 930 + static_cast<int>(i)),
                    ComponentConfig{}.with_skip_grid_snap()
                        .with_size(ComponentSize{pixels(7), pixels(7)})
                        .with_absolute_position()
                        .with_translate(tabX + 9.f, chipY + (tabH - 7.f) * 0.5f)
                        .with_custom_background(isActive ? theme::SELECTED_ACCENT
                                                         : tab_colors::DOT)
                        .with_rounded_corners(theme::layout::ROUNDED_CORNERS)
                        .with_corner_radius(3.5f)
                        .with_render_layer(7)
                        .with_debug_name("tab_dot"));
            }

            // Click to activate tab
            bool clicked = hovered && ctx.mouse.just_pressed;
            (void)tabDiv;
            if (clicked && !isActive) {
                switch_to_tab(tabEntity, layout);
            }
            if (hovered && middlePressed && tabStrip.tabOrder.size() > 1) {
                close_tab(tabStrip, tabId, i, isActive, layout);
                return;
            }

            // Close button (only show when > 1 tab)
            if (tabStrip.tabOrder.size() > 1 && tabW >= 48.f) {
                float closeW = 16.f;
                float closeX = tabX + tabW - closeW - 4.f;
                float closeY = chipY + (tabH - closeW) * 0.5f;

                bool closeHovered = afterhours::ui::is_mouse_inside(
                    mouse,
                    RectangleType{closeX, closeY, closeW, closeW});

                // Quiet close: no filled square at rest (the old version
                // painted the tab bg as a mismatched box on the active chip);
                // the hover pill is the only filled state.
                auto closeBtn = button(ctx, mk(uiRoot, 950 + static_cast<int>(i)),
                    ComponentConfig{}.with_skip_grid_snap()
                        .with_label("\xc3\x97")
                        .with_padding(Padding{.left = pixels(0)})
                        .with_size(ComponentSize{pixels(closeW), pixels(closeW)})
                        .with_absolute_position()
                        .with_translate(closeX, closeY)
                        .with_transparent_bg()
                        .with_custom_hover_bg(tab_colors::CLOSE_HOVER)
                        .with_custom_text_color(closeHovered ? tab_colors::TAB_TEXT_ACT : tab_colors::TAB_TEXT)
                        .with_font_size(pixels(14))
                        .with_alignment(TextAlignment::Center)
                        .with_justify_content(JustifyContent::Center)
                        .with_align_items(AlignItems::Center)
                        .with_click_activation(ClickActivationMode::Press)
                        .with_rounded_corners(theme::layout::ROUNDED_CORNERS)
                        .with_corner_radius(5.f)
                        .with_render_layer(7)
                        .with_debug_name("tab_close"));

                (void)closeBtn;
                if (closeHovered && ctx.mouse.just_pressed) {
                    close_tab(tabStrip, tabId, i, isActive, layout);
                    ctx.mouse.just_pressed = false;
                    return;
                }
            }

            tabX += tabW + chipGap;
        }

        // "+" button: a bare chip-height square with no box at rest — the
        // old full-height version read as another tab and picked up a hard
        // focus square. Hover fill + 6px radius only.
        const float plusX = std::min(tabX, stripW - plusW);
        auto plusBtn = button(ctx, mk(uiRoot, 999),
            ComponentConfig{}.with_skip_grid_snap()
                .with_label("+")
                .with_padding(Padding{.left = pixels(0)})
                .with_size(ComponentSize{pixels(plusW), pixels(tabH)})
                .with_absolute_position()
                .with_translate(plusX, chipY)
                .with_transparent_bg()
                .with_custom_text_color(tab_colors::PLUS_TEXT)
                .with_font_size(pixels(14))
                .with_alignment(TextAlignment::Center)
                .with_justify_content(JustifyContent::Center)
                .with_align_items(AlignItems::Center)
                .with_click_activation(ClickActivationMode::Press)
                .with_custom_hover_bg(tab_colors::TAB_HOVER)
                .with_rounded_corners(theme::layout::ROUNDED_CORNERS)
                .with_corner_radius(6.f)
                .with_render_layer(6)
                .with_debug_name("tab_add"));

        bool plusHovered = afterhours::ui::is_mouse_inside(
            mouse,
            RectangleType{plusX, chipY, plusW, tabH});
        (void)plusBtn;
        if (plusHovered && ctx.mouse.just_pressed) {
            create_new_tab(tabStrip, layout);
        }
    }

    static void switch_to_tab(Entity& newTab, LayoutComponent& layout) {
        auto* oldActiveEnt = find_singleton_entity<Tab, ActiveTab>();

        if (oldActiveEnt) {
            auto& oldTab = *oldActiveEnt;
            // Save current layout state into the outgoing tab
            auto& oldTabComp = oldTab.get<Tab>();
            oldTabComp.sidebarMode = layout.sidebarMode;
            oldTabComp.fileViewMode = layout.fileViewMode;
            oldTabComp.diffViewMode = layout.diffViewMode;
            oldTabComp.sidebarVisible = layout.sidebarVisible;
            if (oldTab.id != newTab.id && oldTab.has<RepoComponent>()) oldTab.get<RepoComponent>().selectionCopy = {};
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
        auto* oldActiveEnt = find_singleton_entity<Tab, ActiveTab>();
        if (oldActiveEnt) {
            auto& oldTab = *oldActiveEnt;
            auto& oldTabComp = oldTab.get<Tab>();
            oldTabComp.sidebarMode = layout.sidebarMode;
            oldTabComp.fileViewMode = layout.fileViewMode;
            oldTabComp.diffViewMode = layout.diffViewMode;
            oldTabComp.sidebarVisible = layout.sidebarVisible;
            if (oldTab.has<RepoComponent>()) oldTab.get<RepoComponent>().selectionCopy = {};
            oldTab.removeComponent<ActiveTab>();
        }

        auto& newEntity = EntityHelper::createEntity();
        newEntity.addComponent<Tab>();
        newEntity.addComponent<ActiveTab>();
        newEntity.addComponent<RepoComponent>();
        newEntity.addComponent<CommitDetailCache>();
        newEntity.addComponent<BranchDialogState>();
        newEntity.addComponent<CommitEditorComponent>();
        newEntity.addComponent<ReviewComponent>();

        tabStrip.tabOrder.push_back(newEntity.id);

        // Reset layout to defaults for new tab
        layout.sidebarMode = LayoutComponent::SidebarMode::Changes;
        layout.fileViewMode = LayoutComponent::FileViewMode::Flat;
        layout.diffViewMode = LayoutComponent::DiffViewMode::Inline;
        layout.sidebarVisible = true;
    }

    static void open_repository(const std::string& path, LayoutComponent& layout) {
        auto* strip = find_singleton<TabStripComponent>();
        if (!strip || path.empty()) return;
        std::error_code error;
        const auto canonical = std::filesystem::weakly_canonical(path, error);
        if (error) return;
        for (const auto id : strip->tabOrder) {
            auto tab = EntityHelper::getEntityForID(id);
            if (!tab.valid() || tab->cleanup || !tab->has<RepoComponent>()) continue;
            const auto& existing = tab->get<RepoComponent>().repoPath;
            if (!existing.empty() && std::filesystem::weakly_canonical(existing, error) == canonical && !error) {
                switch_to_tab(tab.asE(), layout);
                return;
            }
        }
        auto* active = find_singleton<RepoComponent, ActiveTab>();
        if (!active || !active->repoPath.empty()) create_new_tab(*strip, layout);
        active = find_singleton<RepoComponent, ActiveTab>();
        if (!active) return;
        active->repoPath = canonical.string();
        active->refreshRequested = true;
        layout.filePickerOpen = false;
        Settings::get().add_recent_repo(active->repoPath);
        Settings::get().add_open_repo(active->repoPath);
        Settings::get().set_last_active_repo(active->repoPath);
    }

    static void close_tab(TabStripComponent& tabStrip, afterhours::EntityID tabId,
                           size_t index, bool wasActive, LayoutComponent& layout) {
        tabStrip.tabOrder.erase(tabStrip.tabOrder.begin() + static_cast<long>(index));

        auto tabOpt = EntityHelper::getEntityForID(tabId);
        if (tabOpt.valid()) {
            if (!app_state::testModeEnabled && tabOpt->has<RepoComponent>() && tabOpt->has<ReviewComponent>()) {
                const auto& repo = tabOpt->get<RepoComponent>();
                if (!repo.repoPath.empty() && repo.readingSessionPath == repo.repoPath)
                    Settings::get().set_reading_session(repo.repoPath, reading::save_session(repo.workspace()));
                const auto& review = tabOpt->get<ReviewComponent>();
                review_store::save_review(review.storageRepoPath.empty() ? tabOpt->get<RepoComponent>().repoPath : review.storageRepoPath, review);
            }
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
        auto* activeEnt = find_singleton_entity<Tab, ActiveTab>();
        if (!activeEnt) return;

        auto* layout = find_singleton<LayoutComponent>();
        if (!layout) return;

        auto& tab = activeEnt->get<Tab>();

        // Continuously sync layout -> tab so switching away captures latest state
        tab.sidebarMode = layout->sidebarMode;
        tab.fileViewMode = layout->fileViewMode;
        tab.diffViewMode = layout->diffViewMode;
        tab.sidebarVisible = layout->sidebarVisible;

        // Update tab label from repo if available
        if (activeEnt->has<RepoComponent>()) {
            auto& repo = activeEnt->get<RepoComponent>();
            if (!app_state::testModeEnabled) Settings::get().remember_repository_identity(repo.repoPath, repo.headCommitHash);
            if (!repo.repoPath.empty()) {
                std::string base = repo_display_name(repo.repoPath);
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
