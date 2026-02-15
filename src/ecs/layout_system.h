#pragma once

#include <algorithm>
#include <chrono>
#include <cmath>
#include <string>

#include "../../vendor/afterhours/src/core/system.h"
#include "../git/git_parser.h"
#include "../git/git_runner.h"
#include "../input_mapping.h"
#include <afterhours/src/logging.h>
#include "../rl.h"
#include "../ui/diff_renderer.h"
#include "../ui/theme.h"
#include "../ui_context.h"
#include "components.h"

namespace ecs {

using afterhours::Entity;
using afterhours::ui::UIContext;
using afterhours::ui::imm::ComponentConfig;
using afterhours::ui::imm::div;
using afterhours::ui::imm::hstack;
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
using afterhours::ui::HasDragListener;
using afterhours::ui::Overflow;
using afterhours::ui::Axis;
using afterhours::ui::resolve_to_pixels;

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

        float menuH = rpxH(static_cast<float>(theme::layout::MENU_BAR_HEIGHT));
        float toolbarH = rpxH(static_cast<float>(theme::layout::TOOLBAR_HEIGHT));
        float statusH = rpxH(static_cast<float>(theme::layout::STATUS_BAR_HEIGHT));
        float contentY = menuH + toolbarH;
        float contentH = sh - menuH - toolbarH - statusH;

        layout.menuBar = {0, 0, sw, menuH};
        // Toolbar position/size computed after sidebar width is known (below)

        // Scale sidebar width to match w1280 coordinate system
        float scaledSidebarW = rpxW(layout.sidebarWidth);
        float scaledSidebarMinW = rpxW(layout.sidebarMinWidth);

        // Ensure sidebar is at least SIDEBAR_MIN_PCT of window width at high res
        float pctMinW = sw * theme::layout::SIDEBAR_MIN_PCT;
        if (pctMinW > scaledSidebarMinW) scaledSidebarMinW = pctMinW;

        // Clamp sidebar width (in screen pixels)
        float maxSidebarW = sw * 0.5f;
        scaledSidebarW = std::clamp(scaledSidebarW, scaledSidebarMinW, maxSidebarW);

        float dividerW = rpxW(4.0f);

        if (layout.sidebarVisible) {
            layout.sidebar = {0, contentY, scaledSidebarW, contentH};

            // Toolbar starts after the sidebar, spanning the content area only
            // This avoids z-order issues where sidebar renders on top of toolbar
            layout.toolbar = {scaledSidebarW + dividerW, menuH, sw - scaledSidebarW - dividerW, toolbarH};

            // Sidebar internal split: files (1-commitLogRatio) / commits (commitLogRatio)
            float filesH = contentH * (1.0f - layout.commitLogRatio);
            float commitsH = contentH * layout.commitLogRatio;
            layout.sidebarFiles = {0, contentY, scaledSidebarW, filesH};
            layout.sidebarLog = {0, contentY + filesH, scaledSidebarW, commitsH};

            // Commit editor area (overlaps bottom of sidebar when visible)
            float scaledEditorH = rpxH(layout.commitEditorHeight);
            if (scaledEditorH > 0.0f) {
                layout.sidebarCommitEditor = {
                    0, contentY + contentH - scaledEditorH,
                    scaledSidebarW, scaledEditorH};
            } else {
                layout.sidebarCommitEditor = {0, 0, 0, 0};
            }

            float mainX = scaledSidebarW + dividerW;
            float mainW = sw - scaledSidebarW - dividerW;

            // Command log panel takes space from the bottom of main content
            if (layout.commandLogVisible) {
                float scaledLogH = rpxH(layout.commandLogHeight);
                float logH = std::clamp(scaledLogH, rpxH(80.0f), contentH * 0.6f);
                layout.commandLogHeight = logH * 720.0f / sh; // store back in 720p coords
                float mainH = contentH - logH;
                layout.mainContent = {mainX, contentY, mainW, mainH};
                layout.commandLog = {mainX, contentY + mainH, mainW, logH};
            } else {
                layout.mainContent = {mainX, contentY, mainW, contentH};
                layout.commandLog = {0, 0, 0, 0};
            }
        } else {
            layout.sidebar = {0, 0, 0, 0};
            layout.sidebarFiles = {0, 0, 0, 0};
            layout.sidebarLog = {0, 0, 0, 0};
            layout.sidebarCommitEditor = {0, 0, 0, 0};

            // No sidebar - toolbar spans full width
            layout.toolbar = {0, menuH, sw, toolbarH};

            float mainX = 0;
            float mainW = sw;

            if (layout.commandLogVisible) {
                float scaledLogH = rpxH(layout.commandLogHeight);
                float logH = std::clamp(scaledLogH, rpxH(80.0f), contentH * 0.6f);
                layout.commandLogHeight = logH * 720.0f / sh; // store back in 720p coords
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
            .with_font_size(h720(theme::layout::FONT_CAPTION))
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
            .with_font_size(h720(theme::layout::FONT_META))
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
            .with_overflow(Overflow::Scroll, Axis::Y)
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
                .with_font_size(h720(theme::layout::FONT_CHROME))
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
                .with_font_size(h720(theme::layout::FONT_CODE))
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
                    .with_font_size(h720(theme::layout::FONT_META))
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
                    .with_font_size(h720(theme::layout::FONT_META))
                    .with_alignment(TextAlignment::Left)
                    .with_roundness(0.0f)
                    .with_debug_name("cmdlog_err_" + std::to_string(entryId)));
        }
    }
}

// ---- Commit Detail Helpers (T035) ----
namespace commit_detail_view {

// Parse ISO 8601 date to time_t
inline std::time_t parse_iso8601(const std::string& dateStr) {
    if (dateStr.size() < 19) return 0;
    std::tm tm{};
    tm.tm_year = std::stoi(dateStr.substr(0, 4)) - 1900;
    tm.tm_mon  = std::stoi(dateStr.substr(5, 2)) - 1;
    tm.tm_mday = std::stoi(dateStr.substr(8, 2));
    tm.tm_hour = std::stoi(dateStr.substr(11, 2));
    tm.tm_min  = std::stoi(dateStr.substr(14, 2));
    tm.tm_sec  = std::stoi(dateStr.substr(17, 2));
    tm.tm_isdst = -1;
    std::time_t t = timegm(&tm);
    if (dateStr.size() > 19) {
        char sign = dateStr[19];
        if (sign == '+' || sign == '-') {
            int tzH = std::stoi(dateStr.substr(20, 2));
            int tzM = (dateStr.size() >= 25) ? std::stoi(dateStr.substr(23, 2)) : 0;
            int offset = tzH * 3600 + tzM * 60;
            t += (sign == '+') ? -offset : offset;
        }
    }
    return t;
}

inline std::string relative_time(const std::string& isoDate) {
    if (isoDate.empty()) return "";
    std::time_t commitTime = parse_iso8601(isoDate);
    if (commitTime == 0) return "";
    std::time_t now = std::time(nullptr);
    long diff = static_cast<long>(std::difftime(now, commitTime));
    if (diff < 0) return "now";
    if (diff < 60) return std::to_string(diff) + "s ago";
    if (diff < 3600) return std::to_string(diff / 60) + "m ago";
    if (diff < 86400) return std::to_string(diff / 3600) + "h ago";
    if (diff < 604800) return std::to_string(diff / 86400) + "d ago";
    if (diff < 2592000) return std::to_string(diff / 604800) + "w ago";
    if (diff < 31536000) return std::to_string(diff / 2592000) + "mo ago";
    return std::to_string(diff / 31536000) + "y ago";
}

enum class DecorationType { LocalBranch, Head, RemoteBranch, Tag };

struct Decoration {
    std::string label;
    DecorationType type;
};

inline std::vector<Decoration> parse_decorations(const std::string& decStr) {
    std::vector<Decoration> result;
    if (decStr.empty()) return result;
    std::string remaining = decStr;
    while (!remaining.empty()) {
        size_t pos = remaining.find(", ");
        std::string item;
        if (pos != std::string::npos) {
            item = remaining.substr(0, pos);
            remaining = remaining.substr(pos + 2);
        } else {
            item = remaining;
            remaining.clear();
        }
        while (!item.empty() && item.front() == ' ') item.erase(item.begin());
        while (!item.empty() && item.back() == ' ') item.pop_back();
        if (item.empty()) continue;
        if (item.find("HEAD -> ") == 0) {
            result.push_back({"HEAD", DecorationType::Head});
            result.push_back({item.substr(8), DecorationType::LocalBranch});
        } else if (item == "HEAD") {
            result.push_back({"HEAD", DecorationType::Head});
        } else if (item.find("tag: ") == 0) {
            result.push_back({item.substr(5), DecorationType::Tag});
        } else if (item.find('/') != std::string::npos) {
            result.push_back({item, DecorationType::RemoteBranch});
        } else {
            result.push_back({item, DecorationType::LocalBranch});
        }
    }
    return result;
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
    if (!selectedCommit) return;

    // Fetch and cache commit diff + metadata if not already cached
    if (repo.cachedCommitHash != repo.selectedCommitHash) {
        repo.cachedCommitHash = repo.selectedCommitHash;

        // Get commit diff
        auto diffResult = git::git_show(repo.repoPath, repo.selectedCommitHash);
        if (diffResult.success()) {
            repo.commitDetailDiff = git::parse_diff(diffResult.stdout_str());
        } else {
            repo.commitDetailDiff.clear();
        }

        // Get commit metadata (body, email, parents)
        auto infoResult = git::git_show_commit_info(repo.repoPath, repo.selectedCommitHash);
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
    }

    // ID range: 3050-3099 for commit detail header elements
    int nextId = 3050;

    // === Scrollable container for entire commit detail ===
    auto scrollContainer = div(ctx, mk(parent, nextId++),
        ComponentConfig{}
            .with_size(ComponentSize{percent(1.0f), percent(1.0f)})
            .with_overflow(Overflow::Scroll, Axis::Y)
            .with_flex_direction(FlexDirection::Column)
            .with_custom_background(theme::WINDOW_BG)
            .with_roundness(0.0f)
            .with_debug_name("commit_detail_scroll"));

    // === Header section: subject + body ===
    constexpr float HEADER_PAD = 16.0f;

    // Back button
    auto backBtn = button(ctx, mk(scrollContainer.ent(), nextId++),
        ComponentConfig{}
            .with_label("<- Back")
            .with_size(ComponentSize{children(), h720(22)})
            .with_padding(Padding{
                .top = h720(3), .right = w1280(12),
                .bottom = h720(3), .left = w1280(12)})
            .with_margin(Margin{
                .top = h720(8), .bottom = h720(4),
                .left = w1280(HEADER_PAD), .right = {}})
            .with_custom_background(afterhours::Color{0, 0, 0, 0})
            .with_custom_text_color(theme::BUTTON_PRIMARY)
            .with_font_size(h720(theme::layout::FONT_CHROME))
            .with_roundness(0.04f)
            .with_debug_name("commit_back_btn"));

    if (backBtn) {
        repo.selectedCommitHash.clear();
        repo.cachedCommitHash.clear();
        return;
    }

    // Subject line (bold, large)
    div(ctx, mk(scrollContainer.ent(), nextId++),
        ComponentConfig{}
            .with_label(selectedCommit->subject)
            .with_size(ComponentSize{percent(1.0f), children()})
            .with_padding(Padding{
                .top = h720(8), .right = w1280(HEADER_PAD),
                .bottom = h720(4), .left = w1280(HEADER_PAD)})
            .with_custom_text_color(theme::TEXT_PRIMARY)
            .with_font_size(h720(theme::layout::FONT_HERO))
            .with_alignment(TextAlignment::Left)
            .with_roundness(0.0f)
            .with_debug_name("commit_subject"));

    // Commit body (if present)
    if (!repo.commitDetailBody.empty()) {
        div(ctx, mk(scrollContainer.ent(), nextId++),
            ComponentConfig{}
                .with_label(repo.commitDetailBody)
                .with_size(ComponentSize{percent(1.0f), children()})
                .with_padding(Padding{
                    .top = h720(4), .right = w1280(HEADER_PAD),
                    .bottom = h720(8), .left = w1280(HEADER_PAD)})
                .with_custom_text_color(theme::TEXT_PRIMARY)
                .with_font_size(h720(theme::layout::FONT_BODY))
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("commit_body"));
    }

    // === Metadata table ===
    constexpr float META_ROW_H = 22.0f;
    constexpr float META_LABEL_W = 80.0f;
    constexpr float META_PAD = 12.0f;

    auto metaBox = div(ctx, mk(scrollContainer.ent(), nextId++),
        ComponentConfig{}
            .with_size(ComponentSize{percent(1.0f), children()})
            .with_custom_background(theme::SIDEBAR_BG)
            .with_flex_direction(FlexDirection::Column)
            .with_padding(Padding{
                .top = h720(META_PAD), .right = w1280(HEADER_PAD),
                .bottom = h720(META_PAD), .left = w1280(HEADER_PAD)})
            .with_margin(Margin{
                .top = h720(4), .bottom = h720(4),
                .left = w1280(HEADER_PAD), .right = w1280(HEADER_PAD)})
            .with_roundness(0.04f)
            .with_debug_name("commit_meta_box"));

    // Helper lambda for metadata rows
    auto metaRow = [&](const std::string& label, const std::string& value,
                       afterhours::Color valueColor = theme::TEXT_PRIMARY) {
        auto row = div(ctx, mk(metaBox.ent(), nextId++),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), h720(META_ROW_H)})
                .with_flex_direction(FlexDirection::Row)
                .with_align_items(AlignItems::Center)
                .with_roundness(0.0f)
                .with_debug_name("meta_row"));

        div(ctx, mk(row.ent(), 1),
            ComponentConfig{}
                .with_label(label)
                .with_size(ComponentSize{w1280(META_LABEL_W), h720(META_ROW_H)})
                .with_custom_text_color(theme::TEXT_SECONDARY)
                .with_font_size(h720(theme::layout::FONT_CHROME))
                .with_alignment(TextAlignment::Right)
                .with_padding(Padding{
                    .top = h720(0), .right = w1280(8),
                    .bottom = h720(0), .left = w1280(0)})
                .with_roundness(0.0f)
                .with_debug_name("meta_label"));

        div(ctx, mk(row.ent(), 2),
            ComponentConfig{}
                .with_label(value)
                .with_size(ComponentSize{afterhours::ui::expand(), h720(META_ROW_H)})
                .with_custom_text_color(valueColor)
                .with_font_size(h720(theme::layout::FONT_CHROME))
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("meta_value"));
    };

    // Commit hash
    metaRow("Commit:", selectedCommit->hash, theme::TEXT_SECONDARY);

    // Author
    std::string authorStr = selectedCommit->author;
    if (!repo.commitDetailAuthorEmail.empty()) {
        authorStr += " <" + repo.commitDetailAuthorEmail + ">";
    }
    metaRow("Author:", authorStr);

    // Date
    std::string dateStr = selectedCommit->authorDate;
    std::string relTime = cdv::relative_time(selectedCommit->authorDate);
    if (!relTime.empty()) {
        dateStr += " (" + relTime + ")";
    }
    metaRow("Date:", dateStr);

    // Parents
    if (!repo.commitDetailParents.empty()) {
        // Show abbreviated parent hashes
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

    // Decoration badges (branches, tags)
    if (!selectedCommit->decorations.empty()) {
        auto badgeRow = div(ctx, mk(metaBox.ent(), nextId++),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), h720(META_ROW_H)})
                .with_flex_direction(FlexDirection::Row)
                .with_align_items(AlignItems::Center)
                .with_roundness(0.0f)
                .with_debug_name("meta_badge_row"));

        div(ctx, mk(badgeRow.ent(), 1),
            ComponentConfig{}
                .with_label("Refs:")
                .with_size(ComponentSize{w1280(META_LABEL_W), h720(META_ROW_H)})
                .with_custom_text_color(theme::TEXT_SECONDARY)
                .with_alignment(TextAlignment::Right)
                .with_padding(Padding{
                    .top = h720(0), .right = w1280(8),
                    .bottom = h720(0), .left = w1280(0)})
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
                ComponentConfig{}
                    .with_label(badge.label)
                    .with_size(ComponentSize{children(), h720(14)})
                    .with_padding(Padding{
                        .top = h720(1), .right = w1280(4),
                        .bottom = h720(1), .left = w1280(4)})
                    .with_margin(Margin{
                        .top = {}, .bottom = {},
                        .left = {}, .right = w1280(3)})
                    .with_custom_background(bg)
                    .with_custom_text_color(text)
                    .with_roundness(0.15f)
                    .with_alignment(TextAlignment::Center)
                    .with_debug_name("commit_dec_badge"));
        }
    }

    // === Separator ===
    div(ctx, mk(scrollContainer.ent(), nextId++),
        ComponentConfig{}
            .with_size(ComponentSize{percent(1.0f), h720(1)})
            .with_custom_background(theme::BORDER)
            .with_margin(Margin{
                .top = h720(8), .bottom = h720(8),
                .left = {}, .right = {}})
            .with_roundness(0.0f)
            .with_debug_name("commit_sep"));

    // === File change summary ===
    if (!repo.commitDetailDiff.empty()) {
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
                .with_size(ComponentSize{percent(1.0f), h720(22)})
                .with_padding(Padding{
                    .top = h720(4), .right = w1280(HEADER_PAD),
                    .bottom = h720(4), .left = w1280(HEADER_PAD)})
                .with_custom_text_color(theme::TEXT_SECONDARY)
                .with_font_size(h720(theme::layout::FONT_CHROME))
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("files_changed_header"));

        // File summary rows with change bars
        constexpr float FILE_ROW_H = 28.0f;
        constexpr float CHANGE_BAR_W = 60.0f;

        int totalChanges = totalAdd + totalDel;
        if (totalChanges == 0) totalChanges = 1; // avoid div by zero

        for (size_t fi = 0; fi < repo.commitDetailDiff.size(); ++fi) {
            auto& fd = repo.commitDetailDiff[fi];

            // Status badge character
            std::string badge = "M";
            afterhours::Color badgeColor = theme::STATUS_MODIFIED;
            if (fd.isNew) {
                badge = "A";
                badgeColor = theme::STATUS_ADDED;
            } else if (fd.isDeleted) {
                badge = "D";
                badgeColor = theme::STATUS_DELETED;
            } else if (fd.isRenamed) {
                badge = "R";
                badgeColor = theme::STATUS_RENAMED;
            }

            auto fileRow = div(ctx, mk(scrollContainer.ent(), nextId++),
                ComponentConfig{}
                    .with_size(ComponentSize{percent(1.0f), h720(FILE_ROW_H)})
                    .with_flex_direction(FlexDirection::Row)
                    .with_align_items(AlignItems::Center)
                    .with_padding(Padding{
                        .top = h720(2), .right = w1280(HEADER_PAD),
                        .bottom = h720(2), .left = w1280(HEADER_PAD)})
                    .with_custom_background(theme::WINDOW_BG)
                    .with_roundness(0.0f)
                    .with_debug_name("file_summary_row"));

            // Badge
            div(ctx, mk(fileRow.ent(), 1),
                ComponentConfig{}
                    .with_label(badge)
                    .with_size(ComponentSize{w1280(20), h720(FILE_ROW_H)})
                    .with_custom_text_color(badgeColor)
                    .with_font_size(h720(theme::layout::FONT_CHROME))
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
                    .with_size(ComponentSize{afterhours::ui::expand(), h720(FILE_ROW_H)})
                    .with_custom_text_color(theme::TEXT_PRIMARY)
                    .with_font_size(h720(theme::layout::FONT_BODY))
                    .with_alignment(TextAlignment::Left)
                    .with_padding(Padding{
                        .top = h720(0), .right = w1280(8),
                        .bottom = h720(0), .left = w1280(4)})
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
                    .with_size(ComponentSize{w1280(60), h720(FILE_ROW_H)})
                    .with_custom_text_color(theme::TEXT_SECONDARY)
                    .with_font_size(h720(theme::layout::FONT_META))
                    .with_alignment(TextAlignment::Right)
                    .with_roundness(0.0f)
                    .with_debug_name("file_stats"));

            // Change bar (visual proportional bar)
            int fileTotal = fd.additions + fd.deletions;
            float filePct = static_cast<float>(fileTotal) / static_cast<float>(totalChanges);
            float addPct = (fileTotal > 0)
                ? static_cast<float>(fd.additions) / static_cast<float>(fileTotal)
                : 0.0f;

            constexpr float BAR_H = 8.0f;
            float barFillW = CHANGE_BAR_W * std::min(filePct * 5.0f, 1.0f); // Scale up so small changes are visible
            float greenW = barFillW * addPct;
            float redW = barFillW * (1.0f - addPct);

            auto barContainer = div(ctx, mk(fileRow.ent(), 4),
                ComponentConfig{}
                    .with_size(ComponentSize{w1280(CHANGE_BAR_W), h720(BAR_H)})
                    .with_flex_direction(FlexDirection::Row)
                    .with_custom_background(theme::BORDER)
                    .with_roundness(0.15f)
                    .with_debug_name("change_bar"));

            if (greenW > 0.5f) {
                div(ctx, mk(barContainer.ent(), 1),
                    ComponentConfig{}
                        .with_size(ComponentSize{w1280(greenW), h720(BAR_H)})
                        .with_custom_background(theme::STATUS_ADDED)
                        .with_roundness(0.0f)
                        .with_debug_name("bar_green"));
            }
            if (redW > 0.5f) {
                div(ctx, mk(barContainer.ent(), 2),
                    ComponentConfig{}
                        .with_size(ComponentSize{w1280(redW), h720(BAR_H)})
                        .with_custom_background(theme::STATUS_DELETED)
                        .with_roundness(0.0f)
                        .with_debug_name("bar_red"));
            }
        }

        // === Separator before diffs ===
        div(ctx, mk(scrollContainer.ent(), nextId++),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), h720(1)})
                .with_custom_background(theme::BORDER)
                .with_margin(Margin{
                    .top = h720(8), .bottom = h720(8),
                    .left = {}, .right = {}})
                .with_roundness(0.0f)
                .with_debug_name("diff_sep"));

        // === Inline diff for the commit ===
        ui::render_inline_diff(ctx, scrollContainer.ent(),
                               repo.commitDetailDiff,
                               layout.mainContent.width,
                               layout.mainContent.height);
    }
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

        // Determine what to show: diff, commit detail, or empty state
        bool hasSelectedFile = false;
        bool hasSelectedCommit = false;
        bool hasDiffData = false;

        if (!repoEntities.empty()) {
            auto& repo = repoEntities[0].get().get<RepoComponent>();
            hasSelectedFile = !repo.selectedFilePath.empty();
            hasSelectedCommit = !repo.selectedCommitHash.empty();
            hasDiffData = !repo.currentDiff.empty();
        }

        if (hasSelectedFile && hasDiffData) {
            // Render diff view for selected file only
            auto& repo = repoEntities[0].get().get<RepoComponent>();

            // Filter diffs to just the selected file
            std::vector<FileDiff> selectedDiffs;
            for (auto& d : repo.currentDiff) {
                // Match by filename (selectedFilePath may be relative, d.filePath may vary)
                if (d.filePath == repo.selectedFilePath ||
                    d.filePath.ends_with("/" + repo.selectedFilePath) ||
                    repo.selectedFilePath.ends_with("/" + d.filePath) ||
                    repo.selectedFilePath.ends_with(d.filePath)) {
                    selectedDiffs.push_back(d);
                    break;
                }
            }

            if (!selectedDiffs.empty()) {
                // Render diff as child of mainBg using percent() sizing.
                // (Previously used a separate absolute element as workaround
                // for framework bug — now fixed upstream.)
                ui::render_inline_diff(ctx, mainBg.ent(), selectedDiffs,
                                       0, 0);  // 0 = use percent(1.0f)
            } else {
                // File selected but no diff found for it (might be untracked/new)
                // Center the message both horizontally and vertically
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
                        .with_font_size(h720(theme::layout::FONT_HEADING))
                        .with_transparent_bg()
                        .with_roundness(0.0f)
                        .with_debug_name("no_diff_msg"));
            }
        } else if (hasSelectedCommit) {
            // === Commit Detail View (T035) ===
            auto& repo = repoEntities[0].get().get<RepoComponent>();
            render_commit_detail(ctx, mainBg.ent(), repo, layout);
        } else {
            // Empty state — clean, Apple Notes-style welcome
            auto emptyContainer = div(ctx, mk(mainBg.ent(), 3060),
                ComponentConfig{}
                    .with_size(ComponentSize{percent(1.0f), percent(1.0f)})
                    .with_flex_direction(FlexDirection::Column)
                    .with_justify_content(JustifyContent::Center)
                    .with_align_items(AlignItems::Center)
                    .with_transparent_bg()
                    .with_roundness(0.0f)
                    .with_debug_name("empty_state"));

            // Large decorative icon
            div(ctx, mk(emptyContainer.ent(), 3005),
                ComponentConfig{}
                    .with_label("\xe2\x97\x87")  // diamond symbol
                    .with_size(ComponentSize{children(), children()})
                    .with_font_size(h720(32))
                    .with_padding(Padding{
                        .top = h720(0), .right = w1280(0),
                        .bottom = h720(16), .left = w1280(0)})
                    .with_transparent_bg()
                    .with_custom_text_color(afterhours::Color{60, 60, 60, 255})
                    .with_alignment(TextAlignment::Center)
                    .with_roundness(0.0f)
                    .with_debug_name("empty_icon"));

            // Primary hint
            div(ctx, mk(emptyContainer.ent(), 3010),
                ComponentConfig{}
                    .with_label("Select a file or commit")
                    .with_size(ComponentSize{children(), children()})
                    .with_font_size(h720(theme::layout::FONT_HERO))
                    .with_padding(Padding{
                        .top = h720(0), .right = w1280(8),
                        .bottom = h720(6), .left = w1280(8)})
                    .with_transparent_bg()
                    .with_custom_text_color(theme::TEXT_SECONDARY)
                    .with_alignment(TextAlignment::Center)
                    .with_roundness(0.0f)
                    .with_debug_name("empty_hint_1"));

            // Secondary hint
            div(ctx, mk(emptyContainer.ent(), 3020),
                ComponentConfig{}
                    .with_label("to view changes")
                    .with_size(ComponentSize{children(), children()})
                    .with_font_size(h720(theme::layout::FONT_CHROME))
                    .with_padding(Padding{
                        .top = h720(0), .right = w1280(8),
                        .bottom = h720(0), .left = w1280(8)})
                    .with_transparent_bg()
                    .with_custom_text_color(afterhours::Color{80, 80, 80, 255})
                    .with_alignment(TextAlignment::Center)
                    .with_roundness(0.0f)
                    .with_debug_name("empty_hint_2"));
        }

        // === Command Log Panel (at bottom of main content area) ===
        if (layout.commandLogVisible) {
            render_command_log(ctx, uiRoot, layout);
        }

        // === Vertical divider between sidebar and main content ===
        if (layout.sidebarVisible) {
            // Divider spans the full content height (main + command log if visible)
            float dividerH = layout.mainContent.height;
            if (layout.commandLogVisible) {
                dividerH += layout.commandLog.height;
            }
            // Divider also spans the toolbar row above for visual continuity
            float dividerY = layout.toolbar.y;
            float fullDividerH = dividerH + (layout.mainContent.y - layout.toolbar.y);
            auto vDivider = div(ctx, mk(uiRoot, 3100),
                ComponentConfig{}
                    .with_size(ComponentSize{pixels(2), pixels(fullDividerH)})
                    .with_absolute_position()
                    .with_translate(layout.sidebar.width, dividerY)
                    .with_custom_background(theme::BORDER)
                    .with_roundness(0.0f)
                    .with_debug_name("sidebar_divider"));

            // Make vertical divider draggable (adjusts sidebar width)
            vDivider.ent().addComponentIfMissing<HasDragListener>(
                [](Entity& /*e*/) {});
            auto& vDrag = vDivider.ent().get<HasDragListener>();
            if (vDrag.down) {
                auto mousePos = afterhours::graphics::get_mouse_position();
                float mouseX = static_cast<float>(mousePos.x);
                float sw = static_cast<float>(afterhours::graphics::get_screen_width());
                float maxW = sw * 0.5f;
                // Convert mouse position (screen pixels) back to 1280-baseline
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
    }
};

}  // namespace ecs
