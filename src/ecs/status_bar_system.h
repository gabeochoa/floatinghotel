#pragma once

#include <cmath>

#include "ui_imports.h"
#include "../ui/zoom.h"

namespace ecs {

struct StatusBarSystem : afterhours::System<UIContext<InputAction>> {
    void for_each_with(Entity&, UIContext<InputAction>& ctx, float) override {
        auto* layout = find_singleton<LayoutComponent>();
        if (!layout) return;
        auto* repo = find_singleton<RepoComponent, ActiveTab>();
        Entity& root = ui_imm::getUIRootEntity();
        const auto& rect = layout->statusBar;
        const float width = rect.width;
        const float height = rect.height;
        const bool detached = repo && repo->isDetachedHead;
        const auto background = detached ? theme::STATUS_BAR_DETACHED_BG : theme::STATUS_BAR_BG;

        div(ctx, mk(root, 4000), ComponentConfig{}.with_skip_grid_snap()
            .with_size(ComponentSize{pixels(width), pixels(height)})
            .with_absolute_position().with_translate(0.f, rect.y)
            .with_custom_background(background).with_border_top(theme::BORDER)
            .with_roundness(0.f).with_render_layer(5)
            .with_debug_name("status_bar_bg"));

        std::string branch;
        std::string counts;
        if (!repo || repo->repoPath.empty()) branch = "No repository";
        else if (!repo->filesError.empty()) branch = "Repository unavailable";
        else if (!repo->hasLoadedOnce) branch = "Loading repository";
        else {
            branch = detached ? "HEAD " + repo->headCommitHash.substr(0, 7)
                : repo->currentBranch.empty() ? "main" : repo->currentBranch;
            const auto staged = repo->stagedFiles.size();
            const auto unstaged = repo->unstagedFiles.size() + repo->untrackedFiles.size();
            if (staged > 0) counts = std::to_string(staged) + " staged";
            if (staged > 0 && unstaged > 0) counts += ", ";
            if (unstaged > 0) counts += std::to_string(unstaged) + " unstaged";
            if (counts.empty()) counts = "Working tree clean";
            if (repo->reviewWorkspace && width >= 700.f) branch += "   Read-only review";
        }

        const float zoomWidth = std::min(120.f, width);
        const float infoWidth = std::max(0.f, width - zoomWidth);
        auto label = [&](int id, const std::string& text, float x, float labelWidth,
                         TextAlignment alignment, const std::string& debugName) {
            div(ctx, mk(root, id), ComponentConfig{}.with_skip_grid_snap()
                .with_label(text).with_size(ComponentSize{pixels(labelWidth), pixels(height)})
                .with_absolute_position().with_translate(x, rect.y)
                .with_padding(Padding{.left = pixels(10), .right = pixels(10)})
                .with_transparent_bg().with_custom_text_color(theme::STATUS_BAR_TEXT)
                .with_font_size(pixels(11)).with_alignment(alignment)
                .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
                .with_roundness(0.f).with_render_layer(5).with_debug_name(debugName));
        };
        if (infoWidth > 0.f)
            label(4010, branch, 0.f, infoWidth, TextAlignment::Left, "status_info");
        if (!counts.empty() && width >= 620.f)
            label(4020, counts, width * 0.45f, infoWidth - width * 0.45f,
                  TextAlignment::Right, "status_counts");
        const float zoomX = width - zoomWidth;
        label(4030, std::to_string(static_cast<int>(std::lround(ui::zoom::get() * 100.f))) + "%",
              zoomX + 26.f, std::max(0.f, zoomWidth - 52.f), TextAlignment::Center, "status_zoom");
        auto zoomButton = [&](int id, const std::string& text, float x, const std::string& debugName) {
            return button(ctx, mk(root, id), ComponentConfig{}.with_skip_grid_snap()
                .with_label(text).with_size(ComponentSize{pixels(26), pixels(height)})
                .with_absolute_position().with_translate(x, rect.y)
                .with_custom_background(background).with_custom_hover_bg(theme::PANEL_BG)
                .with_custom_text_color(theme::STATUS_BAR_TEXT).with_font_size(pixels(13))
                .with_alignment(TextAlignment::Center).with_roundness(0.f)
                .with_render_layer(6).with_debug_name(debugName));
        };
        if (width >= 120.f) {
            if (zoomButton(4031, "-", zoomX, "zoom_out")) ui::zoom::step(-ui::zoom::kStep);
            if (zoomButton(4032, "+", width - 26.f, "zoom_in")) ui::zoom::step(ui::zoom::kStep);
        }
    }
};

}
