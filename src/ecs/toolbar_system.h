#pragma once

#include "../git/git_commands.h"
#include "../git/git_runner.h"
#include "ui_imports.h"

namespace ecs {

// ToolbarSystem: Renders action buttons.
// When sidebar is visible, renders as a compact strip inside the sidebar column.
// When sidebar is hidden, renders as a full-width toolbar below the menu bar.
struct ToolbarSystem : afterhours::System<UIContext<InputAction>> {
    void for_each_with(Entity& /*ctxEntity*/, UIContext<InputAction>& ctx,
                       float) override {
        auto* layoutPtr = find_singleton<LayoutComponent>();
        if (!layoutPtr) return;
        auto& layout = *layoutPtr;

        auto* repo = find_singleton<RepoComponent, ActiveTab>();

        bool hasRepo = (repo != nullptr);
        bool hasUnstaged = false;
        bool hasStaged = false;
        std::string branchName = "main";

        if (repo) {
            hasUnstaged = !repo->unstagedFiles.empty() || !repo->untrackedFiles.empty();
            hasStaged = !repo->stagedFiles.empty();
            if (!repo->currentBranch.empty()) {
                branchName = repo->currentBranch;
            }
        }

        Entity& uiRoot = ui_imm::getUIRootEntity();
        float toolbarX = layout.toolbar.x;
        float w = layout.toolbar.width;
        float h = layout.toolbar.height;
        float toolbarY = layout.toolbar.y;

        bool inSidebar = layout.sidebarVisible;

        // Outer chrome container (absolute positioned)
        auto topChrome = div(ctx, mk(uiRoot, 5000),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(w), pixels(h)})
                .with_absolute_position()
                .with_translate(toolbarX, toolbarY)
                .with_custom_background(theme::TOOLBAR_BG)
                .with_flex_direction(FlexDirection::Column)
                .with_roundness(0.0f)
                .with_render_layer(1)
                .with_debug_name("top_chrome"));

        if (inSidebar) {
            render_sidebar_toolbar(ctx, topChrome, w, h,
                                   repo, hasRepo, hasUnstaged, hasStaged);
        } else {
            render_fullwidth_toolbar(ctx, topChrome, w, h,
                                     repo, hasRepo, hasUnstaged, hasStaged,
                                     branchName);
        }
    }

private:
    template<typename Result>
    void render_sidebar_toolbar(UIContext<InputAction>& ctx,
                                Result& topChrome,
                                float w, float /*h*/,
                                RepoComponent* repo,
                                bool hasRepo, bool /*hasUnstaged*/, bool hasStaged) {
        // Toolbar fills its allocated height
        auto toolbarBg = div(ctx, mk(topChrome.ent(), 1),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(w), percent(1.0f)})
                .with_custom_background(theme::TOOLBAR_BG)
                .with_border_bottom(theme::BORDER)
                .with_flex_direction(FlexDirection::Column)
                .with_align_items(AlignItems::FlexStart)
                .with_padding(Padding{
                    .top = pixels(6), .right = pixels(8),
                    .bottom = pixels(6), .left = pixels(8)})
                .with_roundness(0.0f)
                .with_debug_name("toolbar_sidebar"));

        int nextId = 1100;

        // Single row: Commit | Push | Pull | Stash (gap: 4px between buttons)
        auto row1 = div(ctx, mk(toolbarBg.ent(), 10),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), children()})
                .with_flex_direction(FlexDirection::Row)
                .with_align_items(AlignItems::Center)
                .with_gap(pixels(4))
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("toolbar_row1"));

        auto sidebarBtn = [&](Entity& parent, int id,
                              const std::string& label, bool enabled,
                              bool primary = false) -> bool {
            auto config = preset::Button(label, enabled)
                .with_size(ComponentSize{children(), children()})
                .with_padding(Padding{
                    .top = pixels(4), .right = pixels(14),
                    .bottom = pixels(4), .left = pixels(14)})
                .with_font_size(afterhours::ui::FontSize::Medium)
                .with_cursor(afterhours::ui::CursorType::Pointer)
                .with_debug_name("toolbar_btn");
            if (enabled && !primary) {
                config = config.with_custom_background(theme::BUTTON_SECONDARY)
                               .with_custom_text_color(afterhours::Color{204, 204, 204, 255});
            }
            return static_cast<bool>(button(ctx, mk(parent, id), config));
        };

        if (sidebarBtn(row1.ent(), nextId++, "Commit", hasRepo && hasStaged, true)) {
            auto* editor = ::ecs::find_singleton<CommitEditorComponent, ActiveTab>();
            if (editor) editor->commitRequested = true;
        }
        if (sidebarBtn(row1.ent(), nextId++, "Push", hasRepo)) {
            git::git_push(repo->repoPath);
            repo->refreshRequested = true;
        }
        if (sidebarBtn(row1.ent(), nextId++, "Pull", hasRepo)) {
            git::git_pull(repo->repoPath);
            repo->refreshRequested = true;
        }
        if (sidebarBtn(row1.ent(), nextId++, "Stash", hasRepo)) {
            auto* menuComp = ::ecs::find_singleton<MenuComponent>();
            if (menuComp)
                menuComp->pendingToast = "Stash is not yet implemented";
        }
    }

    template<typename Result>
    void render_fullwidth_toolbar(UIContext<InputAction>& ctx,
                                  Result& topChrome,
                                  float w, float h,
                                  RepoComponent* repo,
                                  bool hasRepo, bool hasUnstaged, bool hasStaged,
                                  const std::string& branchName) {
        // Top border
        div(ctx, mk(topChrome.ent(), 2),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(w), pixels(1)})
                .with_custom_background(theme::BORDER)
                .with_roundness(0.0f)
                .with_debug_name("toolbar_top_border"));

        // Toolbar row
        auto toolbarBg = div(ctx, mk(topChrome.ent(), 3),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(w), pixels(h - 2)})
                .with_custom_background(theme::TOOLBAR_BG)
                .with_flex_direction(FlexDirection::Row)
                .with_align_items(AlignItems::Center)
                .with_padding(Padding{
                    .top = h720(theme::layout::SMALL_PADDING),
                    .right = w1280(theme::layout::PADDING),
                    .bottom = h720(theme::layout::SMALL_PADDING),
                    .left = w1280(theme::layout::PADDING)})
                .with_roundness(0.0f)
                .with_debug_name("toolbar"));

        // Bottom border
        div(ctx, mk(topChrome.ent(), 4),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(w), pixels(1)})
                .with_custom_background(theme::BORDER)
                .with_roundness(0.0f)
                .with_debug_name("toolbar_border"));

        int nextId = 1110;
        auto toolbarButton = [&](const std::string& label, bool enabled) -> bool {
            int id = nextId++;
            float btnWidth = static_cast<float>(label.size()) * 9.0f + 24.0f;
            auto config = preset::Button(label, enabled)
                .with_size(ComponentSize{w1280(btnWidth), h720(28)})
                .with_padding(Padding{
                    .top = h720(4), .right = w1280(10),
                    .bottom = h720(4), .left = w1280(10)})
                .with_margin(Margin{
                    .top = {}, .bottom = {},
                    .left = {}, .right = w1280(3)})
                .with_font_size(afterhours::ui::FontSize::Medium)
                .with_cursor(afterhours::ui::CursorType::Pointer)
                .with_debug_name("toolbar_btn");
            if (enabled) {
                config = config.with_custom_background(afterhours::Color{62, 62, 66, 255})
                               .with_custom_text_color(afterhours::Color{200, 200, 200, 255});
            }
            return static_cast<bool>(button(ctx, mk(toolbarBg.ent(), id), config));
        };

        auto toolbarSeparator = [&]() {
            int id = nextId++;
            div(ctx, mk(toolbarBg.ent(), id),
                ComponentConfig{}
                    .with_size(ComponentSize{
                        w1280(theme::layout::TOOLBAR_SEP_WIDTH),
                        h720(theme::layout::TOOLBAR_SEP_HEIGHT)})
                    .with_margin(Margin{
                        .top = {}, .bottom = {},
                        .left = w1280(theme::layout::TOOLBAR_SEP_MARGIN),
                        .right = w1280(theme::layout::TOOLBAR_SEP_MARGIN)})
                    .with_custom_background(theme::BORDER)
                    .with_roundness(0.0f)
                    .with_debug_name("toolbar_sep"));
        };

        if (toolbarButton("Refresh", hasRepo)) {
            repo->refreshRequested = true;
        }
        if (toolbarButton("Stage All", hasRepo && hasUnstaged)) {
            git::stage_all(repo->repoPath);
            repo->refreshRequested = true;
        }
        if (toolbarButton("Unstage All", hasRepo && hasStaged)) {
            git::unstage_all(repo->repoPath);
            repo->refreshRequested = true;
        }

        toolbarSeparator();

        if (toolbarButton("Commit", hasRepo && hasStaged)) {
            auto* editor = ::ecs::find_singleton<CommitEditorComponent, ActiveTab>();
            if (editor) editor->commitRequested = true;
        }
        if (toolbarButton("Push", hasRepo)) {
            git::git_push(repo->repoPath);
            repo->refreshRequested = true;
        }
        if (toolbarButton("Pull", hasRepo)) {
            git::git_pull(repo->repoPath);
            repo->refreshRequested = true;
        }
        if (toolbarButton("Fetch", hasRepo)) {
            git::git_fetch(repo->repoPath);
            repo->refreshRequested = true;
        }

        toolbarSeparator();

        // Spacer to push branch selector right
        div(ctx, mk(toolbarBg.ent(), nextId++),
            ComponentConfig{}
                .with_size(ComponentSize{afterhours::ui::expand(), h720(1)})
                .with_roundness(0.0f)
                .with_debug_name("toolbar_spacer"));

        // Branch selector
        std::string branchLabel = branchName + " \xe2\x96\xbe";
        if (toolbarButton(branchLabel, hasRepo)) {
            auto* lc = ::ecs::find_singleton<LayoutComponent>();
            if (lc) {
                lc->sidebarMode = LayoutComponent::SidebarMode::Refs;
                lc->sidebarVisible = true;
            }
        }
    }
};

}  // namespace ecs
