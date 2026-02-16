#pragma once

#include <string>
#include <vector>

#include "../ecs/components.h"
#include "../git/git_commands.h"
#include "../input_mapping.h"
#include "../rl.h"
#include "../ui_context.h"
#include "theme.h"

namespace ui {

using afterhours::Entity;
using afterhours::ui::UIContext;
using afterhours::ui::imm::ComponentConfig;
using afterhours::ui::imm::button;
using afterhours::ui::imm::div;
using afterhours::ui::imm::mk;
using afterhours::ui::Axis;
using afterhours::ui::AlignItems;
using afterhours::ui::children;
using afterhours::ui::ComponentSize;
using afterhours::ui::FlexDirection;
using afterhours::ui::JustifyContent;
using afterhours::ui::Overflow;
using afterhours::ui::Padding;
using afterhours::ui::Margin;
using afterhours::ui::TextAlignment;
using afterhours::ui::percent;
using afterhours::ui::pixels;
using afterhours::ui::h720;
using afterhours::ui::w1280;

namespace diff_detail {

// Diff colors — all defined in theme.h, aliased here for brevity
const auto& DIFF_ADD_BG    = theme::DIFF_ADD_BG;
const auto& DIFF_DEL_BG    = theme::DIFF_DEL_BG;
const auto& HUNK_HEADER_BG = theme::DIFF_HUNK_BG;
const auto& GUTTER_BG      = theme::GUTTER_BG;
const auto& GUTTER_BORDER  = theme::GUTTER_BORDER;
const auto& GUTTER_ADD_BG  = theme::GUTTER_ADD_BG;
const auto& GUTTER_DEL_BG  = theme::GUTTER_DEL_BG;

constexpr float LINE_HEIGHT   = 20.0f;
constexpr float GUTTER_WIDTH  = 40.0f;
constexpr float HUNK_HEADER_H = 24.0f;
constexpr float FILE_HEADER_H = 28.0f;
constexpr float DIFF_HEADER_H = 28.0f;
constexpr float CODE_PAD_LEFT = 8.0f;

// ID ranges for diff elements to avoid collision with other systems.
// MainContentSystem uses 3000-3999. We use 4000-59999.
constexpr int BASE_ID = 4000;

} // namespace diff_detail

// Render a single diff line as a composed label.
// Format: "  OldLn  NewLn  content"
inline void render_diff_line(UIContext<InputAction>& ctx,
                              Entity& parent,
                              int id,
                              const std::string& line,
                              int& oldLine,
                              int& newLine,
                              float contentWidth = 0) {
    afterhours::Color bgColor, textColor;
    std::string oldNum, newNum;
    std::string content;

    // Determine line type from prefix character
    char prefix = line.empty() ? ' ' : line[0];
    content = line.size() > 1 ? line.substr(1) : "";

    if (prefix == '+') {
        bgColor   = diff_detail::DIFF_ADD_BG;
        textColor = theme::DIFF_ADD_TEXT;
        newNum    = std::to_string(newLine++);
    } else if (prefix == '-') {
        bgColor   = diff_detail::DIFF_DEL_BG;
        textColor = theme::DIFF_DEL_TEXT;
        oldNum    = std::to_string(oldLine++);
    } else {
        bgColor   = theme::PANEL_BG;
        textColor = theme::TEXT_PRIMARY;
        oldNum    = std::to_string(oldLine++);
        newNum    = std::to_string(newLine++);
    }

    // Right-pad line numbers for alignment
    auto padNum = [](const std::string& n, size_t width) -> std::string {
        if (n.empty()) return std::string(width, ' ');
        if (n.size() >= width) return n;
        return std::string(width - n.size(), ' ') + n;
    };

    std::string label = padNum(oldNum, 5) + " " + padNum(newNum, 5) + "  " + content;

    auto w = contentWidth > 0 ? pixels(contentWidth) : percent(1.0f);
    div(ctx, mk(parent, id),
        ComponentConfig{}
            .with_size(ComponentSize{w, h720(diff_detail::LINE_HEIGHT)})
            .with_custom_background(bgColor)
            .with_custom_text_color(textColor)
            .with_label(label)
            .with_font_size(pixels(theme::layout::FONT_CODE))
            .with_alignment(TextAlignment::Left)
            .with_padding(Padding{
                .top = h720(0), .right = w1280(0),
                .bottom = h720(0), .left = w1280(diff_detail::CODE_PAD_LEFT)})
            .with_roundness(0.0f)
            .with_debug_name("diff_line"));
}

// Render a single hunk with its header and all diff lines.
inline void render_hunk(UIContext<InputAction>& ctx,
                         Entity& parent,
                         const ecs::FileDiff& fileDiff,
                         const ecs::DiffHunk& hunk,
                         int& nextId,
                         float contentWidth = 0) {
    (void)fileDiff; // Kept for future stage/discard functionality

    auto w = contentWidth > 0 ? pixels(contentWidth) : percent(1.0f);

    // Hunk header as single label (avoids Row layout bug)
    int hunkHeaderId = nextId++;
    div(ctx, mk(parent, hunkHeaderId),
        ComponentConfig{}
            .with_label(hunk.header)
            .with_size(ComponentSize{w, h720(diff_detail::HUNK_HEADER_H)})
            .with_custom_background(diff_detail::HUNK_HEADER_BG)
            .with_custom_text_color(theme::DIFF_HUNK_HEADER)
            .with_font_size(pixels(theme::layout::FONT_META))
            .with_alignment(TextAlignment::Left)
            .with_padding(Padding{
                .top = h720(4), .right = w1280(12),
                .bottom = h720(4), .left = w1280(12)})
            .with_roundness(0.0f)
            .with_debug_name("hunk_header"));

    // Render each line in the hunk
    int oldLine = hunk.oldStart;
    int newLine = hunk.newStart;

    for (auto& line : hunk.lines) {
        render_diff_line(ctx, parent, nextId++, line, oldLine, newLine, contentWidth);
    }
}

// Render the complete inline diff view for all file diffs.
// This is the main entry point called by MainContentSystem.
inline void render_inline_diff(UIContext<InputAction>& ctx,
                                Entity& parent,
                                const std::vector<ecs::FileDiff>& diffs,
                                float contentWidth, float contentHeight) {
    int nextId = diff_detail::BASE_ID;

    // Size the scroll container to fill available space
    auto w = contentWidth > 0 ? pixels(contentWidth) : percent(1.0f);
    auto h = contentHeight > 0 ? pixels(contentHeight - diff_detail::DIFF_HEADER_H)
                                : percent(1.0f);

    // Scrollable container for all diff content
    auto scrollContainer = div(ctx, mk(parent, nextId++),
        ComponentConfig{}
            .with_size(ComponentSize{w, h})
            .with_overflow(Overflow::Scroll, Axis::Y)
            .with_flex_direction(FlexDirection::Column)
            .with_custom_background(theme::PANEL_BG)
            .with_roundness(0.0f)
            .with_debug_name("diff_scroll"));

    // Stats summary header inside scroll
    {
        int totalAdditions = 0, totalDeletions = 0;
        for (auto& d : diffs) {
            totalAdditions += d.additions;
            totalDeletions += d.deletions;
        }
        std::string stats = std::to_string(diffs.size()) + " file"
            + (diffs.size() != 1 ? "s" : "") + " changed  +"
            + std::to_string(totalAdditions) + "  -"
            + std::to_string(totalDeletions);

        div(ctx, mk(scrollContainer.ent(), nextId++),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), h720(diff_detail::DIFF_HEADER_H)})
                .with_padding(Padding{
                    .top = h720(6), .right = w1280(12),
                    .bottom = h720(4), .left = w1280(12)})
                .with_custom_text_color(theme::TEXT_PRIMARY)
                .with_custom_background(afterhours::Color{35, 35, 38, 255})
                .with_label(stats)
                .with_font_size(pixels(theme::layout::FONT_META))
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("diff_stats_header"));
    }

    for (auto& fileDiff : diffs) {
        // File header bar
        std::string fileLabel = fileDiff.filePath;
        if (fileDiff.isRenamed && !fileDiff.oldPath.empty()) {
            fileLabel = fileDiff.oldPath + " -> " + fileDiff.filePath;
        }

        std::string statsLabel;
        if (fileDiff.additions > 0) {
            statsLabel += "+" + std::to_string(fileDiff.additions);
        }
        if (fileDiff.deletions > 0) {
            if (!statsLabel.empty()) statsLabel += " ";
            statsLabel += "-" + std::to_string(fileDiff.deletions);
        }
        if (!statsLabel.empty()) {
            fileLabel += "  " + statsLabel;
        }

        if (fileDiff.isNew) {
            fileLabel += "  (new file)";
        } else if (fileDiff.isDeleted) {
            fileLabel += "  (deleted)";
        } else if (fileDiff.isBinary) {
            fileLabel += "  (binary)";
        }

        div(ctx, mk(scrollContainer.ent(), nextId++),
            ComponentConfig{}
                .with_size(ComponentSize{w, h720(diff_detail::FILE_HEADER_H)})
                .with_custom_background(theme::SIDEBAR_BG)
                .with_custom_text_color(theme::TEXT_PRIMARY)
                .with_label(fileLabel)
                .with_font_size(pixels(theme::layout::FONT_BODY))
                .with_alignment(TextAlignment::Left)
                .with_padding(Padding{
                    .top = h720(8), .right = w1280(16),
                    .bottom = h720(8), .left = w1280(16)})
                .with_border_bottom(theme::BORDER)
                .with_roundness(0.0f)
                .with_debug_name("file_header"));

        // Binary files: just show the header, no hunks
        if (fileDiff.isBinary) {
            div(ctx, mk(scrollContainer.ent(), nextId++),
                ComponentConfig{}
                    .with_size(ComponentSize{w, h720(24)})
                    .with_custom_background(theme::PANEL_BG)
                    .with_custom_text_color(theme::TEXT_SECONDARY)
                    .with_label("Binary file not shown")
                    .with_font_size(pixels(theme::layout::FONT_META))
                    .with_alignment(TextAlignment::Center)
                    .with_padding(Padding{
                        .top = h720(4), .right = w1280(8),
                        .bottom = h720(4), .left = w1280(8)})
                    .with_roundness(0.0f)
                    .with_debug_name("binary_notice"));
            continue;
        }

        // Render each hunk (passing contentWidth for proper sizing)
        for (auto& hunk : fileDiff.hunks) {
            render_hunk(ctx, scrollContainer.ent(), fileDiff, hunk, nextId,
                        contentWidth);
        }

        // Spacer between files
        if (&fileDiff != &diffs.back()) {
            div(ctx, mk(scrollContainer.ent(), nextId++),
                ComponentConfig{}
                    .with_size(ComponentSize{w, h720(8)})
                    .with_custom_background(theme::PANEL_BG)
                    .with_roundness(0.0f)
                    .with_debug_name("file_spacer"));
        }
    }
}

} // namespace ui
