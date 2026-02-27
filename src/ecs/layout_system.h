#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <fstream>
#include <sstream>

#include <afterhours/src/logging.h>
#include "../git/git_parser.h"
#include "../git/git_runner.h"
#include "../settings.h"
#include "../ui/diff_renderer.h"
#include "../util/git_helpers.h"
#include "ui_imports.h"

namespace ecs {

// LayoutUpdateSystem: Recalculates all panel rectangles each frame based on
// current screen size, sidebar width, and commit log ratio.
struct LayoutUpdateSystem : afterhours::System<LayoutComponent> {
    void for_each_with(Entity& /*entity*/, LayoutComponent& layout,
                       float) override {
        int screenW = afterhours::graphics::get_screen_width();
        int screenH = afterhours::graphics::get_screen_height();
        float sw = static_cast<float>(screenW);
        float sh = static_cast<float>(screenH);

        // Use resolve_to_pixels with h720/w1280 for resolution-independent sizing.
        // h720(x) -> screen_pct(x/720) -> x * (screenH / 720) pixels.
        // w1280(x) -> screen_pct(x/1280) -> x * (screenW / 1280) pixels.
        auto rpxH = [sh](float design_px) {
            return resolve_to_pixels(h720(design_px), sh);
        };
        auto rpxW = [sw](float design_px) {
            return resolve_to_pixels(w1280(design_px), sw);
        };

        float tabStripH = std::max(rpxH(28.0f), 18.0f);
        float menuH = std::max(rpxH(static_cast<float>(theme::layout::MENU_BAR_HEIGHT)), 16.0f);
        float toolbarH = std::max(rpxH(static_cast<float>(theme::layout::TOOLBAR_HEIGHT)), 28.0f);
        float statusH = std::max(rpxH(static_cast<float>(theme::layout::STATUS_BAR_HEIGHT)), 16.0f);

        float actualTabStripH = tabStripH;

        layout.tabStrip = {0, 0, sw, actualTabStripH};
        layout.menuBar = {0, actualTabStripH, sw, menuH};

        // Scale sidebar width to match w1280 coordinate system
        float scaledSidebarW = rpxW(layout.sidebarWidth);
        float scaledSidebarMinW = rpxW(layout.sidebarMinWidth);

        // Ensure sidebar is at least SIDEBAR_MIN_PCT of window width at high res
        float pctMinW = sw * theme::layout::SIDEBAR_MIN_PCT;
        if (pctMinW > scaledSidebarMinW) scaledSidebarMinW = pctMinW;

        // Clamp sidebar width (in screen pixels)
        // At very narrow widths, limit sidebar to 50% of window
        float maxSidebarW = sw * 0.5f;
        scaledSidebarW = std::clamp(scaledSidebarW, std::min(scaledSidebarMinW, maxSidebarW), maxSidebarW);

        float dividerW = rpxW(4.0f);

        float topY = actualTabStripH + menuH;

        if (layout.sidebarVisible) {
            // Toolbar lives inside the sidebar column (single row of buttons)
            float sidebarToolbarH = std::max(rpxH(38.0f), 24.0f);
            layout.toolbar = {0, topY, scaledSidebarW, sidebarToolbarH};

            // Sidebar content area starts below the toolbar strip
            float sidebarContentY = topY + sidebarToolbarH;
            float sidebarContentH = std::max(sh - topY - sidebarToolbarH - statusH, 40.0f);
            layout.sidebar = {0, sidebarContentY, scaledSidebarW, sidebarContentH};

            // Sidebar internal split: files / commits (account for divider)
            float dividerH = rpxH(5.0f);
            float usableH = std::max(sidebarContentH - dividerH, 20.0f);
            float filesH = usableH * (1.0f - layout.commitLogRatio);
            float commitsH = usableH * layout.commitLogRatio;
            layout.sidebarFiles = {0, sidebarContentY, scaledSidebarW, filesH};
            layout.sidebarLog = {0, sidebarContentY + filesH, scaledSidebarW, commitsH};

            // Commit editor area (overlaps bottom of sidebar when visible)
            float scaledEditorH = rpxH(layout.commitEditorHeight);
            if (scaledEditorH > 0.0f) {
                layout.sidebarCommitEditor = {
                    0, sidebarContentY + sidebarContentH - scaledEditorH,
                    scaledSidebarW, scaledEditorH};
            } else {
                layout.sidebarCommitEditor = {0, 0, 0, 0};
            }

            // Main content gets full height (no toolbar gap)
            float mainX = scaledSidebarW + dividerW;
            float mainW = std::max(sw - scaledSidebarW - dividerW, 20.0f);
            float mainContentY = topY;
            float mainContentH = std::max(sh - topY - statusH, 20.0f);

            // Command log panel takes space from the bottom of main content
            if (layout.commandLogVisible) {
                float scaledLogH = rpxH(layout.commandLogHeight);
                float logH = std::clamp(scaledLogH, rpxH(80.0f), mainContentH * 0.6f);
                layout.commandLogHeight = logH * 720.0f / sh;
                float mainH = mainContentH - logH;
                layout.mainContent = {mainX, mainContentY, mainW, mainH};
                layout.commandLog = {mainX, mainContentY + mainH, mainW, logH};
            } else {
                layout.mainContent = {mainX, mainContentY, mainW, mainContentH};
                layout.commandLog = {0, 0, 0, 0};
            }
        } else {
            layout.sidebar = {0, 0, 0, 0};
            layout.sidebarFiles = {0, 0, 0, 0};
            layout.sidebarLog = {0, 0, 0, 0};
            layout.sidebarCommitEditor = {0, 0, 0, 0};

            // No sidebar - toolbar spans full width below menu
            float contentY = topY + toolbarH;
            float contentH = std::max(sh - topY - toolbarH - statusH, 20.0f);
            layout.toolbar = {0, topY, sw, toolbarH};

            float mainX = 0;
            float mainW = sw;

            if (layout.commandLogVisible) {
                float scaledLogH = rpxH(layout.commandLogHeight);
                float logH = std::clamp(scaledLogH, rpxH(80.0f), contentH * 0.6f);
                layout.commandLogHeight = logH * 720.0f / sh;
                float mainH = contentH - logH;
                layout.mainContent = {mainX, contentY, mainW, mainH};
                layout.commandLog = {mainX, contentY + mainH, mainW, logH};
            } else {
                layout.mainContent = {mainX, contentY, mainW, contentH};
                layout.commandLog = {0, 0, 0, 0};
            }
        }

        layout.statusBar = {0, sh - statusH, sw, statusH};
    }
};

// SidebarSystem is defined in sidebar_system.h

// Formats a timestamp (seconds since epoch) into a short time string "HH:MM:SS"
inline std::string format_timestamp(double timestamp) {
    auto secs = static_cast<std::time_t>(timestamp);
    std::tm tm_buf{};
    localtime_r(&secs, &tm_buf);
    char buf[16];
    std::snprintf(buf, sizeof(buf), "%02d:%02d:%02d",
                  tm_buf.tm_hour, tm_buf.tm_min, tm_buf.tm_sec);
    return std::string(buf);
}

// Renders the command log panel at the bottom of the main content area.
// Shows all git commands that have been executed, with success/fail indicators
// and expandable stdout/stderr output.
inline void render_command_log(afterhours::ui::UIContext<InputAction>& ctx,
                               Entity& uiRoot,
                               LayoutComponent& layout) {
    auto cmdLogEntities = afterhours::EntityQuery({.force_merge = true})
                              .whereHasComponent<CommandLogComponent>()
                              .gen();
    if (cmdLogEntities.empty()) return;
    auto& cmdLog = cmdLogEntities[0].get().get<CommandLogComponent>();

    auto& cl = layout.commandLog;

    // === Horizontal divider at top of command log panel ===
    div(ctx, mk(uiRoot, 3200),
        ComponentConfig{}
            .with_size(ComponentSize{pixels(cl.width), pixels(2)})
            .with_absolute_position()
            .with_translate(cl.x, cl.y)
            .with_custom_background(theme::BORDER)
            .with_roundness(0.0f)
            .with_debug_name("cmdlog_divider"));

    // Make divider draggable to resize the command log panel
    {
        auto dragDiv = div(ctx, mk(uiRoot, 3201),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(cl.width), pixels(6)})
                .with_absolute_position()
                .with_translate(cl.x, cl.y - 2.0f)
                .with_custom_background(afterhours::Color{0, 0, 0, 0})
                .with_roundness(0.0f)
                .with_debug_name("cmdlog_drag_handle"));

        dragDiv.ent().addComponentIfMissing<HasDragListener>(
            [](Entity& /*e*/) {});
        auto& drag = dragDiv.ent().get<HasDragListener>();
        if (drag.down) {
            auto mousePos = afterhours::graphics::get_mouse_position();
            float mouseY = static_cast<float>(mousePos.y);
            float bottomY = layout.statusBar.y;
            float topY = layout.mainContent.y + 60.0f; // min main content height
            float newLogH = bottomY - mouseY;
            newLogH = std::clamp(newLogH, 80.0f, bottomY - topY);
            // Convert back to 720-baseline for storage
            float sh = static_cast<float>(afterhours::graphics::get_screen_height());
            float unscaledLogH = newLogH * 720.0f / sh;

            auto lEntities = afterhours::EntityQuery({.force_merge = true})
                                 .whereHasComponent<LayoutComponent>()
                                 .gen();
            if (!lEntities.empty()) {
                lEntities[0].get().get<LayoutComponent>().commandLogHeight = unscaledLogH;
            }
        }
    }

    // === Panel background ===
    auto panelBg = div(ctx, mk(uiRoot, 3210),
        ComponentConfig{}
            .with_size(ComponentSize{pixels(cl.width), pixels(cl.height)})
            .with_absolute_position()
            .with_translate(cl.x, cl.y)
            .with_custom_background(theme::SIDEBAR_BG)
            .with_flex_direction(FlexDirection::Column)
            .with_roundness(0.0f)
            .with_debug_name("cmdlog_panel"));

    // === Header bar ===
    constexpr float HEADER_H = 28.0f;
    auto headerBar = div(ctx, mk(panelBg.ent(), 3220),
        ComponentConfig{}
            .with_size(ComponentSize{percent(1.0f), h720(HEADER_H)})
            .with_custom_background(theme::BORDER)
            .with_flex_direction(FlexDirection::Row)
            .with_align_items(AlignItems::Center)
            .with_padding(Padding{
                .top = h720(0), .right = w1280(8),
                .bottom = h720(0), .left = w1280(8)})
            .with_roundness(0.0f)
            .with_debug_name("cmdlog_header"));

    // Header label
    div(ctx, mk(headerBar.ent(), 3221),
        ComponentConfig{}
            .with_label("GIT COMMAND LOG")
            .with_size(ComponentSize{percent(1.0f), h720(HEADER_H)})
            .with_custom_text_color(theme::TEXT_SECONDARY)
            .with_font_size(afterhours::ui::h720(16))
            .with_alignment(TextAlignment::Left)
            .with_roundness(0.0f)
            .with_debug_name("cmdlog_title"));

    // Entry count label
    std::string countLabel = std::to_string(cmdLog.entries.size()) + " commands";
    div(ctx, mk(headerBar.ent(), 3222),
        ComponentConfig{}
            .with_label(countLabel)
            .with_size(ComponentSize{children(), h720(HEADER_H)})
            .with_custom_text_color(theme::TEXT_SECONDARY)
            .with_font_size(afterhours::ui::FontSize::Medium)
            .with_alignment(TextAlignment::Right)
            .with_roundness(0.0f)
            .with_debug_name("cmdlog_count"));

    // === Scrollable entries area ===
    float cmdLogScreenH = static_cast<float>(afterhours::graphics::get_screen_height());
    float cmdLogHeaderPx = resolve_to_pixels(h720(HEADER_H), cmdLogScreenH);
    float cmdLogScrollH = cl.height - cmdLogHeaderPx;
    if (cmdLogScrollH < 10.0f) cmdLogScrollH = 10.0f;
    auto scrollArea = div(ctx, mk(panelBg.ent(), 3230),
        ComponentConfig{}
            .with_size(ComponentSize{percent(1.0f), pixels(cmdLogScrollH)})
            .with_overflow(Overflow::Auto, Axis::Y)
            .with_flex_direction(FlexDirection::Column)
            .with_custom_background(theme::SIDEBAR_BG)
            .with_roundness(0.0f)
            .with_debug_name("cmdlog_scroll"));

    if (cmdLog.entries.empty()) {
        // Empty state
        div(ctx, mk(scrollArea.ent(), 3240),
            ComponentConfig{}
                .with_label("No commands executed yet")
                .with_size(ComponentSize{percent(1.0f), h720(28)})
                .with_padding(Padding{
                    .top = h720(8), .right = w1280(8),
                    .bottom = h720(8), .left = w1280(8)})
                .with_custom_text_color(afterhours::Color{90, 90, 90, 255})
                .with_font_size(afterhours::ui::FontSize::Large)
                .with_alignment(TextAlignment::Center)
                .with_roundness(0.0f)
                .with_debug_name("cmdlog_empty"));
        return;
    }

    // Render entries in reverse order (most recent first)
    constexpr float ENTRY_CMD_H = 22.0f;
    constexpr float ENTRY_OUTPUT_H = 18.0f;
    constexpr int MAX_VISIBLE = 200; // cap for performance

    int count = 0;
    for (auto it = cmdLog.entries.rbegin(); it != cmdLog.entries.rend() && count < MAX_VISIBLE; ++it, ++count) {
        auto& entry = *it;
        int entryId = count;

        // Status indicator + command text
        // Use UTF-8 check mark or cross mark
        std::string prefix = entry.success ? "\xe2\x9c\x93 " : "\xe2\x9c\x97 ";
        std::string timeStr = format_timestamp(entry.timestamp);
        std::string cmdLabel = prefix + timeStr + "  " + entry.command;

        afterhours::Color cmdColor = entry.success ? theme::STATUS_ADDED : theme::STATUS_DELETED;

        div(ctx, mk(scrollArea.ent(), 3300 + entryId * 10),
            ComponentConfig{}
                .with_label(cmdLabel)
                .with_size(ComponentSize{percent(1.0f), h720(ENTRY_CMD_H)})
                .with_padding(Padding{
                    .top = h720(2), .right = w1280(8),
                    .bottom = h720(2), .left = w1280(8)})
                .with_custom_text_color(cmdColor)
                .with_font_size(afterhours::ui::FontSize::Medium)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("cmdlog_entry_" + std::to_string(entryId)));

        // Stdout (if any, shown in secondary text color)
        if (!entry.output.empty()) {
            // Truncate long output for display
            std::string displayOut = entry.output;
            if (displayOut.size() > 200) {
                displayOut = displayOut.substr(0, 200) + "...";
            }
            // Replace newlines with spaces for single-line display
            for (auto& ch : displayOut) {
                if (ch == '\n') ch = ' ';
                if (ch == '\r') ch = ' ';
            }

            div(ctx, mk(scrollArea.ent(), 3300 + entryId * 10 + 1),
                ComponentConfig{}
                    .with_label(displayOut)
                    .with_size(ComponentSize{percent(1.0f), h720(ENTRY_OUTPUT_H)})
                    .with_padding(Padding{
                        .top = h720(0), .right = w1280(8),
                        .bottom = h720(2), .left = w1280(24)})
                    .with_custom_text_color(theme::TEXT_SECONDARY)
                    .with_font_size(afterhours::ui::FontSize::Medium)
                    .with_alignment(TextAlignment::Left)
                    .with_roundness(0.0f)
                    .with_debug_name("cmdlog_out_" + std::to_string(entryId)));
        }

        // Stderr (if any, shown in red)
        if (!entry.error.empty()) {
            std::string displayErr = entry.error;
            if (displayErr.size() > 200) {
                displayErr = displayErr.substr(0, 200) + "...";
            }
            for (auto& ch : displayErr) {
                if (ch == '\n') ch = ' ';
                if (ch == '\r') ch = ' ';
            }

            div(ctx, mk(scrollArea.ent(), 3300 + entryId * 10 + 2),
                ComponentConfig{}
                    .with_label(displayErr)
                    .with_size(ComponentSize{percent(1.0f), h720(ENTRY_OUTPUT_H)})
                    .with_padding(Padding{
                        .top = h720(0), .right = w1280(8),
                        .bottom = h720(2), .left = w1280(24)})
                    .with_custom_text_color(theme::STATUS_DELETED)
                    .with_font_size(afterhours::ui::FontSize::Medium)
                    .with_alignment(TextAlignment::Left)
                    .with_roundness(0.0f)
                    .with_debug_name("cmdlog_err_" + std::to_string(entryId)));
        }
    }
}

// ---- Commit Detail Helpers (T035) ----
// Shared helpers (parse_iso8601, relative_time, parse_decorations) live
// in src/util/git_helpers.h.
namespace commit_detail_view {

using git_helpers::DecorationType;
using git_helpers::Decoration;
using git_helpers::parse_decorations;

inline std::string relative_time(const std::string& isoDate) {
    return git_helpers::relative_time(isoDate, /*suffix=*/true);
}

// Parse the NUL-separated output from git_show_commit_info
struct CommitInfo {
    std::string subject;
    std::string body;
    std::string author;
    std::string authorEmail;
    std::string date;
    std::string parents;      // Space-separated full parent hashes
    std::string decorations;
};

inline CommitInfo parse_commit_info(const std::string& output) {
    CommitInfo info;
    // Fields are NUL-separated: subject\0body\0author\0authorEmail\0date\0parents\0decorations
    std::vector<std::string> fields;
    size_t start = 0;
    for (size_t i = 0; i < output.size(); ++i) {
        if (output[i] == '\0') {
            fields.push_back(output.substr(start, i - start));
            start = i + 1;
        }
    }
    fields.push_back(output.substr(start));

    // Trim trailing newline from last field
    if (!fields.empty()) {
        auto& last = fields.back();
        while (!last.empty() && (last.back() == '\n' || last.back() == '\r'))
            last.pop_back();
    }

    if (fields.size() > 0) info.subject = fields[0];
    if (fields.size() > 1) {
        info.body = fields[1];
        // Trim trailing newlines from body
        while (!info.body.empty() && (info.body.back() == '\n' || info.body.back() == '\r'))
            info.body.pop_back();
    }
    if (fields.size() > 2) info.author = fields[2];
    if (fields.size() > 3) info.authorEmail = fields[3];
    if (fields.size() > 4) info.date = fields[4];
    if (fields.size() > 5) info.parents = fields[5];
    if (fields.size() > 6) info.decorations = fields[6];
    return info;
}

} // namespace commit_detail_view

// Renders the full commit detail view when a commit is selected.
inline void render_commit_detail(afterhours::ui::UIContext<InputAction>& ctx,
                                  Entity& parent,
                                  RepoComponent& repo,
                                  LayoutComponent& layout) {
    namespace cdv = commit_detail_view;

    // Find the selected commit in the log
    const CommitEntry* selectedCommit = nullptr;
    for (auto& c : repo.commitLog) {
        if (c.hash == repo.selectedCommitHash) {
            selectedCommit = &c;
            break;
        }
    }

    if (!selectedCommit) {
        // Commit no longer in loaded log — show a message instead of blank
        auto container = div(ctx, mk(parent, 3049),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), percent(1.0f)})
                .with_flex_direction(FlexDirection::Column)
                .with_justify_content(JustifyContent::Center)
                .with_align_items(AlignItems::Center)
                .with_custom_background(theme::WINDOW_BG)
                .with_roundness(0.0f)
                .with_debug_name("commit_not_found"));

        div(ctx, mk(container.ent(), 1),
            ComponentConfig{}
                .with_label("Commit not found in loaded history")
                .with_size(ComponentSize{children(), children()})
                .with_custom_text_color(theme::TEXT_SECONDARY)
                .with_font_size(afterhours::ui::FontSize::Large)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("commit_not_found_msg"));

        auto goBackBtn = button(ctx, mk(container.ent(), 2),
            preset::Button("<- Back")
                .with_size(ComponentSize{children(), children()})
                .with_padding(Padding{
                    .top = pixels(6), .right = pixels(16),
                    .bottom = pixels(6), .left = pixels(16)})
                .with_margin(Margin{.top = pixels(12)})
                .with_transparent_bg()
                .with_custom_text_color(theme::BUTTON_PRIMARY)
                .with_font_size(afterhours::ui::FontSize::Large)
                .with_debug_name("commit_not_found_back"));

        if (goBackBtn) {
            repo.selectedCommitHash.clear();
            repo.cachedCommitHash.clear();
        }
        return;
    }

    // Fetch and cache commit diff + metadata if not already cached.
    // Only set cachedCommitHash after both git commands succeed so that
    // transient failures are retried on the next frame.
    bool commitJustChanged = (repo.cachedCommitHash != repo.selectedCommitHash);
    if (commitJustChanged) {
        auto diffResult = git::git_show(repo.repoPath, repo.selectedCommitHash);
        auto infoResult = git::git_show_commit_info(repo.repoPath, repo.selectedCommitHash);

        if (diffResult.success()) {
            repo.commitDetailDiff = git::parse_diff(diffResult.stdout_str());
        } else {
            repo.commitDetailDiff.clear();
        }

        if (infoResult.success()) {
            auto info = cdv::parse_commit_info(infoResult.stdout_str());
            repo.commitDetailBody = info.body;
            repo.commitDetailAuthorEmail = info.authorEmail;
            repo.commitDetailParents = info.parents;
        } else {
            repo.commitDetailBody.clear();
            repo.commitDetailAuthorEmail.clear();
            repo.commitDetailParents.clear();
        }

        repo.cachedCommitHash = repo.selectedCommitHash;
    }

    int nextId = 3050;
    constexpr float PAD = 16.0f;
    constexpr float LABEL_W = 70.0f;
    float contentW = layout.mainContent.width;

    // === Scrollable container ===
    auto scrollContainer = div(ctx, mk(parent, nextId++),
        ComponentConfig{}
            .with_size(ComponentSize{percent(1.0f), percent(1.0f)})
            .with_overflow(Overflow::Scroll, Axis::Y)
            .with_flex_direction(FlexDirection::Column)
            .with_custom_background(theme::WINDOW_BG)
            .with_roundness(0.0f)
            .with_debug_name("commit_detail_scroll"));

    if (commitJustChanged && scrollContainer.ent().has<afterhours::ui::HasScrollView>()) {
        scrollContainer.ent().get<afterhours::ui::HasScrollView>().scroll_offset = {0, 0};
    }

    // === Back button ===
    auto backBtn = button(ctx, mk(scrollContainer.ent(), nextId++),
        preset::Button("<- Back")
            .with_size(ComponentSize{children(), children()})
            .with_padding(Padding{
                .top = pixels(3), .right = pixels(12),
                .bottom = pixels(3), .left = pixels(12)})
            .with_margin(Margin{
                .top = pixels(8), .bottom = pixels(4),
                .left = pixels(PAD), .right = {}})
            .with_transparent_bg()
            .with_custom_text_color(theme::BUTTON_PRIMARY)
            .with_font_size(afterhours::ui::FontSize::Large)
            .with_debug_name("commit_back_btn"));

    if (backBtn) {
        repo.selectedCommitHash.clear();
        repo.cachedCommitHash.clear();
        return;
    }

    // === Subject line ===
    div(ctx, mk(scrollContainer.ent(), nextId++),
        ComponentConfig{}
            .with_label(selectedCommit->subject)
            .with_size(ComponentSize{percent(1.0f), children()})
            .with_padding(Padding{
                .top = pixels(8), .right = pixels(PAD),
                .bottom = pixels(4), .left = pixels(PAD)})
            .with_custom_text_color(theme::TEXT_PRIMARY)
            .with_font_size(pixels(20.0f))
            .with_alignment(TextAlignment::Left)
            .with_roundness(0.0f)
            .with_debug_name("commit_subject"));

    // === Commit body (if present) ===
    if (!repo.commitDetailBody.empty()) {
        div(ctx, mk(scrollContainer.ent(), nextId++),
            ComponentConfig{}
                .with_label(repo.commitDetailBody)
                .with_size(ComponentSize{percent(1.0f), children()})
                .with_padding(Padding{
                    .top = pixels(4), .right = pixels(PAD),
                    .bottom = pixels(8), .left = pixels(PAD)})
                .with_custom_text_color(theme::TEXT_PRIMARY)
                .with_font_size(pixels(14.0f))
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("commit_body"));
    }

    // === Metadata table ===
    float metaValueW = contentW - PAD * 4 - LABEL_W - 8.0f;
    if (metaValueW < 100.0f) metaValueW = 100.0f;

    auto metaBox = div(ctx, mk(scrollContainer.ent(), nextId++),
        ComponentConfig{}
            .with_size(ComponentSize{percent(1.0f), children()})
            .with_custom_background(theme::SIDEBAR_BG)
            .with_flex_direction(FlexDirection::Column)
            .with_padding(Padding{
                .top = pixels(10), .right = pixels(PAD),
                .bottom = pixels(10), .left = pixels(PAD)})
            .with_margin(Margin{
                .top = pixels(4), .bottom = pixels(4),
                .left = pixels(PAD), .right = pixels(PAD)})
            .with_rounded_corners(theme::layout::ROUNDED_CORNERS)
            .with_roundness(theme::layout::ROUNDNESS_BOX)
            .with_debug_name("commit_meta_box"));

    auto metaRow = [&](const std::string& label, const std::string& value,
                       afterhours::Color valueColor = theme::TEXT_PRIMARY) {
        auto row = div(ctx, mk(metaBox.ent(), nextId++),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), children()})
                .with_flex_direction(FlexDirection::Row)
                .with_align_items(AlignItems::Center)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("meta_row"));

        div(ctx, mk(row.ent(), 1),
            ComponentConfig{}
                .with_label(label)
                .with_size(ComponentSize{pixels(LABEL_W), children()})
                .with_transparent_bg()
                .with_custom_text_color(theme::TEXT_SECONDARY)
                .with_font_size(pixels(13.0f))
                .with_alignment(TextAlignment::Right)
                .with_padding(Padding{
                    .top = pixels(2), .right = pixels(8),
                    .bottom = pixels(2), .left = {}})
                .with_roundness(0.0f)
                .with_debug_name("meta_label"));

        div(ctx, mk(row.ent(), 2),
            ComponentConfig{}
                .with_label(value)
                .with_size(ComponentSize{pixels(metaValueW), children()})
                .with_transparent_bg()
                .with_custom_text_color(valueColor)
                .with_font_size(pixels(13.0f))
                .with_alignment(TextAlignment::Left)
                .with_padding(Padding{
                    .top = pixels(2), .bottom = pixels(2)})
                .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
                .with_roundness(0.0f)
                .with_debug_name("meta_value"));
    };

    metaRow("Commit:", selectedCommit->hash, theme::TEXT_SECONDARY);

    std::string authorStr = selectedCommit->author;
    if (!repo.commitDetailAuthorEmail.empty()) {
        authorStr += " <" + repo.commitDetailAuthorEmail + ">";
    }
    metaRow("Author:", authorStr);

    std::string dateStr = selectedCommit->authorDate;
    std::string relTime = cdv::relative_time(selectedCommit->authorDate);
    if (!relTime.empty()) {
        dateStr += " (" + relTime + ")";
    }
    metaRow("Date:", dateStr);

    if (!repo.commitDetailParents.empty()) {
        std::string parentDisplay;
        std::string remaining = repo.commitDetailParents;
        while (!remaining.empty()) {
            size_t sp = remaining.find(' ');
            std::string hash;
            if (sp != std::string::npos) {
                hash = remaining.substr(0, sp);
                remaining = remaining.substr(sp + 1);
            } else {
                hash = remaining;
                remaining.clear();
            }
            if (!parentDisplay.empty()) parentDisplay += ", ";
            parentDisplay += hash.substr(0, 7);
        }
        metaRow("Parents:", parentDisplay, theme::BUTTON_PRIMARY);
    }

    // Decoration badges
    if (!selectedCommit->decorations.empty()) {
        auto badgeRow = div(ctx, mk(metaBox.ent(), nextId++),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), children()})
                .with_flex_direction(FlexDirection::Row)
                .with_align_items(AlignItems::Center)
                .with_gap(pixels(4))
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("meta_badge_row"));

        div(ctx, mk(badgeRow.ent(), 1),
            ComponentConfig{}
                .with_label("Refs:")
                .with_size(ComponentSize{pixels(LABEL_W), children()})
                .with_transparent_bg()
                .with_custom_text_color(theme::TEXT_SECONDARY)
                .with_font_size(pixels(13.0f))
                .with_alignment(TextAlignment::Right)
                .with_padding(Padding{
                    .top = pixels(2), .right = pixels(8),
                    .bottom = pixels(2), .left = {}})
                .with_roundness(0.0f)
                .with_debug_name("refs_label"));

        auto badges = cdv::parse_decorations(selectedCommit->decorations);
        int badgeId = 20;
        for (auto& badge : badges) {
            afterhours::Color bg, text;
            switch (badge.type) {
                case cdv::DecorationType::Head:
                    bg = theme::BADGE_HEAD_BG;
                    text = afterhours::Color{255, 255, 255, 255};
                    break;
                case cdv::DecorationType::LocalBranch:
                    bg = theme::BADGE_BRANCH_BG;
                    text = afterhours::Color{255, 255, 255, 255};
                    break;
                case cdv::DecorationType::RemoteBranch:
                    bg = theme::BADGE_REMOTE_BG;
                    text = afterhours::Color{255, 255, 255, 255};
                    break;
                case cdv::DecorationType::Tag:
                    bg = theme::BADGE_TAG_BG;
                    text = theme::BADGE_TAG_TEXT;
                    break;
                default:
                    bg = theme::BADGE_TAG_BG;
                    text = theme::BADGE_TAG_TEXT;
                    break;
            }

            div(ctx, mk(badgeRow.ent(), badgeId++),
                preset::Badge(badge.label, bg, text)
                    .with_debug_name("commit_dec_badge"));
        }
    }

    // === Separator ===
    div(ctx, mk(scrollContainer.ent(), nextId++),
        ComponentConfig{}
            .with_size(ComponentSize{percent(1.0f), pixels(1)})
            .with_custom_background(theme::BORDER)
            .with_margin(Margin{
                .top = pixels(8), .bottom = pixels(8)})
            .with_roundness(0.0f)
            .with_debug_name("commit_sep"));

    // === File change summary ===
    if (repo.commitDetailDiff.empty()) {
        div(ctx, mk(scrollContainer.ent(), nextId++),
            ComponentConfig{}
                .with_label("No file changes in this commit")
                .with_size(ComponentSize{percent(1.0f), children()})
                .with_padding(Padding{
                    .top = pixels(16), .right = pixels(PAD),
                    .bottom = pixels(16), .left = pixels(PAD)})
                .with_custom_text_color(theme::TEXT_SECONDARY)
                .with_font_size(afterhours::ui::FontSize::Large)
                .with_alignment(TextAlignment::Center)
                .with_roundness(0.0f)
                .with_debug_name("empty_diff_msg"));
    } else {
        int totalAdd = 0, totalDel = 0;
        for (auto& d : repo.commitDetailDiff) {
            totalAdd += d.additions;
            totalDel += d.deletions;
        }

        std::string summaryLabel = "FILES CHANGED (" +
            std::to_string(repo.commitDetailDiff.size()) + " file" +
            (repo.commitDetailDiff.size() != 1 ? "s" : "") +
            ", +" + std::to_string(totalAdd) + " -" + std::to_string(totalDel) + ")";

        div(ctx, mk(scrollContainer.ent(), nextId++),
            ComponentConfig{}
                .with_label(summaryLabel)
                .with_size(ComponentSize{percent(1.0f), children()})
                .with_padding(Padding{
                    .top = pixels(4), .right = pixels(PAD),
                    .bottom = pixels(4), .left = pixels(PAD)})
                .with_custom_text_color(theme::TEXT_SECONDARY)
                .with_font_size(pixels(13.0f))
                .with_letter_spacing(0.5f)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("files_changed_header"));

        // File summary rows
        constexpr float STATS_W = 55.0f;
        constexpr float BAR_W = 50.0f;
        constexpr float BADGE_W = 20.0f;

        int totalChanges = totalAdd + totalDel;
        if (totalChanges == 0) totalChanges = 1;

        float fileNameW = contentW - PAD * 2 - BADGE_W - STATS_W - BAR_W - 8.0f * 3;
        if (fileNameW < 80.0f) fileNameW = 80.0f;

        for (size_t fi = 0; fi < repo.commitDetailDiff.size(); ++fi) {
            auto& fd = repo.commitDetailDiff[fi];

            std::string badge = "M";
            afterhours::Color badgeColor = theme::STATUS_MODIFIED;
            if (fd.isNew) { badge = "A"; badgeColor = theme::STATUS_ADDED; }
            else if (fd.isDeleted) { badge = "D"; badgeColor = theme::STATUS_DELETED; }
            else if (fd.isRenamed) { badge = "R"; badgeColor = theme::STATUS_RENAMED; }

            auto fileRow = div(ctx, mk(scrollContainer.ent(), nextId++),
                ComponentConfig{}
                    .with_size(ComponentSize{percent(1.0f), children()})
                    .with_flex_direction(FlexDirection::Row)
                    .with_flex_wrap(afterhours::ui::FlexWrap::NoWrap)
                    .with_align_items(AlignItems::Center)
                    .with_padding(Padding{
                        .top = pixels(3), .right = pixels(PAD),
                        .bottom = pixels(3), .left = pixels(PAD)})
                    .with_gap(pixels(8))
                    .with_custom_background(theme::WINDOW_BG)
                    .with_roundness(0.0f)
                    .with_debug_name("file_summary_row"));

            // Status badge
            div(ctx, mk(fileRow.ent(), 1),
                ComponentConfig{}
                    .with_label(badge)
                    .with_size(ComponentSize{pixels(BADGE_W), children()})
                    .with_transparent_bg()
                    .with_custom_text_color(badgeColor)
                    .with_font_size(pixels(14.0f))
                    .with_alignment(TextAlignment::Center)
                    .with_roundness(0.0f)
                    .with_debug_name("file_badge"));

            // Filename
            std::string fname = fd.filePath;
            if (fd.isRenamed && !fd.oldPath.empty()) {
                fname = fd.oldPath + " -> " + fd.filePath;
            }
            div(ctx, mk(fileRow.ent(), 2),
                ComponentConfig{}
                    .with_label(fname)
                    .with_size(ComponentSize{pixels(fileNameW), children()})
                    .with_transparent_bg()
                    .with_custom_text_color(theme::TEXT_PRIMARY)
                    .with_font_size(pixels(14.0f))
                    .with_alignment(TextAlignment::Left)
                    .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
                    .with_roundness(0.0f)
                    .with_debug_name("file_name"));

            // Stats (+N -N)
            std::string statsStr;
            if (fd.additions > 0) statsStr += "+" + std::to_string(fd.additions);
            if (fd.deletions > 0) {
                if (!statsStr.empty()) statsStr += " ";
                statsStr += "-" + std::to_string(fd.deletions);
            }
            div(ctx, mk(fileRow.ent(), 3),
                ComponentConfig{}
                    .with_label(statsStr)
                    .with_size(ComponentSize{pixels(STATS_W), children()})
                    .with_transparent_bg()
                    .with_custom_text_color(theme::TEXT_SECONDARY)
                    .with_font_size(pixels(12.0f))
                    .with_alignment(TextAlignment::Right)
                    .with_roundness(0.0f)
                    .with_debug_name("file_stats"));

            // Change bar
            int fileTotal = fd.additions + fd.deletions;
            float filePct = static_cast<float>(fileTotal) / static_cast<float>(totalChanges);
            float addPct = (fileTotal > 0)
                ? static_cast<float>(fd.additions) / static_cast<float>(fileTotal)
                : 0.0f;

            float barFillW = BAR_W * std::min(filePct * 5.0f, 1.0f);
            float greenW = barFillW * addPct;
            float redW = barFillW * (1.0f - addPct);

            auto barContainer = div(ctx, mk(fileRow.ent(), 4),
                ComponentConfig{}
                    .with_size(ComponentSize{pixels(BAR_W), pixels(8)})
                    .with_flex_direction(FlexDirection::Row)
                    .with_custom_background(theme::BORDER)
                    .with_rounded_corners(theme::layout::ROUNDED_CORNERS)
                    .with_roundness(theme::layout::ROUNDNESS_BADGE)
                    .with_debug_name("change_bar"));

            if (greenW > 0.5f) {
                div(ctx, mk(barContainer.ent(), 1),
                    ComponentConfig{}
                        .with_size(ComponentSize{pixels(greenW), pixels(8)})
                        .with_custom_background(theme::STATUS_ADDED)
                        .with_roundness(0.0f)
                        .with_debug_name("bar_green"));
            }
            if (redW > 0.5f) {
                div(ctx, mk(barContainer.ent(), 2),
                    ComponentConfig{}
                        .with_size(ComponentSize{pixels(redW), pixels(8)})
                        .with_custom_background(theme::STATUS_DELETED)
                        .with_roundness(0.0f)
                        .with_debug_name("bar_red"));
            }
        }

        // === Separator before diffs ===
        div(ctx, mk(scrollContainer.ent(), nextId++),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(1)})
                .with_custom_background(theme::BORDER)
                .with_margin(Margin{
                    .top = pixels(8), .bottom = pixels(8)})
                .with_roundness(0.0f)
                .with_debug_name("diff_sep"));

        // === Inline diff (embedded — no nested scroll container) ===
        ui::render_inline_diff(ctx, scrollContainer.ent(),
                               repo.commitDetailDiff,
                               layout.mainContent.width,
                               layout.mainContent.height,
                               /*embedInParentScroll=*/true);
    }
}

inline std::optional<FileDiff> build_new_file_diff(
    const std::string& repoPath, const std::string& relPath) {
    namespace fs = std::filesystem;
    fs::path fullPath = fs::path(repoPath) / relPath;

    std::error_code ec;
    if (!fs::exists(fullPath, ec) || fs::is_directory(fullPath, ec))
        return std::nullopt;

    auto fileSize = fs::file_size(fullPath, ec);
    if (ec) return std::nullopt;

    constexpr std::uintmax_t MAX_SIZE = 1 * 1024 * 1024; // 1 MB
    if (fileSize > MAX_SIZE) return std::nullopt;

    std::ifstream ifs(fullPath, std::ios::binary);
    if (!ifs) return std::nullopt;

    std::string contents((std::istreambuf_iterator<char>(ifs)),
                          std::istreambuf_iterator<char>());

    // Detect binary: any null byte in the first 8 KB
    bool isBinary = false;
    {
        auto checkLen = std::min(contents.size(), size_t(8192));
        for (size_t i = 0; i < checkLen; ++i) {
            if (contents[i] == '\0') { isBinary = true; break; }
        }
    }

    FileDiff diff;
    diff.filePath = relPath;
    diff.isNew = true;

    if (isBinary) {
        diff.isBinary = true;
        return diff;
    }

    std::istringstream ss(contents);
    std::string line;
    DiffHunk hunk;
    hunk.oldStart = 0;
    hunk.oldCount = 0;
    hunk.newStart = 1;

    int lineNum = 0;
    while (std::getline(ss, line)) {
        ++lineNum;
        hunk.lines.push_back("+" + line);
    }
    hunk.newCount = lineNum;
    hunk.header = "@@ -0,0 +1," + std::to_string(lineNum) + " @@ (new file)";
    diff.additions = lineNum;
    diff.hunks.push_back(std::move(hunk));

    return diff;
}

// MainContentSystem: Renders the main content area (diff viewer or empty state).
struct MainContentSystem : afterhours::System<UIContext<InputAction>> {
    void for_each_with(Entity& /*ctxEntity*/, UIContext<InputAction>& ctx,
                       float) override {
        auto layoutEntities = afterhours::EntityQuery({.force_merge = true})
                                  .whereHasComponent<LayoutComponent>()
                                  .gen();
        if (layoutEntities.empty()) return;
        auto& layout = layoutEntities[0].get().get<LayoutComponent>();

        auto repoEntities = afterhours::EntityQuery({.force_merge = true})
                                .whereHasComponent<RepoComponent>()
                                .whereHasComponent<ActiveTab>()
                                .gen();

        Entity& uiRoot = ui_imm::getUIRootEntity();

        // Main content background
        auto mainBg = div(ctx, mk(uiRoot, 3000),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(layout.mainContent.width),
                                        pixels(layout.mainContent.height)})
                .with_absolute_position()
                .with_translate(layout.mainContent.x, layout.mainContent.y)
                .with_custom_background(theme::WINDOW_BG)
                .with_flex_direction(FlexDirection::Column)
                .with_roundness(0.0f)
                .with_debug_name("main_content"));

        // Check if active tab has a repo loaded
        bool hasRepo = !repoEntities.empty() &&
                       !repoEntities[0].get().get<RepoComponent>().repoPath.empty();

        if (!hasRepo) {
            render_welcome_screen(ctx, mainBg.ent(), layout);

            // Still render command log and divider
            if (layout.commandLogVisible) {
                render_command_log(ctx, uiRoot, layout);
            }
            if (layout.sidebarVisible) {
                render_sidebar_divider(ctx, uiRoot, layout);
            }
            return;
        }

        auto& repo = repoEntities[0].get().get<RepoComponent>();
        bool hasSelectedFile = !repo.selectedFilePath.empty();
        bool hasSelectedCommit = !repo.selectedCommitHash.empty();

        if (hasSelectedFile) {
            bool fileJustChanged = (repo.cachedFilePath != repo.selectedFilePath);
            if (fileJustChanged) {
                repo.cachedFilePath = repo.selectedFilePath;
            }

            // Try to find a matching diff from git diff output
            std::vector<FileDiff> selectedDiffs;
            for (auto& d : repo.currentDiff) {
                if (d.filePath == repo.selectedFilePath ||
                    d.filePath.ends_with("/" + repo.selectedFilePath) ||
                    repo.selectedFilePath.ends_with("/" + d.filePath) ||
                    repo.selectedFilePath.ends_with(d.filePath)) {
                    selectedDiffs.push_back(d);
                    break;
                }
            }

            // For new/untracked files, synthesize a diff from the file contents
            if (selectedDiffs.empty()) {
                auto synth = build_new_file_diff(repo.repoPath,
                                                  repo.selectedFilePath);
                if (synth.has_value()) {
                    selectedDiffs.push_back(std::move(*synth));
                }
            }

            if (!selectedDiffs.empty()) {
                ui::render_inline_diff(ctx, mainBg.ent(), selectedDiffs,
                                       0, 0, false, fileJustChanged);
            } else {
                auto noDiffContainer = div(ctx, mk(mainBg.ent(), 3040),
                    ComponentConfig{}
                        .with_size(ComponentSize{percent(1.0f), percent(1.0f)})
                        .with_flex_direction(FlexDirection::Column)
                        .with_justify_content(JustifyContent::Center)
                        .with_align_items(AlignItems::Center)
                        .with_transparent_bg()
                        .with_roundness(0.0f)
                        .with_debug_name("no_diff_container"));
                div(ctx, mk(noDiffContainer.ent(), 3041),
                    ComponentConfig{}
                        .with_label("No diff available for this file")
                        .with_size(ComponentSize{children(), children()})
                        .with_custom_text_color(theme::TEXT_SECONDARY)
                        .with_font_size(afterhours::ui::FontSize::Large)
                        .with_transparent_bg()
                        .with_roundness(0.0f)
                        .with_debug_name("no_diff_msg"));
            }
        } else if (hasSelectedCommit) {
            render_commit_detail(ctx, mainBg.ent(), repo, layout);
        } else {
            auto emptyContainer = div(ctx, mk(mainBg.ent(), 3060),
                ComponentConfig{}
                    .with_size(ComponentSize{percent(1.0f), percent(1.0f)})
                    .with_flex_direction(FlexDirection::Column)
                    .with_justify_content(JustifyContent::Center)
                    .with_align_items(AlignItems::Center)
                    .with_transparent_bg()
                    .with_roundness(0.0f)
                    .with_debug_name("empty_state"));

            if (!repo.hasLoadedOnce && (repo.isRefreshing || repo.refreshRequested)) {
                // Initial load in progress
                static int mainSpinIdx = 0;
                static int mainFrameCounter = 0;
                constexpr const char* spinFrames[] = {
                    "\xe2\xa0\x8b", "\xe2\xa0\x99", "\xe2\xa0\xb9",
                    "\xe2\xa0\xb8", "\xe2\xa0\xbc", "\xe2\xa0\xb4",
                    "\xe2\xa0\xa6", "\xe2\xa0\xa7", "\xe2\xa0\x87",
                    "\xe2\xa0\x8f"
                };
                if (++mainFrameCounter >= 6) {
                    mainFrameCounter = 0;
                    mainSpinIdx = (mainSpinIdx + 1) % 10;
                }

                div(ctx, mk(emptyContainer.ent(), 3005),
                    ComponentConfig{}
                        .with_label(spinFrames[mainSpinIdx])
                        .with_size(ComponentSize{children(), children()})
                        .with_font_size(pixels(32))
                        .with_padding(Padding{
                            .top = h720(0), .right = w1280(0),
                            .bottom = h720(16), .left = w1280(0)})
                        .with_transparent_bg()
                        .with_custom_text_color(theme::TEXT_SECONDARY)
                        .with_alignment(TextAlignment::Center)
                        .with_roundness(0.0f)
                        .with_debug_name("loading_icon"));

                div(ctx, mk(emptyContainer.ent(), 3010),
                    ComponentConfig{}
                        .with_label("Loading repository\xe2\x80\xa6")
                        .with_size(ComponentSize{children(), children()})
                        .with_font_size(afterhours::ui::FontSize::Large)
                        .with_padding(Padding{
                            .top = h720(0), .right = w1280(8),
                            .bottom = h720(6), .left = w1280(8)})
                        .with_transparent_bg()
                        .with_custom_text_color(theme::TEXT_SECONDARY)
                        .with_alignment(TextAlignment::Center)
                        .with_roundness(0.0f)
                        .with_debug_name("loading_text"));
            } else {
                div(ctx, mk(emptyContainer.ent(), 3005),
                    ComponentConfig{}
                        .with_label("\xe2\x97\x87")
                        .with_size(ComponentSize{children(), children()})
                        .with_font_size(pixels(32))
                        .with_padding(Padding{
                            .top = h720(0), .right = w1280(0),
                            .bottom = h720(16), .left = w1280(0)})
                        .with_transparent_bg()
                        .with_custom_text_color(afterhours::Color{80, 80, 80, 255})
                        .with_alignment(TextAlignment::Center)
                        .with_roundness(0.0f)
                        .with_debug_name("empty_icon"));

                div(ctx, mk(emptyContainer.ent(), 3010),
                    ComponentConfig{}
                        .with_label("Select a file or commit")
                        .with_size(ComponentSize{children(), children()})
                        .with_font_size(afterhours::ui::FontSize::Large)
                        .with_padding(Padding{
                            .top = h720(0), .right = w1280(8),
                            .bottom = h720(6), .left = w1280(8)})
                        .with_transparent_bg()
                        .with_custom_text_color(theme::TEXT_SECONDARY)
                        .with_alignment(TextAlignment::Center)
                        .with_roundness(0.0f)
                        .with_debug_name("empty_hint_1"));

                div(ctx, mk(emptyContainer.ent(), 3020),
                    ComponentConfig{}
                        .with_label("to view changes")
                        .with_size(ComponentSize{children(), children()})
                        .with_font_size(afterhours::ui::FontSize::Large)
                        .with_padding(Padding{
                            .top = h720(0), .right = w1280(8),
                            .bottom = h720(4), .left = w1280(8)})
                        .with_transparent_bg()
                        .with_custom_text_color(afterhours::Color{90, 90, 90, 255})
                        .with_alignment(TextAlignment::Center)
                        .with_roundness(0.0f)
                        .with_debug_name("empty_hint_2"));
            }

            // Keyboard shortcut hint
            div(ctx, mk(emptyContainer.ent(), 3030),
                ComponentConfig{}
                    .with_label("j/k navigate  Enter view  s stage  c commit")
                    .with_size(ComponentSize{children(), children()})
                    .with_font_size(afterhours::ui::FontSize::Medium)
                    .with_padding(Padding{
                        .top = h720(16), .right = w1280(8),
                        .bottom = h720(0), .left = w1280(8)})
                    .with_transparent_bg()
                    .with_custom_text_color(afterhours::Color{60, 60, 60, 255})
                    .with_alignment(TextAlignment::Center)
                    .with_roundness(0.0f)
                    .with_debug_name("empty_shortcuts"));
        }

        // === Command Log Panel (at bottom of main content area) ===
        if (layout.commandLogVisible) {
            render_command_log(ctx, uiRoot, layout);
        }

        if (layout.sidebarVisible) {
            render_sidebar_divider(ctx, uiRoot, layout);
        }
    }

    void render_sidebar_divider(UIContext<InputAction>& ctx, Entity& uiRoot,
                                 LayoutComponent& layout) {
        float dividerH = layout.mainContent.height;
        if (layout.commandLogVisible) {
            dividerH += layout.commandLog.height;
        }
        float dividerY = layout.toolbar.y;
        float fullDividerH = dividerH + (layout.mainContent.y - layout.toolbar.y);
        auto vDivider = div(ctx, mk(uiRoot, 3100),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(2), pixels(fullDividerH)})
                .with_absolute_position()
                .with_translate(layout.sidebar.width, dividerY)
                .with_custom_background(theme::BORDER)
                .with_cursor(afterhours::ui::CursorType::ResizeH)
                .with_roundness(0.0f)
                .with_debug_name("sidebar_divider"));

        vDivider.ent().addComponentIfMissing<HasDragListener>(
            [](Entity& /*e*/) {});
        auto& vDrag = vDivider.ent().get<HasDragListener>();
        if (vDrag.down) {
            auto mousePos = afterhours::graphics::get_mouse_position();
            float mouseX = static_cast<float>(mousePos.x);
            float sw = static_cast<float>(afterhours::graphics::get_screen_width());
            float maxW = sw * 0.5f;
            float newWidth1280 = mouseX * 1280.0f / sw;
            float newWidth = std::clamp(newWidth1280, layout.sidebarMinWidth, maxW * 1280.0f / sw);

            auto lEntities = afterhours::EntityQuery({.force_merge = true})
                                 .whereHasComponent<LayoutComponent>()
                                 .gen();
            if (!lEntities.empty()) {
                lEntities[0].get().get<LayoutComponent>().sidebarWidth = newWidth;
            }
        }
    }

    void render_welcome_screen(UIContext<InputAction>& ctx, Entity& parent,
                                LayoutComponent& /*layout*/) {
        auto container = div(ctx, mk(parent, 3060),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), percent(1.0f)})
                .with_flex_direction(FlexDirection::Column)
                .with_justify_content(JustifyContent::Center)
                .with_align_items(AlignItems::Center)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("welcome_screen"));

        div(ctx, mk(container.ent(), 1),
            ComponentConfig{}
                .with_label("\xe2\x97\x87")
                .with_size(ComponentSize{children(), children()})
                .with_font_size(pixels(36))
                .with_padding(Padding{.bottom = h720(12)})
                .with_transparent_bg()
                .with_custom_text_color(afterhours::Color{70, 130, 180, 255})
                .with_alignment(TextAlignment::Center)
                .with_roundness(0.0f)
                .with_debug_name("welcome_icon"));

        div(ctx, mk(container.ent(), 2),
            ComponentConfig{}
                .with_label("Welcome to floatinghotel")
                .with_size(ComponentSize{children(), children()})
                .with_font_size(pixels(22))
                .with_padding(Padding{.bottom = h720(6)})
                .with_transparent_bg()
                .with_custom_text_color(theme::TEXT_PRIMARY)
                .with_alignment(TextAlignment::Center)
                .with_roundness(0.0f)
                .with_debug_name("welcome_title"));

        div(ctx, mk(container.ent(), 3),
            ComponentConfig{}
                .with_label("Open a repository to get started")
                .with_size(ComponentSize{children(), children()})
                .with_font_size(afterhours::ui::FontSize::Large)
                .with_padding(Padding{.bottom = h720(24)})
                .with_transparent_bg()
                .with_custom_text_color(theme::TEXT_SECONDARY)
                .with_alignment(TextAlignment::Center)
                .with_roundness(0.0f)
                .with_debug_name("welcome_subtitle"));

        // Collect normalized paths of repos currently open in other tabs
        auto canonicalize = [](const std::string& p) -> std::string {
            std::error_code ec;
            auto cp = std::filesystem::canonical(p, ec);
            return ec ? p : cp.string();
        };

        auto allTabs = afterhours::EntityQuery({.force_merge = true})
            .whereHasComponent<Tab>()
            .whereHasComponent<RepoComponent>().gen();

        std::vector<std::string> openPaths;
        for (auto& t : allTabs) {
            auto& r = t.get().get<RepoComponent>();
            if (!r.repoPath.empty()) {
                openPaths.push_back(canonicalize(r.repoPath));
            }
        }

        std::vector<std::string> recentRepos;
        auto savedRecent = Settings::get().get_recent_repos();
        for (auto& path : savedRecent) {
            std::string norm = canonicalize(path);
            bool alreadyOpen = false;
            for (auto& op : openPaths) {
                if (op == norm) { alreadyOpen = true; break; }
            }
            if (!alreadyOpen) {
                recentRepos.push_back(path);
            }
        }

        if (!recentRepos.empty()) {
            div(ctx, mk(container.ent(), 10),
                ComponentConfig{}
                    .with_label("Recently Opened")
                    .with_size(ComponentSize{w1280(400), children()})
                    .with_font_size(afterhours::ui::FontSize::Medium)
                    .with_padding(Padding{.bottom = h720(8)})
                    .with_transparent_bg()
                    .with_custom_text_color(theme::TEXT_SECONDARY)
                    .with_alignment(TextAlignment::Left)
                    .with_roundness(0.0f)
                    .with_debug_name("recent_header"));

            constexpr afterhours::Color REPO_ROW_BG = {38, 38, 38, 255};
            constexpr afterhours::Color REPO_ROW_HOVER = {50, 50, 50, 255};
            const char* home = std::getenv("HOME");
            size_t homeLen = home ? std::strlen(home) : 0;

            for (int ri = 0; ri < static_cast<int>(recentRepos.size()); ++ri) {
                std::filesystem::path p(recentRepos[ri]);
                std::string basename = p.filename().string();
                std::string dirPath = p.parent_path().string();

                if (home && dirPath.starts_with(home)) {
                    dirPath = "~" + dirPath.substr(homeLen);
                }

                auto row = button(ctx, mk(container.ent(), 100 + ri),
                    ComponentConfig{}
                        .with_size(ComponentSize{w1280(400), h720(36)})
                        .with_flex_direction(FlexDirection::Column)
                        .with_justify_content(JustifyContent::Center)
                        .with_padding(Padding{
                            .top = h720(4), .right = w1280(12),
                            .bottom = h720(4), .left = w1280(12)})
                        .with_custom_background(REPO_ROW_BG)
                        .with_custom_hover_bg(REPO_ROW_HOVER)
                        .with_roundness(4.0f)
                        .with_margin(Margin{.bottom = h720(2)})
                        .with_cursor(afterhours::ui::CursorType::Pointer)
                        .with_debug_name("recent_repo_" + basename));

                div(ctx, mk(row.ent(), 1),
                    ComponentConfig{}
                        .with_label(basename)
                        .with_size(ComponentSize{percent(1.0f), children()})
                        .with_font_size(afterhours::ui::FontSize::Large)
                        .with_transparent_bg()
                        .with_custom_text_color(theme::TEXT_PRIMARY)
                        .with_alignment(TextAlignment::Left)
                        .with_roundness(0.0f)
                        .with_debug_name("recent_name"));

                div(ctx, mk(row.ent(), 2),
                    ComponentConfig{}
                        .with_label(dirPath)
                        .with_size(ComponentSize{percent(1.0f), children()})
                        .with_font_size(afterhours::ui::h720(16))
                        .with_transparent_bg()
                        .with_custom_text_color(afterhours::Color{100, 100, 100, 255})
                        .with_alignment(TextAlignment::Left)
                        .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
                        .with_roundness(0.0f)
                        .with_debug_name("recent_path"));

                // Click to open repo in this tab
                if (row) {
                    auto activeQ = afterhours::EntityQuery({.force_merge = true})
                        .whereHasComponent<RepoComponent>()
                        .whereHasComponent<ActiveTab>().gen();
                    if (!activeQ.empty()) {
                        auto& repo = activeQ[0].get().get<RepoComponent>();
                        repo.repoPath = recentRepos[ri];
                        repo.refreshRequested = true;
                        Settings::get().add_recent_repo(recentRepos[ri]);
                    }
                }
            }
        } else {
            div(ctx, mk(container.ent(), 10),
                ComponentConfig{}
                    .with_label("No recent repositories")
                    .with_size(ComponentSize{children(), children()})
                    .with_font_size(afterhours::ui::FontSize::Medium)
                    .with_padding(Padding{.bottom = h720(8)})
                    .with_transparent_bg()
                    .with_custom_text_color(afterhours::Color{70, 70, 70, 255})
                    .with_alignment(TextAlignment::Center)
                    .with_roundness(0.0f)
                    .with_debug_name("no_recent"));
        }

        // Open repo hint
        div(ctx, mk(container.ent(), 20),
            ComponentConfig{}
                .with_label("Cmd+O to open a repository")
                .with_size(ComponentSize{children(), children()})
                .with_font_size(afterhours::ui::FontSize::Medium)
                .with_padding(Padding{.top = h720(20)})
                .with_transparent_bg()
                .with_custom_text_color(afterhours::Color{60, 60, 60, 255})
                .with_alignment(TextAlignment::Center)
                .with_roundness(0.0f)
                .with_debug_name("welcome_hint"));
    }
};

}  // namespace ecs
