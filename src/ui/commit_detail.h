#pragma once

#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <vector>

#include "../git/git_parser.h"
#include "../git/git_runner.h"
#include "../util/git_helpers.h"
#include "../util/visible_rows.h"
#include "../util/file_content.h"
#include "../ecs/ui_imports.h"
#include "diff_renderer.h"

namespace ecs {

// ---- Commit Detail Helpers (T035) ----
namespace commit_detail_view {

using git_helpers::DecorationType;
using git_helpers::Decoration;
using git_helpers::parse_decorations;

struct CommitInfo {
    CommitEntry entry;
    std::string body;
    std::string authorEmail;
    std::string parents;
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

    if (!fields.empty()) info.entry.subject = fields[0];
    if (fields.size() > 2) info.entry.author = fields[2];
    if (fields.size() > 4) info.entry.authorDate = fields[4];
    if (fields.size() > 6) info.entry.decorations = fields[6];
    if (fields.size() > 1) {
        info.body = fields[1];
        while (!info.body.empty() && (info.body.back() == '\n' || info.body.back() == '\r'))
            info.body.pop_back();
    }
    if (fields.size() > 3) info.authorEmail = fields[3];
    if (fields.size() > 5) info.parents = fields[5];
    return info;
}

} // namespace commit_detail_view

inline void render_commit_detail(afterhours::ui::UIContext<InputAction>& ctx,
                                  Entity& parent,
                                  RepoComponent& repo,
                                  CommitDetailCache& detailCache,
                                  LayoutComponent& layout,
                                  ReviewComponent* review = nullptr) {
    namespace cdv = commit_detail_view;

    bool commitJustChanged = detailCache.cachedCommitHash != repo.selectedCommitHash || detailCache.cachedRepoPath != repo.repoPath;
    if (commitJustChanged) {
        repo.diffTargetFile.clear();
        repo.diffTargetFrames = 0;
        detailCache.commitDetailError.clear();
        detailCache.commitDetailDiff.clear();
        detailCache.commitDetailBody.clear();
        detailCache.messageExpanded = false;
        detailCache.messageLines.clear();
        detailCache.commitDetailAuthorEmail.clear();
        detailCache.commitDetailParents.clear();
        detailCache.entry = {};
        detailCache.entry.hash = repo.selectedCommitHash;
        detailCache.entry.shortHash = repo.selectedCommitHash.substr(0, 7);
        detailCache.entry.subject = detailCache.entry.shortHash;
        for (const auto* entries : {&repo.commitLog, &repo.fileHistoryEntries, &repo.commitSearchEntries})
            for (const auto& entry : *entries)
                if (entry.hash == repo.selectedCommitHash) detailCache.entry = entry;
        std::vector<std::string> diffArgs{"show", repo.selectedCommitHash, "--format="};
        diffArgs.push_back("--unified=" + std::to_string(repo.diffContext));
        if (repo.ignoreWhitespace) diffArgs.push_back("--ignore-all-space");
        detailCache.patchFuture = git::git_run_async(repo.repoPath, diffArgs);
        detailCache.infoFuture = git::git_run_async(repo.repoPath, {"show", repo.selectedCommitHash, "--no-patch",
            "--format=%s%x00%b%x00%an%x00%ae%x00%aI%x00%P%x00%D"});
        detailCache.cachedCommitHash = repo.selectedCommitHash;
        detailCache.cachedRepoPath = repo.repoPath;
    }
    if (detailCache.patchFuture.valid() && detailCache.patchFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto diffResult = detailCache.patchFuture.get();
        detailCache.patchFuture = {};
        if (diffResult.success()) {
            detailCache.commitDetailDiff = git::parse_diff(diffResult.stdout_str());
        } else {
            detailCache.commitDetailError = "Unable to load commit diff: " + diffResult.stderr_str();
        }
    }
    if (detailCache.infoFuture.valid() && detailCache.infoFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        auto infoResult = detailCache.infoFuture.get();
        detailCache.infoFuture = {};
        if (infoResult.success()) {
            auto info = cdv::parse_commit_info(infoResult.stdout_str());
            info.entry.hash = repo.selectedCommitHash;
            info.entry.shortHash = repo.selectedCommitHash.substr(0, 7);
            detailCache.entry = std::move(info.entry);
            detailCache.commitDetailBody = info.body;
            detailCache.commitDetailAuthorEmail = info.authorEmail;
            detailCache.commitDetailParents = info.parents;
        } else {
            detailCache.commitDetailError += " Unable to load commit metadata: " + infoResult.stderr_str();
        }
    }
    const auto* selectedCommit = &detailCache.entry;

    int nextId = 3050;
    constexpr float PAD = 16.0f;
    constexpr float LABEL_W = 70.0f;
    float contentW = layout.mainContent.width;
    float controlsHeight = (layout.diffFindOpen ? 34.f : 0.f) + (detailCache.commitDetailDiff.empty() ? 0.f : 24.f);

    auto findHost = div(ctx, mk(parent, 593000), ComponentConfig{}
        .with_size(ComponentSize{percent(1.f), pixels(controlsHeight)})
        .with_debug_name("commit_find_host"));
    auto scrollContainer = div(ctx, mk(parent, nextId++),
        ComponentConfig{}
            .with_size(ComponentSize{percent(1.0f), pixels(layout.mainContent.height - controlsHeight)})
            .with_overflow(Overflow::Scroll)
            .with_flex_direction(FlexDirection::Column)
            .with_no_wrap()  // a scroll list must stack, never wrap into columns
            .with_custom_background(theme::WINDOW_BG)
            .with_roundness(0.0f)
            .with_debug_name("commit_detail_scroll"));

    ui::remember_reading_position(repo, scrollContainer.ent(), "commit:" + repo.selectedCommitHash +
        (layout.diffViewMode == LayoutComponent::DiffViewMode::SideBySide ? "\nsplit" : "\ninline"),
        !detailCache.patchFuture.valid() && !detailCache.infoFuture.valid());

    auto backBtn = button(ctx, mk(scrollContainer.ent(), nextId++),
        preset::Button("<- Back")
            .with_size(ComponentSize{children(), children()})
            .with_padding(Padding{
                .top = pixels(3), .right = pixels(12),
                .bottom = pixels(3), .left = pixels(12)})
            .with_margin(Margin{
                // left = PAD - button padding so the "<- Back" TEXT lines up
                // with the title/body left edge (which start at PAD).
                .top = pixels(8), .bottom = pixels(4),
                .left = pixels(PAD - 12.0f), .right = {}})
            .with_transparent_bg()
            .with_custom_text_color(theme::BUTTON_PRIMARY)
            .with_font_size(afterhours::ui::FontSize::Medium)
            .with_debug_name("commit_back_btn"));

    if (backBtn) {
        repo.selectedCommitHash.clear();
        detailCache.cachedCommitHash.clear();
        return;
    }

    auto subjectLabel = div(ctx, mk(scrollContainer.ent(), nextId++),
        ComponentConfig{}
            .with_label(selectedCommit->subject)
            .with_size(ComponentSize{percent(1.0f), children()})
            .with_padding(Padding{
                .top = pixels(8), .right = pixels(PAD),
                .bottom = pixels(4), .left = pixels(PAD)})
            .with_custom_text_color(theme::TEXT_PRIMARY)
            .with_font_size(afterhours::ui::FontSize::XL)  // Display tier (22)
            .with_alignment(TextAlignment::Left)
            .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
            .with_roundness(0.0f)
            .with_debug_name("commit_subject"));
    ui::set_tooltip(subjectLabel.ent(), selectedCommit->subject);

    // While reviewing, comments on a commit's hunks are scoped to its SHA and
    // meant to be applied as fixups — make that explicit (mock's fixup banner).
    if (review && review->reviewing) {
        div(ctx, mk(scrollContainer.ent(), nextId++),
            ComponentConfig{}
                .with_label("comments here -> fixup into " +
                            selectedCommit->hash.substr(0, 7))
                .with_size(ComponentSize{children(), children()})
                .with_padding(Padding{
                    .top = pixels(3), .right = pixels(8),
                    .bottom = pixels(3), .left = pixels(8)})
                .with_margin(Margin{
                    .top = pixels(2), .bottom = pixels(6),
                    .left = pixels(PAD), .right = {}})
                .with_custom_background(afterhours::Color{51, 42, 24, 255})
                .with_custom_text_color(afterhours::Color{226, 192, 141, 255})
                .with_font_size(afterhours::ui::FontSize::Small)
                .with_rounded_corners(theme::layout::ROUNDED_CORNERS)
                .with_corner_radius(theme::layout::RADIUS_BOX)
                .with_debug_name("commit_fixup_banner"));
    }

    detailCache.messageVisibleRows = 0;
    if (!detailCache.commitDetailBody.empty()) {
        float bodyWidth = std::max(80.f, contentW - PAD * 2.f - 16.f);
        float fontSize = resolve_to_pixels(h720(14.f), static_cast<float>(afterhours::graphics::get_screen_height()));
        if (detailCache.messageLines.empty() || detailCache.messageWrapWidth != bodyWidth || detailCache.messageFontSize != fontSize) {
            auto& fonts = EntityHelper::get_singleton_cmp_enforce<afterhours::ui::FontManager>();
            const auto font = fonts.get_active_font();
            detailCache.messageLines = wrap_measured_text(detailCache.commitDetailBody, bodyWidth,
                [&](const std::string& text) { return afterhours::measure_text(font, text.c_str(), fontSize, 1.f).x; });
            detailCache.messageWrapWidth = bodyWidth;
            detailCache.messageFontSize = fontSize;
        }
        const auto& bodyLines = detailCache.messageLines;
        constexpr size_t previewLines = 6;
        if (bodyLines.size() > previewLines) {
            if (button(ctx, mk(scrollContainer.ent(), nextId++),
                preset::Button(detailCache.messageExpanded ? "Collapse message" : "Show full message (" + std::to_string(bodyLines.size()) + " lines)")
                    .with_size(ComponentSize{children(), pixels(28)})
                    .with_debug_name("commit_message_toggle")))
                detailCache.messageExpanded = !detailCache.messageExpanded;
        }
        size_t count = detailCache.messageExpanded ? bodyLines.size() : std::min(previewLines, bodyLines.size());
        auto origin = div(ctx, mk(scrollContainer.ent(), 596000), ComponentConfig{}
            .with_size(ComponentSize{percent(1.f), children()})
            .with_flex_direction(FlexDirection::Column).with_no_wrap());
        float rowHeight = resolve_to_pixels(h720(18.f), static_cast<float>(afterhours::graphics::get_screen_height()));
        float scrollY = 0.f, viewport = layout.mainContent.height;
        if (scrollContainer.ent().has<afterhours::ui::HasScrollView>()) {
            const auto& scroll = scrollContainer.ent().get<afterhours::ui::HasScrollView>();
            scrollY = scroll.scroll_offset.y;
            if (scroll.viewport_or_zero().y > 0.f) viewport = scroll.viewport_or_zero().y;
        }
        auto rect = afterhours::ui::detail::apply_scroll_offset(origin.ent(), origin.ent().get<afterhours::ui::UIComponent>().rect());
        float bodyY = std::max(0.f, rect.y + scrollY - scrollContainer.ent().get<afterhours::ui::UIComponent>().rect().y);
        auto [first, last] = visible_rows(count, rowHeight, bodyY, scrollY, viewport);
        div(ctx, mk(origin.ent(), 0), ComponentConfig{}
            .with_size(ComponentSize{percent(1.f), pixels(static_cast<float>(first) * rowHeight)}));
        for (size_t i = first; i < last; ++i) {
            ++detailCache.messageVisibleRows;
            const auto& bl = bodyLines[i];
            div(ctx, mk(origin.ent(), 1 + static_cast<int>(i)),
                ComponentConfig{}
                    .with_label(bl.empty() ? " " : bl)
                    .with_size(ComponentSize{percent(1.0f), h720(18.0f)})
                    .with_padding(Padding{
                        .right = pixels(PAD),
                        .left = pixels(PAD)})
                    .with_transparent_bg()
                    .with_custom_text_color(theme::TEXT_PRIMARY)
                    .with_font_size(afterhours::ui::FontSize::Medium)
                    .with_alignment(TextAlignment::Left)
                    .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
                    .with_roundness(0.0f)
                    .with_debug_name("commit_body_line"));
        }
        div(ctx, mk(origin.ent(), 1 + static_cast<int>(count)), ComponentConfig{}
            .with_size(ComponentSize{percent(1.f), pixels(static_cast<float>(count - last) * rowHeight)}));
    }

    auto metadataHeader = div(ctx, mk(scrollContainer.ent(), nextId++), ComponentConfig{}
        .with_size(ComponentSize{percent(1.f), pixels(34)}).with_flex_direction(FlexDirection::Row)
        .with_padding(Padding{.left = pixels(PAD), .right = pixels(PAD)}));
    div(ctx, mk(metadataHeader.ent(), 0), ComponentConfig{}
        .with_label(selectedCommit->hash.substr(0, 7) + "  " + selectedCommit->author + "  " + selectedCommit->authorDate.substr(0, 10))
        .with_size(ComponentSize{expand(), pixels(30)}).with_font_size(FontSize::Small)
        .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis).with_debug_name("commit_meta_compact"));
    if (button(ctx, mk(metadataHeader.ent(), 1), preset::Button(layout.commitMetadataExpanded ? "Hide details" : "Show details")
        .with_size(ComponentSize{pixels(105), pixels(28)}).with_debug_name("commit_meta_toggle")))
        layout.commitMetadataExpanded = !layout.commitMetadataExpanded;

    if (layout.commitMetadataExpanded) {
        float cardW = std::min(contentW - PAD * 2.0f, 680.0f);
        if (cardW < 160.0f) cardW = std::max(contentW - PAD * 2.0f, 120.0f);
        float metaValueW = cardW - PAD * 2.0f - LABEL_W - 8.0f;
        if (metaValueW < 100.0f) metaValueW = 100.0f;

        auto metaBox = div(ctx, mk(scrollContainer.ent(), nextId++),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(cardW), children()})
                .with_custom_background(theme::SIDEBAR_BG)
                .with_flex_direction(FlexDirection::Column)
                .with_no_wrap()
                .with_padding(Padding{
                    .top = pixels(10), .right = pixels(PAD),
                    .bottom = pixels(10), .left = pixels(PAD)})
                .with_margin(Margin{
                    .top = pixels(8), .bottom = pixels(8),
                    .left = pixels(PAD), .right = {}})
                .with_border(theme::BORDER, h720(1.0f))
                .with_rounded_corners(theme::layout::ROUNDED_CORNERS)
                .with_corner_radius(theme::layout::RADIUS_BOX)
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
                    .with_font_size(afterhours::ui::FontSize::Small)
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
                    .with_font_size(afterhours::ui::FontSize::Small)
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
        if (dateStr.size() >= 16 && dateStr[10] == 'T') {
            dateStr = dateStr.substr(0, 10) + " " + dateStr.substr(11, 5);
        }
        std::string relTime =
            git_helpers::relative_time(selectedCommit->authorDate, true);
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
                    .with_font_size(afterhours::ui::FontSize::Small)
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

    }

    div(ctx, mk(scrollContainer.ent(), nextId++),
        ComponentConfig{}
            .with_size(ComponentSize{percent(1.0f), pixels(1)})
            .with_custom_background(theme::BORDER)
            .with_margin(Margin{
                .top = pixels(8), .bottom = pixels(8)})
            .with_roundness(0.0f)
            .with_debug_name("commit_sep"));

    if (detailCache.patchFuture.valid() || detailCache.infoFuture.valid()) {
        div(ctx, mk(scrollContainer.ent(), nextId++), ComponentConfig{}.with_label("Loading commit details...")
            .with_size(ComponentSize{percent(1.f), pixels(50)}).with_font_size(FontSize::Medium)
            .with_debug_name("commit_detail_loading"));
    } else if (!detailCache.commitDetailError.empty()) {
        div(ctx, mk(scrollContainer.ent(), nextId++),
            ComponentConfig{}
                .with_label(detailCache.commitDetailError)
                .with_size(ComponentSize{percent(1.f), pixels(100)})
                .with_font_size(FontSize::Medium)
                .with_custom_text_color(theme::STATUS_DELETED)
                .with_text_overflow(afterhours::ui::TextOverflow::Wrap)
                .with_debug_name("commit_load_error"));
        auto retry = button(ctx, mk(scrollContainer.ent(), nextId++),
            preset::Button("Retry loading commit")
                .with_size(ComponentSize{children(), pixels(32)})
                .with_debug_name("commit_load_retry"));
        if (retry) detailCache.cachedCommitHash.clear();
    } else if (detailCache.commitDetailDiff.empty()) {
        div(ctx, mk(scrollContainer.ent(), nextId++),
            ComponentConfig{}
                .with_label("No file changes in this commit")
                .with_size(ComponentSize{percent(1.0f), children()})
                .with_padding(Padding{
                    .top = pixels(16), .right = pixels(PAD),
                    .bottom = pixels(16), .left = pixels(PAD)})
                .with_custom_text_color(theme::TEXT_SECONDARY)
                .with_font_size(afterhours::ui::FontSize::Medium)
                .with_alignment(TextAlignment::Center)
                .with_roundness(0.0f)
                .with_debug_name("empty_diff_msg"));
    } else {
        int totalAdd = 0, totalDel = 0;
        for (auto& d : detailCache.commitDetailDiff) {
            totalAdd += d.additions;
            totalDel += d.deletions;
        }

        // "FILES CHANGED (N files, +A -D)" — split into colored spans so the
        // additions read green and deletions red, matching the per-file rows.
        std::string summaryPrefix = "FILES CHANGED (" +
            std::to_string(detailCache.commitDetailDiff.size()) + " file" +
            (detailCache.commitDetailDiff.size() != 1 ? "s" : "") + ", ";

        auto summaryRow = div(ctx, mk(scrollContainer.ent(), nextId++),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), children()})
                .with_flex_direction(FlexDirection::Row)
                .with_align_items(AlignItems::Center)
                .with_gap(pixels(4))
                .with_padding(Padding{
                    .top = pixels(4), .right = pixels(PAD),
                    .bottom = pixels(4), .left = pixels(PAD)})
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("files_changed_header"));

        auto summarySpan = [&](int id, const std::string& text,
                               afterhours::Color color) {
            div(ctx, mk(summaryRow.ent(), id),
                ComponentConfig{}
                    .with_label(text)
                    .with_size(ComponentSize{children(), children()})
                    .with_transparent_bg()
                    .with_custom_text_color(color)
                    .with_font_size(afterhours::ui::FontSize::Small)
                    .with_letter_spacing(0.5f)
                    .with_alignment(TextAlignment::Left)
                    .with_roundness(0.0f)
                    .with_debug_name("files_changed_span"));
        };

        summarySpan(1, summaryPrefix, theme::TEXT_SECONDARY);
        summarySpan(2, "+" + std::to_string(totalAdd), theme::STATUS_ADDED);
        summarySpan(3, "-" + std::to_string(totalDel), theme::STATUS_DELETED);
        summarySpan(4, ")", theme::TEXT_SECONDARY);

        constexpr float STATS_W = 55.0f;
        constexpr float BAR_W = 50.0f;
        constexpr float BADGE_W = 20.0f;
        constexpr float BAR_MARGIN = 6.0f;  // keep change bars off the right edge

        int totalChanges = totalAdd + totalDel;
        if (totalChanges == 0) totalChanges = 1;

        float fileNameW = contentW - PAD * 2 - BADGE_W - BAR_MARGIN - STATS_W - BAR_W - 8.0f * 4;
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
            ui::set_tooltip(fileRow.ent(), fd.isRenamed ? fd.oldPath + " -> " + fd.filePath : fd.filePath);
            fileRow.ent().addComponentIfMissing<HasClickListener>([](Entity&){});
            if (fileRow.ent().get<HasClickListener>().down) {
                repo.diffTargetFile = fd.filePath;
                repo.diffTargetFrames = 3;
                layout.diffFindOpen = false;
            }

            // Status letter in a filled colored circle (mock style).
            div(ctx, mk(fileRow.ent(), 1),
                ComponentConfig{}
                    .with_label(badge)
                    .with_size(ComponentSize{pixels(18), pixels(18)})
                    .with_custom_background(badgeColor)
                    .with_custom_text_color(theme::WINDOW_BG)
                    .with_font_size(afterhours::ui::FontSize::Small)
                    .with_alignment(TextAlignment::Center)
                    .with_justify_content(JustifyContent::Center)
                    .with_align_items(AlignItems::Center)
                    .with_rounded_corners(theme::layout::ROUNDED_CORNERS)
                    .with_roundness(1.0f)
                    .with_debug_name("file_badge"));

            std::string fname = fd.filePath;
            if (fd.isRenamed && !fd.oldPath.empty()) {
                fname = fd.oldPath + " -> " + fd.filePath;
            }
            auto fileName = button(ctx, mk(fileRow.ent(), 2),
                ComponentConfig{}
                    .with_label(fname)
                    // Match the badge box height + vertical-center so the name
                    // sits on the same center as the M/A/D badge circle.
                    .with_size(ComponentSize{pixels(fileNameW), pixels(18)})
                    .with_align_items(AlignItems::Center)
                    .with_transparent_bg()
                    .with_custom_text_color(theme::TEXT_PRIMARY)
                    .with_font_size(afterhours::ui::FontSize::Medium)
                    .with_alignment(TextAlignment::Left)
                    .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
                    .with_roundness(0.0f)
                    .with_debug_name("jump_to_diff:" + fd.filePath));
            if (fileName) {
                repo.diffTargetFile = fd.filePath;
                repo.diffTargetFrames = 3;
                layout.diffFindOpen = false;
            }

            // Colored +N / -N counts in two fixed-width right-aligned columns so
            // the numbers line up vertically across rows (no jitter/run-together).
            constexpr float STAT_COL = 26.0f;
            auto statsBox = div(ctx, mk(fileRow.ent(), 3),
                ComponentConfig{}
                    .with_size(ComponentSize{pixels(STATS_W), children()})
                    .with_flex_direction(FlexDirection::Row)
                    .with_align_items(AlignItems::Center)
                    .with_gap(pixels(3))
                    .with_transparent_bg()
                    .with_roundness(0.0f)
                    .with_debug_name("file_stats"));
            div(ctx, mk(statsBox.ent(), 1),
                ComponentConfig{}
                    .with_label(fd.additions > 0 ? "+" + std::to_string(fd.additions) : "")
                    .with_size(ComponentSize{pixels(STAT_COL), children()})
                    .with_transparent_bg()
                    .with_custom_text_color(theme::STATUS_ADDED)
                    .with_font_size(afterhours::ui::FontSize::Small)
                    .with_alignment(TextAlignment::Right)
                    .with_roundness(0.0f)
                    .with_debug_name("file_add"));
            div(ctx, mk(statsBox.ent(), 2),
                ComponentConfig{}
                    .with_label(fd.deletions > 0 ? "-" + std::to_string(fd.deletions) : "")
                    .with_size(ComponentSize{pixels(STAT_COL), children()})
                    .with_transparent_bg()
                    .with_custom_text_color(theme::STATUS_DELETED)
                    .with_font_size(afterhours::ui::FontSize::Small)
                    .with_alignment(TextAlignment::Right)
                    .with_roundness(0.0f)
                    .with_debug_name("file_del"));

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
                    .with_margin(Margin{.right = pixels(BAR_MARGIN)})
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

        ui::render_diff(ctx, scrollContainer.ent(),
                               detailCache.commitDetailDiff,
                               layout.mainContent.width,
                               layout.mainContent.height,
                               true, false,
                               layout.diffViewMode == LayoutComponent::DiffViewMode::SideBySide,
                               repo.repoPath, review, repo.selectedCommitHash, &findHost.ent());
    }
}

inline std::optional<FileDiff> build_new_file_diff(
    const std::string& repoPath, const std::string& relPath) {
    namespace fs = std::filesystem;
    fs::path fullPath = fs::path(repoPath) / relPath;

    auto source = file_content::read_working_file(fullPath, {}, 1024 * 1024);
    if (!source.error.empty()) return std::nullopt;
    std::string contents = std::move(source.bytes);

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
    diff.newMode = std::move(source.mode);

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
    if (!contents.empty() && !contents.ends_with('\n')) hunk.noNewline.insert(hunk.lines.size() - 1);
    hunk.header = "@@ -0,0 +1," + std::to_string(lineNum) + " @@ (new file)";
    diff.additions = lineNum;
    diff.hunks.push_back(std::move(hunk));

    return diff;
}

} // namespace ecs
