#pragma once

#include <string>

#include "../../vendor/afterhours/src/core/system.h"
#include "../input_mapping.h"
#include "../rl.h"
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
using afterhours::ui::ComponentSize;
using afterhours::ui::Padding;
using afterhours::ui::Margin;
using afterhours::ui::TextAlignment;
using afterhours::ui::HasClickListener;

// ToolbarSystem: Renders the horizontal toolbar below the menu bar.
// Buttons: Refresh | Stage All | Unstage All | sep | Commit | Push | Pull | Fetch | sep | Branch
struct ToolbarSystem : afterhours::System<UIContext<InputAction>> {
    void for_each_with(Entity& /*ctxEntity*/, UIContext<InputAction>& ctx,
                       float) override {
        auto layoutEntities = afterhours::EntityQuery({.force_merge = true})
                                  .whereHasComponent<LayoutComponent>()
                                  .gen();
        if (layoutEntities.empty()) return;
        auto& layout = layoutEntities[0].get().get<LayoutComponent>();

        auto repoEntities = afterhours::EntityQuery({.force_merge = true})
                                .whereHasComponent<RepoComponent>()
                                .gen();

        Entity& uiRoot = ui_imm::getUIRootEntity();
        float toolbarX = layout.toolbar.x;
        float w = layout.toolbar.width;
        float h = layout.toolbar.height;
        float toolbarY = layout.toolbar.y;

        // Position toolbar directly at its layout position (after sidebar when visible)
        auto topChrome = div(ctx, mk(uiRoot, 5000),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(w), pixels(h + 2)})
                .with_absolute_position()
                .with_translate(toolbarX, toolbarY)
                .with_flex_direction(FlexDirection::Column)
                .with_roundness(0.0f)
                .with_render_layer(5)
                .with_debug_name("top_chrome"));

        // Top border (separates from menu bar)
        div(ctx, mk(topChrome.ent(), 2),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(w), pixels(1)})
                .with_custom_background(theme::BORDER)
                .with_roundness(0.0f)
                .with_debug_name("toolbar_top_border"));

        // Toolbar row (flow child of topChrome, positioned after spacer)
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

        // Get repo state for enable/disable logic
        bool hasRepo = !repoEntities.empty();
        bool hasUnstaged = false;
        bool hasStaged = false;
        std::string branchName = "main";

        if (hasRepo) {
            auto& repo = repoEntities[0].get().get<RepoComponent>();
            hasUnstaged = !repo.unstagedFiles.empty() || !repo.untrackedFiles.empty();
            hasStaged = !repo.stagedFiles.empty();
            if (!repo.currentBranch.empty()) {
                branchName = repo.currentBranch;
            }
        }

        // Helper lambda: create a toolbar button
        int nextId = 1110;
        auto toolbarButton = [&](const std::string& label, bool enabled) -> bool {
            int id = nextId++;
            auto btnBg = enabled ? afterhours::Color{62, 62, 66, 255}   // Subtle but visible
                                 : afterhours::Color{55, 55, 58, 0};   // Transparent when disabled
            auto textCol = enabled ? afterhours::Color{200, 200, 200, 255}
                                   : afterhours::Color{120, 120, 120, 255}; // Dim but readable
            // Sized per label length for clean proportions
            float btnWidth = static_cast<float>(label.size()) * 9.0f + 24.0f;
            auto config = ComponentConfig{}
                .with_label(label)
                .with_size(ComponentSize{w1280(btnWidth), h720(28)})
                .with_padding(Padding{
                    .top = h720(4),
                    .right = w1280(10),
                    .bottom = h720(4),
                    .left = w1280(10)})
                .with_margin(Margin{
                    .top = {},
                    .bottom = {},
                    .left = {},
                    .right = w1280(3)})
                .with_custom_background(btnBg)
                .with_custom_text_color(textCol)
                .with_roundness(0.08f)
                .with_alignment(TextAlignment::Center)
                .with_debug_name("toolbar_btn");
            config.disabled = !enabled;

            auto result = button(ctx, mk(toolbarBg.ent(), id), config);
            return static_cast<bool>(result);
        };

        // Helper lambda: draw a separator
        auto toolbarSeparator = [&]() {
            int id = nextId++;
            div(ctx, mk(toolbarBg.ent(), id),
                ComponentConfig{}
                    .with_size(ComponentSize{
                        w1280(theme::layout::TOOLBAR_SEP_WIDTH),
                        h720(theme::layout::TOOLBAR_SEP_HEIGHT)})
                    .with_margin(Margin{
                        .top = {},
                        .bottom = {},
                        .left = w1280(theme::layout::TOOLBAR_SEP_MARGIN),
                        .right = w1280(theme::layout::TOOLBAR_SEP_MARGIN)})
                    .with_custom_background(theme::BORDER)
                    .with_roundness(0.0f)
                    .with_debug_name("toolbar_sep"));
        };

        // === Staging group ===

        // Refresh button
        if (toolbarButton("Refresh", hasRepo)) {
            if (hasRepo) {
                auto& repo = repoEntities[0].get().get<RepoComponent>();
                repo.refreshRequested = true;
            }
        }

        // Stage All button
        if (toolbarButton("Stage All", hasRepo && hasUnstaged)) {
            if (hasRepo) {
                auto& repo = repoEntities[0].get().get<RepoComponent>();
                repo.refreshRequested = true;
                // Actual staging handled by git command system (T025)
            }
        }

        // Unstage All button
        if (toolbarButton("Unstage All", hasRepo && hasStaged)) {
            if (hasRepo) {
                auto& repo = repoEntities[0].get().get<RepoComponent>();
                repo.refreshRequested = true;
                // Actual unstaging handled by git command system (T025)
            }
        }

        // === Separator ===
        toolbarSeparator();

        // === Git operations group ===

        // Commit button
        if (toolbarButton("Commit", hasRepo && hasStaged)) {
            // Trigger commit workflow (T030) — checks unstaged policy
            auto editorEntities = afterhours::EntityQuery({.force_merge = true})
                                      .whereHasComponent<CommitEditorComponent>()
                                      .gen();
            if (!editorEntities.empty()) {
                editorEntities[0].get().get<CommitEditorComponent>()
                    .commitRequested = true;
            }
        }

        // Push button
        if (toolbarButton("Push", hasRepo)) {
            // Async push handled by T046
        }

        // Pull button
        if (toolbarButton("Pull", hasRepo)) {
            // Async pull handled by T046
        }

        // Fetch button
        if (toolbarButton("Fetch", hasRepo)) {
            // Async fetch handled by T046
        }

        // === Separator ===
        toolbarSeparator();

        // === Spacer to push branch selector right ===
        div(ctx, mk(toolbarBg.ent(), nextId++),
            ComponentConfig{}
                .with_size(ComponentSize{afterhours::ui::expand(), h720(1)})
                .with_roundness(0.0f)
                .with_debug_name("toolbar_spacer"));

        // === Branch selector (right-aligned) ===
        std::string branchLabel = branchName + " \xe2\x96\xbe"; // ▾
        if (toolbarButton(branchLabel, hasRepo)) {
            // Toggle sidebar to Refs view (T031)
            auto lEntities = afterhours::EntityQuery({.force_merge = true})
                                 .whereHasComponent<LayoutComponent>()
                                 .gen();
            if (!lEntities.empty()) {
                auto& lc = lEntities[0].get().get<LayoutComponent>();
                lc.sidebarMode = LayoutComponent::SidebarMode::Refs;
                lc.sidebarVisible = true;
            }
        }
    }
};

}  // namespace ecs
