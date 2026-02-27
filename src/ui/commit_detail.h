#pragma once

#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "../git/git_parser.h"
#include "../git/git_runner.h"
#include "../util/git_helpers.h"
#include "../ecs/ui_imports.h"
#include "diff_renderer.h"

namespace ecs {

// ---- Commit Detail Helpers (T035) ----
namespace commit_detail_view {

using git_helpers::DecorationType;
using git_helpers::Decoration;
using git_helpers::parse_decorations;

inline std::string relative_time(const std::string& isoDate) {
    return git_helpers::relative_time(isoDate, /*suffix=*/true);
}

struct CommitInfo {
    std::string subject;
    std::string body;
    std::string author;
    std::string authorEmail;
    std::string date;
    std::string parents;
    std::string decorations;
};

inline CommitInfo parse_commit_info(const std::string& output) {
    CommitInfo info;
    std::vector<std::string> fields;
    size_t start = 0;
    for (size_t i = 0; i < output.size(); ++i) {
        if (output[i] == '\0') {
            fields.push_back(output.substr(start, i - start));
            start = i + 1;
        }
    }
    fields.push_back(output.substr(start));

    if (!fields.empty()) {
        auto& last = fields.back();
        while (!last.empty() && (last.back() == '\n' || last.back() == '\r'))
            last.pop_back();
    }

    if (fields.size() > 0) info.subject = fields[0];
    if (fields.size() > 1) {
        info.body = fields[1];
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

inline void render_commit_detail(afterhours::ui::UIContext<InputAction>& ctx,
                                  Entity& parent,
                                  RepoComponent& repo,
                                  CommitDetailCache& detailCache,
                                  LayoutComponent& layout) {
    namespace cdv = commit_detail_view;

    const CommitEntry* selectedCommit = nullptr;
    for (auto& c : repo.commitLog) {
        if (c.hash == repo.selectedCommitHash) {
            selectedCommit = &c;
            break;
        }
    }

    if (!selectedCommit) {
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
            detailCache.cachedCommitHash.clear();
        }
        return;
    }

    bool commitJustChanged = (detailCache.cachedCommitHash != repo.selectedCommitHash);
    if (commitJustChanged) {
        auto diffResult = git::git_show(repo.repoPath, repo.selectedCommitHash);
        auto infoResult = git::git_show_commit_info(repo.repoPath, repo.selectedCommitHash);

        if (diffResult.success()) {
            detailCache.commitDetailDiff = git::parse_diff(diffResult.stdout_str());
        } else {
            detailCache.commitDetailDiff.clear();
        }

        if (infoResult.success()) {
            auto info = cdv::parse_commit_info(infoResult.stdout_str());
            detailCache.commitDetailBody = info.body;
            detailCache.commitDetailAuthorEmail = info.authorEmail;
            detailCache.commitDetailParents = info.parents;
        } else {
            detailCache.commitDetailBody.clear();
            detailCache.commitDetailAuthorEmail.clear();
            detailCache.commitDetailParents.clear();
        }

        detailCache.cachedCommitHash = repo.selectedCommitHash;
    }

    int nextId = 3050;
    constexpr float PAD = 16.0f;
    constexpr float LABEL_W = 70.0f;
    float contentW = layout.mainContent.width;

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
        detailCache.cachedCommitHash.clear();
        return;
    }

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

    if (!detailCache.commitDetailBody.empty()) {
        div(ctx, mk(scrollContainer.ent(), nextId++),
            ComponentConfig{}
                .with_label(detailCache.commitDetailBody)
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
    if (!detailCache.commitDetailAuthorEmail.empty()) {
        authorStr += " <" + detailCache.commitDetailAuthorEmail + ">";
    }
    metaRow("Author:", authorStr);

    std::string dateStr = selectedCommit->authorDate;
    std::string relTime = cdv::relative_time(selectedCommit->authorDate);
    if (!relTime.empty()) {
        dateStr += " (" + relTime + ")";
    }
    metaRow("Date:", dateStr);

    if (!detailCache.commitDetailParents.empty()) {
        std::string parentDisplay;
        std::string remaining = detailCache.commitDetailParents;
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

    div(ctx, mk(scrollContainer.ent(), nextId++),
        ComponentConfig{}
            .with_size(ComponentSize{percent(1.0f), pixels(1)})
            .with_custom_background(theme::BORDER)
            .with_margin(Margin{
                .top = pixels(8), .bottom = pixels(8)})
            .with_roundness(0.0f)
            .with_debug_name("commit_sep"));

    if (detailCache.commitDetailDiff.empty()) {
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
        for (auto& d : detailCache.commitDetailDiff) {
            totalAdd += d.additions;
            totalDel += d.deletions;
        }

        std::string summaryLabel = "FILES CHANGED (" +
            std::to_string(detailCache.commitDetailDiff.size()) + " file" +
            (detailCache.commitDetailDiff.size() != 1 ? "s" : "") +
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

        constexpr float STATS_W = 55.0f;
        constexpr float BAR_W = 50.0f;
        constexpr float BADGE_W = 20.0f;

        int totalChanges = totalAdd + totalDel;
        if (totalChanges == 0) totalChanges = 1;

        float fileNameW = contentW - PAD * 2 - BADGE_W - STATS_W - BAR_W - 8.0f * 3;
        if (fileNameW < 80.0f) fileNameW = 80.0f;

        for (size_t fi = 0; fi < detailCache.commitDetailDiff.size(); ++fi) {
            auto& fd = detailCache.commitDetailDiff[fi];

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

        div(ctx, mk(scrollContainer.ent(), nextId++),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(1)})
                .with_custom_background(theme::BORDER)
                .with_margin(Margin{
                    .top = pixels(8), .bottom = pixels(8)})
                .with_roundness(0.0f)
                .with_debug_name("diff_sep"));

        ui::render_inline_diff(ctx, scrollContainer.ent(),
                               detailCache.commitDetailDiff,
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

    constexpr std::uintmax_t MAX_SIZE = 1 * 1024 * 1024;
    if (fileSize > MAX_SIZE) return std::nullopt;

    std::ifstream ifs(fullPath, std::ios::binary);
    if (!ifs) return std::nullopt;

    std::string contents((std::istreambuf_iterator<char>(ifs)),
                          std::istreambuf_iterator<char>());

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

} // namespace ecs
