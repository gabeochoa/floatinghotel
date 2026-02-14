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
using afterhours::ui::FlexDirection;
using afterhours::ui::AlignItems;
using afterhours::ui::ComponentSize;
using afterhours::ui::Padding;
using afterhours::ui::TextAlignment;
using afterhours::ui::HasClickListener;

// StatusBarSystem: Renders the status bar at the bottom of the window.
// Shows branch name, dirty indicator, ahead/behind counts, and command log toggle.
struct StatusBarSystem : afterhours::System<UIContext<InputAction>> {
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
        float w = layout.statusBar.width;
        float h = layout.statusBar.height;
        float y = layout.statusBar.y;

        // Determine background color (orange for detached HEAD, blue normally)
        bool detached = false;
        if (!repoEntities.empty()) {
            detached = repoEntities[0].get().get<RepoComponent>().isDetachedHead;
        }
        auto barBg = detached ? theme::STATUS_BAR_DETACHED_BG : theme::STATUS_BAR_BG;

        // === Status bar background ===
        auto barResult = div(ctx, mk(uiRoot, 4000),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(w), pixels(h)})
                .with_absolute_position()
                .with_translate(0, y)
                .with_custom_background(barBg)
                .with_flex_direction(FlexDirection::Row)
                .with_align_items(AlignItems::Center)
                .with_padding(Padding{
                    .top = pixels(0), .right = pixels(8),
                    .bottom = pixels(0), .left = pixels(8)})
                .with_roundness(0.0f)
                .with_debug_name("status_bar"));

        // === Left section: branch info ===
        std::string branchLabel;
        bool isDirty = false;
        int aheadCount = 0;
        int behindCount = 0;

        if (!repoEntities.empty()) {
            auto& repo = repoEntities[0].get().get<RepoComponent>();
            isDirty = repo.isDirty;
            aheadCount = repo.aheadCount;
            behindCount = repo.behindCount;

            if (detached) {
                // Show detached HEAD warning with short hash
                std::string shortHash = repo.headCommitHash.substr(
                    0, std::min<size_t>(7, repo.headCommitHash.size()));
                branchLabel = "HEAD " + shortHash;
            } else {
                branchLabel = repo.currentBranch.empty() ? "main" : repo.currentBranch;
            }
        } else {
            branchLabel = "No repository";
        }

        // Branch name
        div(ctx, mk(barResult.ent(), 4010),
            ComponentConfig{}
                .with_label(branchLabel)
                .with_size(ComponentSize{afterhours::ui::children(), pixels(h)})
                .with_padding(Padding{
                    .top = pixels(0), .right = pixels(8),
                    .bottom = pixels(0), .left = pixels(0)})
                .with_custom_text_color(theme::STATUS_BAR_TEXT)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("status_branch"));

        // Dirty indicator (colored dot)
        if (!repoEntities.empty()) {
            auto dotColor = isDirty ? theme::STATUS_BAR_DIRTY : theme::STATUS_BAR_CLEAN;
            std::string dotChar = isDirty ? "\xe2\x97\x8f" : "\xe2\x9c\x93"; // ● or ✓
            div(ctx, mk(barResult.ent(), 4020),
                ComponentConfig{}
                    .with_label(dotChar)
                    .with_size(ComponentSize{afterhours::ui::children(), pixels(h)})
                    .with_padding(Padding{
                        .top = pixels(0), .right = pixels(8),
                        .bottom = pixels(0), .left = pixels(0)})
                    .with_custom_text_color(dotColor)
                    .with_alignment(TextAlignment::Center)
                    .with_roundness(0.0f)
                    .with_debug_name("status_dirty"));
        }

        // Ahead/behind counts (hidden when both 0)
        if (aheadCount > 0 || behindCount > 0) {
            std::string abText;
            if (behindCount > 0)
                abText += "\xe2\x86\x93" + std::to_string(behindCount); // ↓N
            if (aheadCount > 0) {
                if (!abText.empty()) abText += " ";
                abText += "\xe2\x86\x91" + std::to_string(aheadCount); // ↑N
            }
            div(ctx, mk(barResult.ent(), 4030),
                ComponentConfig{}
                    .with_label(abText)
                    .with_size(ComponentSize{afterhours::ui::children(), pixels(h)})
                    .with_padding(Padding{
                        .top = pixels(0), .right = pixels(8),
                        .bottom = pixels(0), .left = pixels(0)})
                    .with_custom_text_color(theme::STATUS_BAR_TEXT)
                    .with_alignment(TextAlignment::Left)
                    .with_roundness(0.0f)
                    .with_debug_name("status_ahead_behind"));
        }

        // === Spacer (flex-grow to push right section to the right) ===
        div(ctx, mk(barResult.ent(), 4040),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(1)})
                .with_roundness(0.0f)
                .with_debug_name("status_spacer"));

        // === Right section: command log toggle button ===
        std::string logLabel = layout.commandLogVisible ? "Hide Log" : "Show Log";
        auto logBtn = button(ctx, mk(barResult.ent(), 4050),
            ComponentConfig{}
                .with_label(logLabel)
                .with_size(ComponentSize{afterhours::ui::children(), pixels(h - 4)})
                .with_padding(Padding{
                    .top = pixels(2), .right = pixels(8),
                    .bottom = pixels(2), .left = pixels(8)})
                .with_custom_text_color(theme::STATUS_BAR_TEXT)
                .with_custom_background(afterhours::Color{0, 0, 0, 0})
                .with_alignment(TextAlignment::Center)
                .with_roundness(0.0f)
                .with_debug_name("status_log_toggle"));

        if (logBtn) {
            // Toggle command log visibility on LayoutComponent
            auto lEntities = afterhours::EntityQuery({.force_merge = true})
                                 .whereHasComponent<LayoutComponent>()
                                 .gen();
            if (!lEntities.empty()) {
                auto& lc = lEntities[0].get().get<LayoutComponent>();
                lc.commandLogVisible = !lc.commandLogVisible;
            }
        }
    }
};

}  // namespace ecs
