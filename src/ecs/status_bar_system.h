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
using afterhours::ui::FlexDirection;
using afterhours::ui::AlignItems;
using afterhours::ui::ComponentSize;
using afterhours::ui::Padding;
using afterhours::ui::TextAlignment;
using afterhours::ui::HasClickListener;

// StatusBarSystem: Renders the status bar at the bottom of the window.
// Shows branch name, dirty indicator, ahead/behind counts, and command log toggle.
//
// NOTE: Due to a framework issue where children of absolute-positioned elements
// render at screen (0,0) instead of the parent's translated position, the status
// bar renders its content as a SINGLE composed label rather than multiple child
// elements. The "Show Log" button uses a separate absolute div.
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

        // === Status bar background (render_layer 5 so it draws above content) ===
        div(ctx, mk(uiRoot, 4000),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(w), pixels(h)})
                .with_absolute_position()
                .with_translate(0, y)
                .with_custom_background(barBg)
                .with_roundness(0.0f)
                .with_render_layer(5)
                .with_debug_name("status_bar_bg"));

        // === Compose status text as a single string ===
        std::string statusText;
        if (!repoEntities.empty()) {
            auto& repo = repoEntities[0].get().get<RepoComponent>();

            // Branch name
            if (detached) {
                std::string shortHash = repo.headCommitHash.substr(
                    0, std::min<size_t>(7, repo.headCommitHash.size()));
                statusText = "HEAD " + shortHash;
            } else {
                statusText = repo.currentBranch.empty() ? "main" : repo.currentBranch;
            }

            // File counts (staged, unstaged)
            int stagedCount = static_cast<int>(repo.stagedFiles.size());
            int unstagedCount = static_cast<int>(
                repo.unstagedFiles.size() + repo.untrackedFiles.size());

            std::string rightText;
            if (stagedCount > 0 || unstagedCount > 0) {
                if (stagedCount > 0)
                    rightText += std::to_string(stagedCount) + " staged";
                if (stagedCount > 0 && unstagedCount > 0)
                    rightText += ", ";
                if (unstagedCount > 0)
                    rightText += std::to_string(unstagedCount) + " unstaged";
            } else {
                rightText = "clean";
            }

            // Pad with spaces to push right text toward the right side
            statusText += "                    " + rightText;
        } else {
            statusText = "No repository";
        }

        // Status info label (absolute, rendered at correct position)
        float sw = static_cast<float>(afterhours::graphics::get_screen_width());
        float padX = afterhours::ui::resolve_to_pixels(w1280(8), sw);
        div(ctx, mk(uiRoot, 4010),
            ComponentConfig{}
                .with_label(statusText)
                .with_size(ComponentSize{pixels(w * 0.7f), pixels(h)})
                .with_absolute_position()
                .with_translate(padX, y)
                .with_padding(Padding{
                    .top = h720(4), .right = w1280(8),
                    .bottom = h720(4), .left = w1280(8)})
                .with_transparent_bg()
                .with_custom_text_color(theme::STATUS_BAR_TEXT)
                .with_font_size(pixels(theme::layout::FONT_META))
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_render_layer(5)
                .with_debug_name("status_info"));

        // === Right section: command log toggle button (absolute) ===
        std::string logLabel = layout.commandLogVisible ? "Hide Log" : "Show Log";
        float btnW = afterhours::ui::resolve_to_pixels(w1280(80), sw);
        auto logBtn = button(ctx, mk(uiRoot, 4050),
            ComponentConfig{}
                .with_label(logLabel)
                .with_size(ComponentSize{pixels(btnW), pixels(h - 4)})
                .with_absolute_position()
                .with_translate(w - btnW - 8, y + 2)
                .with_padding(Padding{
                    .top = h720(2), .right = w1280(10),
                    .bottom = h720(2), .left = w1280(10)})
                .with_custom_text_color(theme::STATUS_BAR_TEXT)
                .with_font_size(pixels(theme::layout::FONT_CAPTION))
                .with_transparent_bg()
                .with_custom_hover_bg(theme::STATUS_BAR_BTN_HOVER)
                .with_alignment(TextAlignment::Center)
                .with_rounded_corners(theme::layout::ROUNDED_CORNERS)
                .with_roundness(theme::layout::ROUNDNESS_BUTTON)
                .with_render_layer(5)
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
