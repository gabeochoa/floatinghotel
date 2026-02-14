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

namespace diff_detail {

// Diff-specific colors from the mockup spec
constexpr afterhours::Color DIFF_ADD_BG      = {13, 40, 24, 255};   // #0D2818
constexpr afterhours::Color DIFF_DEL_BG      = {61, 17, 23, 255};   // #3D1117
constexpr afterhours::Color HUNK_HEADER_BG   = {26, 35, 50, 255};   // #1A2332
constexpr afterhours::Color GUTTER_BG        = {26, 26, 26, 255};   // #1A1A1A
constexpr afterhours::Color GUTTER_BORDER    = {58, 58, 58, 255};   // #3A3A3A
constexpr afterhours::Color GUTTER_ADD_BG    = {13, 51, 23, 255};   // #0D3317
constexpr afterhours::Color GUTTER_DEL_BG    = {77, 17, 23, 255};   // #4D1117

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

// Render the diff summary header showing total file/line change stats.
inline void render_diff_header(UIContext<InputAction>& ctx,
                                Entity& parent,
                                const std::vector<ecs::FileDiff>& diffs,
                                int baseId) {
    int totalAdditions = 0, totalDeletions = 0;
    for (auto& d : diffs) {
        totalAdditions += d.additions;
        totalDeletions += d.deletions;
    }

    std::string stats = std::to_string(diffs.size()) + " file"
        + (diffs.size() != 1 ? "s" : "") + " changed, +"
        + std::to_string(totalAdditions) + " -"
        + std::to_string(totalDeletions);

    div(ctx, mk(parent, baseId),
        ComponentConfig{}
            .with_size(ComponentSize{percent(1.0f),
                                     pixels(diff_detail::DIFF_HEADER_H)})
            .with_padding(Padding{
                .top = pixels(4), .right = pixels(8),
                .bottom = pixels(4), .left = pixels(8)})
            .with_custom_text_color(theme::TEXT_SECONDARY)
            .with_custom_background(theme::PANEL_BG)
            .with_label(stats)
            .with_alignment(TextAlignment::Left)
            .with_roundness(0.0f)
            .with_debug_name("diff_stats_header"));
}

// Render a single diff line (one row: old line# | new line# | content).
inline void render_diff_line(UIContext<InputAction>& ctx,
                              Entity& parent,
                              int id,
                              const std::string& line,
                              int& oldLine,
                              int& newLine) {
    afterhours::Color bgColor, textColor, gutterBg;
    std::string oldNum, newNum;
    std::string content;

    // Determine line type from prefix character
    char prefix = line.empty() ? ' ' : line[0];
    content = line.size() > 1 ? line.substr(1) : "";

    if (prefix == '+') {
        bgColor   = diff_detail::DIFF_ADD_BG;
        textColor = theme::DIFF_ADD_TEXT;
        gutterBg  = diff_detail::GUTTER_ADD_BG;
        newNum    = std::to_string(newLine++);
    } else if (prefix == '-') {
        bgColor   = diff_detail::DIFF_DEL_BG;
        textColor = theme::DIFF_DEL_TEXT;
        gutterBg  = diff_detail::GUTTER_DEL_BG;
        oldNum    = std::to_string(oldLine++);
    } else {
        bgColor   = theme::PANEL_BG;
        textColor = theme::TEXT_PRIMARY;
        gutterBg  = diff_detail::GUTTER_BG;
        oldNum    = std::to_string(oldLine++);
        newNum    = std::to_string(newLine++);
    }

    // Row container
    auto lineRow = div(ctx, mk(parent, id),
        ComponentConfig{}
            .with_size(ComponentSize{percent(1.0f),
                                     pixels(diff_detail::LINE_HEIGHT)})
            .with_custom_background(bgColor)
            .with_flex_direction(FlexDirection::Row)
            .with_align_items(AlignItems::Center)
            .with_roundness(0.0f)
            .with_debug_name("diff_line"));

    // Old line number gutter
    div(ctx, mk(lineRow.ent(), id * 4 + 1),
        ComponentConfig{}
            .with_size(ComponentSize{pixels(diff_detail::GUTTER_WIDTH),
                                     pixels(diff_detail::LINE_HEIGHT)})
            .with_custom_background(gutterBg)
            .with_custom_text_color(theme::TEXT_SECONDARY)
            .with_label(oldNum)
            .with_alignment(TextAlignment::Right)
            .with_padding(Padding{
                .top = pixels(0), .right = pixels(4),
                .bottom = pixels(0), .left = pixels(0)})
            .with_roundness(0.0f)
            .with_debug_name("old_ln"));

    // New line number gutter
    div(ctx, mk(lineRow.ent(), id * 4 + 2),
        ComponentConfig{}
            .with_size(ComponentSize{pixels(diff_detail::GUTTER_WIDTH),
                                     pixels(diff_detail::LINE_HEIGHT)})
            .with_custom_background(gutterBg)
            .with_custom_text_color(theme::TEXT_SECONDARY)
            .with_label(newNum)
            .with_alignment(TextAlignment::Right)
            .with_padding(Padding{
                .top = pixels(0), .right = pixels(4),
                .bottom = pixels(0), .left = pixels(0)})
            .with_roundness(0.0f)
            .with_debug_name("new_ln"));

    // Line content
    div(ctx, mk(lineRow.ent(), id * 4 + 3),
        ComponentConfig{}
            .with_size(ComponentSize{percent(1.0f),
                                     pixels(diff_detail::LINE_HEIGHT)})
            .with_custom_text_color(textColor)
            .with_label(content)
            .with_alignment(TextAlignment::Left)
            .with_padding(Padding{
                .top = pixels(0), .right = pixels(0),
                .bottom = pixels(0), .left = pixels(diff_detail::CODE_PAD_LEFT)})
            .with_roundness(0.0f)
            .with_debug_name("line_content"));
}

// Render a single hunk with its header (including stage/discard buttons) and all diff lines.
inline void render_hunk(UIContext<InputAction>& ctx,
                         Entity& parent,
                         const ecs::FileDiff& fileDiff,
                         const ecs::DiffHunk& hunk,
                         int& nextId) {
    // Hunk header bar: row with @@ text, spacer, [Stage Hunk], [Discard]
    int hunkHeaderId = nextId++;
    auto hunkHeader = div(ctx, mk(parent, hunkHeaderId),
        ComponentConfig{}
            .with_size(ComponentSize{percent(1.0f),
                                     pixels(diff_detail::HUNK_HEADER_H + 4)})
            .with_custom_background(diff_detail::HUNK_HEADER_BG)
            .with_flex_direction(FlexDirection::Row)
            .with_align_items(AlignItems::Center)
            .with_roundness(0.0f)
            .with_debug_name("hunk_header"));

    // Hunk range info text
    div(ctx, mk(hunkHeader.ent(), hunkHeaderId * 6 + 1),
        ComponentConfig{}
            .with_label(hunk.header)
            .with_size(ComponentSize{percent(1.0f),
                                     pixels(diff_detail::HUNK_HEADER_H)})
            .with_custom_text_color(theme::DIFF_HUNK_HEADER)
            .with_alignment(TextAlignment::Left)
            .with_padding(Padding{
                .top = pixels(2), .right = pixels(8),
                .bottom = pixels(2), .left = pixels(8)})
            .with_roundness(0.0f)
            .with_debug_name("hunk_range"));

    // [Stage Hunk] button
    auto stageBtn = button(ctx, mk(hunkHeader.ent(), hunkHeaderId * 6 + 2),
        ComponentConfig{}
            .with_label("Stage Hunk")
            .with_size(ComponentSize{children(), pixels(20)})
            .with_custom_background(theme::BUTTON_PRIMARY)
            .with_custom_text_color(theme::STATUS_BAR_TEXT)
            .with_padding(Padding{
                .top = pixels(2), .right = pixels(8),
                .bottom = pixels(2), .left = pixels(8)})
            .with_margin(Margin{
                .top = pixels(0), .right = pixels(4),
                .bottom = pixels(0), .left = pixels(0)})
            .with_roundness(0.04f)
            .with_debug_name("stage_hunk_btn"));

    if (stageBtn) {
        auto rEntities = afterhours::EntityQuery({.force_merge = true})
                             .whereHasComponent<ecs::RepoComponent>()
                             .gen();
        if (!rEntities.empty()) {
            auto& repo = rEntities[0].get().get<ecs::RepoComponent>();
            git::stage_hunk(repo.repoPath, fileDiff, hunk);
            repo.refreshRequested = true;
        }
    }

    // [Discard] button
    auto discardBtn = button(ctx, mk(hunkHeader.ent(), hunkHeaderId * 6 + 3),
        ComponentConfig{}
            .with_label("Discard")
            .with_size(ComponentSize{children(), pixels(20)})
            .with_custom_background(theme::STATUS_DELETED)
            .with_custom_text_color(theme::STATUS_BAR_TEXT)
            .with_padding(Padding{
                .top = pixels(2), .right = pixels(8),
                .bottom = pixels(2), .left = pixels(8)})
            .with_margin(Margin{
                .top = pixels(0), .right = pixels(8),
                .bottom = pixels(0), .left = pixels(0)})
            .with_roundness(0.04f)
            .with_debug_name("discard_hunk_btn"));

    if (discardBtn) {
        // TODO(T044): Show confirmation dialog before discarding.
        // For now, discard directly.
        auto rEntities = afterhours::EntityQuery({.force_merge = true})
                             .whereHasComponent<ecs::RepoComponent>()
                             .gen();
        if (!rEntities.empty()) {
            auto& repo = rEntities[0].get().get<ecs::RepoComponent>();
            git::discard_hunk(repo.repoPath, fileDiff, hunk);
            repo.refreshRequested = true;
        }
    }

    // Render each line in the hunk
    int oldLine = hunk.oldStart;
    int newLine = hunk.newStart;

    for (auto& line : hunk.lines) {
        render_diff_line(ctx, parent, nextId++, line, oldLine, newLine);
    }
}

// Render the complete inline diff view for all file diffs.
// This is the main entry point called by MainContentSystem.
inline void render_inline_diff(UIContext<InputAction>& ctx,
                                Entity& parent,
                                const std::vector<ecs::FileDiff>& diffs,
                                float /*width*/, float /*height*/) {
    int nextId = diff_detail::BASE_ID;

    // Scrollable container for all diff content
    auto scrollContainer = div(ctx, mk(parent, nextId++),
        ComponentConfig{}
            .with_size(ComponentSize{percent(1.0f), percent(1.0f)})
            .with_overflow(Overflow::Scroll, Axis::Y)
            .with_flex_direction(FlexDirection::Column)
            .with_custom_background(theme::PANEL_BG)
            .with_roundness(0.0f)
            .with_debug_name("diff_scroll"));

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
                .with_size(ComponentSize{percent(1.0f),
                                         pixels(diff_detail::FILE_HEADER_H)})
                .with_custom_background(theme::BORDER)
                .with_custom_text_color(theme::TEXT_PRIMARY)
                .with_label(fileLabel)
                .with_alignment(TextAlignment::Left)
                .with_padding(Padding{
                    .top = pixels(4), .right = pixels(8),
                    .bottom = pixels(4), .left = pixels(8)})
                .with_roundness(0.0f)
                .with_debug_name("file_header"));

        // Binary files: just show the header, no hunks
        if (fileDiff.isBinary) {
            div(ctx, mk(scrollContainer.ent(), nextId++),
                ComponentConfig{}
                    .with_size(ComponentSize{percent(1.0f), pixels(24)})
                    .with_custom_background(theme::PANEL_BG)
                    .with_custom_text_color(theme::TEXT_SECONDARY)
                    .with_label("Binary file not shown")
                    .with_alignment(TextAlignment::Center)
                    .with_padding(Padding{
                        .top = pixels(4), .right = pixels(8),
                        .bottom = pixels(4), .left = pixels(8)})
                    .with_roundness(0.0f)
                    .with_debug_name("binary_notice"));
            continue;
        }

        // Render each hunk
        for (auto& hunk : fileDiff.hunks) {
            render_hunk(ctx, scrollContainer.ent(), fileDiff, hunk, nextId);
        }

        // Spacer between files
        if (&fileDiff != &diffs.back()) {
            div(ctx, mk(scrollContainer.ent(), nextId++),
                ComponentConfig{}
                    .with_size(ComponentSize{percent(1.0f), pixels(8)})
                    .with_custom_background(theme::PANEL_BG)
                    .with_roundness(0.0f)
                    .with_debug_name("file_spacer"));
        }
    }
}

} // namespace ui
