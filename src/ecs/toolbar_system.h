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
        float w = layout.toolbar.width;
        float h = layout.toolbar.height;
        float y = layout.toolbar.y;

        // Toolbar background
        auto toolbarBg = div(ctx, mk(uiRoot, 1100),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(w), pixels(h)})
                .with_absolute_position()
                .with_translate(0, y)
                .with_custom_background(theme::TOOLBAR_BG)
                .with_flex_direction(FlexDirection::Row)
                .with_align_items(AlignItems::Center)
                .with_padding(Padding{
                    .top = pixels(theme::layout::SMALL_PADDING),
                    .right = pixels(theme::layout::PADDING),
                    .bottom = pixels(theme::layout::SMALL_PADDING),
                    .left = pixels(theme::layout::PADDING)})
                .with_roundness(0.0f)
                .with_debug_name("toolbar"));

        // Bottom border
        div(ctx, mk(uiRoot, 1101),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(w), pixels(1)})
                .with_absolute_position()
                .with_translate(0, y + h - 1)
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
            auto config = ComponentConfig{}
                .with_label(label)
                .with_size(ComponentSize{children(), pixels(theme::layout::TOOLBAR_BUTTON_HEIGHT)})
                .with_padding(Padding{
                    .top = pixels(theme::layout::TOOLBAR_BUTTON_VPAD),
                    .right = pixels(theme::layout::TOOLBAR_BUTTON_HPAD),
                    .bottom = pixels(theme::layout::TOOLBAR_BUTTON_VPAD),
                    .left = pixels(theme::layout::TOOLBAR_BUTTON_HPAD)})
                .with_margin(Margin{
                    .top = {},
                    .bottom = {},
                    .left = {},
                    .right = pixels(4)})
                .with_custom_background(afterhours::Color{0, 0, 0, 0})
                .with_custom_text_color(enabled ? theme::TEXT_PRIMARY : theme::TOOLBAR_BTN_DISABLED)
                .with_roundness(0.04f)
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
                        pixels(theme::layout::TOOLBAR_SEP_WIDTH),
                        pixels(theme::layout::TOOLBAR_SEP_HEIGHT)})
                    .with_margin(Margin{
                        .top = {},
                        .bottom = {},
                        .left = pixels(theme::layout::TOOLBAR_SEP_MARGIN),
                        .right = pixels(theme::layout::TOOLBAR_SEP_MARGIN)})
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
                .with_size(ComponentSize{percent(1.0f), pixels(1)})
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
