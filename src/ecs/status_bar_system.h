#pragma once

#include "ui_imports.h"

namespace ecs {

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
        auto* layout = find_singleton<LayoutComponent>();
        if (!layout) return;
        auto* repo = find_singleton<RepoComponent, ActiveTab>();

        Entity& uiRoot = ui_imm::getUIRootEntity();
        float w = layout->statusBar.width;
        float h = layout->statusBar.height;
        float y = layout->statusBar.y;

        // Determine background color (orange for detached HEAD, blue normally)
        bool detached = repo && repo->isDetachedHead;
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

        // === Left text: branch (or detached HEAD) ===
        std::string leftText;
        std::string rightText;
        if (repo) {
            if (detached) {
                std::string shortHash = repo->headCommitHash.substr(
                    0, std::min<size_t>(7, repo->headCommitHash.size()));
                leftText = "HEAD " + shortHash;
            } else {
                leftText = repo->currentBranch.empty() ? "main" : repo->currentBranch;
            }

            // File counts (staged, unstaged)
            int stagedCount = static_cast<int>(repo->stagedFiles.size());
            int unstagedCount = static_cast<int>(
                repo->unstagedFiles.size() + repo->untrackedFiles.size());

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
        } else {
            leftText = "No repository";
        }

        float sw = static_cast<float>(afterhours::graphics::get_screen_width());
        float padX = afterhours::ui::resolve_to_pixels(w1280(8), sw);
        // Log button: fixed width so it stays readable even in a narrow window;
        // label shortens when the window is too tight to fit "Show Log".
        bool narrow = w < 520.0f;
        float btnW = narrow ? 44.0f : 80.0f;

        // Status info label (absolute, rendered at correct position)
        div(ctx, mk(uiRoot, 4010),
            ComponentConfig{}
                .with_label(leftText)
                .with_size(ComponentSize{pixels(w * 0.5f), pixels(h)})
                .with_absolute_position()
                .with_translate(padX, y)
                .with_padding(Padding{
                    .top = h720(4), .right = w1280(8),
                    .bottom = h720(4), .left = w1280(8)})
                .with_transparent_bg()
                .with_custom_text_color(theme::STATUS_BAR_TEXT)
                .with_font_size(afterhours::ui::FontSize::Medium)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_render_layer(5)
                .with_debug_name("status_info"));

        // Right-aligned counts, sitting just left of the log toggle button.
        // Hidden when the window is too narrow to fit them.
        if (!rightText.empty() && !narrow) {
            float countsW = w - btnW - 24.0f;
            if (countsW < 20.0f) countsW = 20.0f;
            div(ctx, mk(uiRoot, 4020),
                ComponentConfig{}
                    .with_label(rightText)
                    .with_size(ComponentSize{pixels(countsW), pixels(h)})
                    .with_absolute_position()
                    .with_translate(0, y)
                    .with_padding(Padding{
                        .top = h720(4), .right = w1280(8),
                        .bottom = h720(4), .left = w1280(8)})
                    .with_transparent_bg()
                    .with_custom_text_color(theme::STATUS_BAR_TEXT)
                    .with_font_size(afterhours::ui::FontSize::Medium)
                    .with_alignment(TextAlignment::Right)
                    .with_roundness(0.0f)
                    .with_render_layer(5)
                    .with_debug_name("status_counts"));
        }

        // === Right section: command log toggle button (absolute) ===
        std::string logLabel = narrow ? (layout->commandLogVisible ? "Hide" : "Log")
                               : (layout->commandLogVisible ? "Hide Log" : "Show Log");
        auto logBtn = button(ctx, mk(uiRoot, 4050),
            preset::Button(logLabel)
                .with_size(ComponentSize{pixels(btnW), pixels(h - 4)})
                .with_absolute_position()
                .with_translate(w - btnW - 8, y + 2)
                .with_padding(Padding{
                    .top = h720(2), .right = w1280(10),
                    .bottom = h720(2), .left = w1280(10)})
                .with_custom_text_color(theme::STATUS_BAR_TEXT)
                .with_font_size(afterhours::ui::FontSize::Medium)
                .with_transparent_bg()
                .with_custom_hover_bg(theme::STATUS_BAR_BTN_HOVER)
                .with_render_layer(5)
                .with_debug_name("status_log_toggle"));

        if (logBtn) {
            layout->commandLogVisible = !layout->commandLogVisible;
        }
    }
};

}  // namespace ecs
