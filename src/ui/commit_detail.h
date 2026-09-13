#pragma once

#include <fstream>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "../git/git_parser.h"
#include "../git/commit_patch.h"
#include "../git/git_runner.h"
#include "../util/git_helpers.h"
#include "../util/visible_rows.h"
#include "../util/file_content.h"
#include "../ecs/ui_imports.h"
#include "diff_renderer.h"
#include "review_queue.h"
#include <afterhours/src/plugins/ui/text_measure.h>

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

    const auto selectedParent = selected_commit_parent(repo);
    const auto reviewScope = commit_review_scope(repo);
    auto requestKey = [&] {
        return commit_review_scope(repo) + "\n" + std::to_string(repo.diffContext) + ":" + std::to_string(repo.ignoreWhitespace);
    };
    bool staleRequest = (detailCache.patchFuture.valid()) &&
        !navigation::accepts(repo, detailCache.requestStamp, requestKey());
    bool commitJustChanged = staleRequest || detailCache.cachedCommitHash != repo.selectedCommitHash() || detailCache.cachedRepoPath != repo.repoPath ||
        detailCache.cachedParentHash != selectedParent || detailCache.cachedContext != repo.diffContext ||
        detailCache.cachedIgnoreWhitespace != repo.ignoreWhitespace;
    if (commitJustChanged) {
        static_cast<CommitDetailRuntime&>(detailCache) = {};
        detailCache.entry.hash = repo.selectedCommitHash();
        detailCache.entry.shortHash = repo.selectedCommitHash().substr(0, 7);
        detailCache.entry.subject = detailCache.entry.shortHash;
        for (const auto* entries : {&repo.commitLog, &repo.fileHistoryEntries, &repo.commitSearchEntries})
            for (const auto& entry : *entries)
                if (entry.hash == repo.selectedCommitHash()) detailCache.entry = entry;
        detailCache.requestStamp = navigation::stamp(repo, requestKey());
        detailCache.patchFuture = git::load_commit_patch_async({repo.repoPath, repo.selectedCommitHash(),
            selectedParent, repo.diffContext, repo.ignoreWhitespace});
        detailCache.cachedCommitHash = repo.selectedCommitHash();
        detailCache.cachedParentHash = selectedParent;
        detailCache.cachedRepoPath = repo.repoPath;
        detailCache.cachedContext = repo.diffContext;
        detailCache.cachedIgnoreWhitespace = repo.ignoreWhitespace;
    }
    if (detailCache.patchFuture.valid() && detailCache.patchFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
        try {
            auto patch = detailCache.patchFuture.get();
            if (!navigation::accepts(repo, detailCache.requestStamp, requestKey())) return;
            navigation::resolve_review(repo, detailCache.requestStamp, patch.resolvedCommit, patch.resolvedParent);
            detailCache.cachedCommitHash = repo.selectedCommitHash();
            detailCache.cachedParentHash = selected_commit_parent(repo);
            detailCache.requestStamp = navigation::stamp(repo, requestKey());
            detailCache.commitDetailDiff = std::move(patch.files);
            navigation::remember_review_files(repo, detailCache.commitDetailDiff);
            detailCache.commitDetailError = std::move(patch.error);
            if (detailCache.commitDetailError.empty()) {
                auto info = cdv::parse_commit_info(patch.metadata);
                info.entry.hash = repo.selectedCommitHash();
                info.entry.shortHash = repo.selectedCommitHash().substr(0, 7);
                info.entry.decorations = detailCache.entry.decorations;
                detailCache.entry = std::move(info.entry);
                navigation::remember_commit_subject(repo, detailCache.entry.subject);
                detailCache.commitDetailBody = std::move(info.body);
                detailCache.commitDetailAuthorEmail = std::move(info.authorEmail);
                detailCache.commitDetailParents = std::move(info.parents);
            }
        } catch (const std::exception& error) { detailCache.commitDetailError = error.what(); }
    }
    ui::diff_syntax::update(repo, detailCache.commitDetailDiff, commit_review_scope(repo));
    const auto* selectedCommit = &detailCache.entry;

    int nextId = 3050;
    constexpr float PAD = 16.0f;
    float contentW = layout.mainContent.width;
    float controlsHeight = ui::diff_controls_height(contentW, layout.diffOptionsOpen,
        true, !detailCache.commitDetailDiff.empty());
    auto boundedLines = [&](std::string_view input, float width, const std::string& font, float size, size_t limit) {
        size_t end = std::min(input.size(), size_t{512});
        while (end < input.size() && end > 0 && (static_cast<unsigned char>(input[end]) & 0xc0) == 0x80) --end;
        const bool truncated = end < input.size();
        std::string text(input.substr(0, end));
        std::replace(text.begin(), text.end(), '\n', ' ');
        auto lines = afterhours::ui::wrap_text(text, std::max(40.f, width) * ui::zoom::get(), font, size * ui::zoom::get());
        if (lines.size() > limit || truncated) {
            lines.resize(std::min(lines.size(), limit));
            if (!lines.empty()) lines.back() += "...";
        }
        return lines;
    };
    auto titleLines = boundedLines(selectedCommit->subject, contentW - 100.f, "ui-bold", 20.f, 2);
    std::string titleText;
    for (const auto& line : titleLines) { if (!titleText.empty()) titleText += '\n'; titleText += line; }
    const float titleHeight = std::max(1.f, static_cast<float>(titleLines.size())) * 26.f;
    const float headerHeight = titleHeight + 8.f;
    auto heading = div(ctx, mk(parent, 593010), ComponentConfig{}.with_skip_grid_snap()
        .with_size(ComponentSize{percent(1.f), pixels(headerHeight)})
        .with_padding(Padding{.top = pixels(4), .right = pixels(0), .bottom = pixels(4), .left = pixels(0)})
        .with_flex_direction(FlexDirection::Row).with_no_wrap()
        .with_debug_name("commit_heading"));

    auto findHost = div(ctx, mk(parent, 593000), ComponentConfig{}.with_skip_grid_snap()
        .with_size(ComponentSize{percent(1.f), pixels(controlsHeight)})
        .with_flex_direction(FlexDirection::Column).with_no_wrap().with_debug_name("commit_find_host"));
    auto scrollContainer = div(ctx, mk(parent, nextId++),
        ComponentConfig{}.with_skip_grid_snap()
            .with_size(ComponentSize{percent(1.0f), pixels(std::max(0.f, layout.mainContent.height - headerHeight - controlsHeight))})
            .with_overflow(Overflow::Scroll)
            .with_flex_direction(FlexDirection::Column)
            .with_no_wrap()  // a scroll list must stack, never wrap into columns
            .with_custom_background(theme::WINDOW_BG)
            .with_roundness(0.0f)
            .with_debug_name("commit_detail_scroll"));

    ui::bind_reading_view(repo, scrollContainer.ent(), "commit:" + reviewScope +
        (layout.diffViewMode == LayoutComponent::DiffViewMode::SideBySide ? "\nsplit" : "\ninline"),
        !detailCache.patchFuture.valid());
    if (review) render_review_queue(ctx, scrollContainer.ent(), nextId++, repo, *review, detailCache);

    auto subjectLabel = div(ctx, mk(heading.ent(), nextId++),
        ComponentConfig{}.with_skip_grid_snap()
            .with_label(titleText)
            .with_size(ComponentSize{expand(), pixels(titleHeight)})
            .with_padding(Padding{.top = pixels(0), .right = pixels(0), .bottom = pixels(0), .left = pixels(0)})
            .with_custom_text_color(theme::TEXT_PRIMARY)
            .with_font("ui-bold", pixels(20))
            .with_alignment(TextAlignment::Left)
            .with_text_overflow(afterhours::ui::TextOverflow::Wrap)
            .with_roundness(0.0f)
            .with_debug_name("commit_detail_subject"));
    ui::set_tooltip(subjectLabel.ent(), selectedCommit->subject);
    auto revision = div(ctx, mk(heading.ent(), 593014), ComponentConfig{}
        .with_label(selectedCommit->shortHash).with_size(ComponentSize{pixels(88), pixels(titleHeight)})
        .with_font("mono", pixels(13)).with_custom_text_color(theme::TEXT_SECONDARY)
        .with_debug_name("commit_sticky_revision"));
    ui::set_tooltip(revision.ent(), selectedCommit->hash);


    auto metadataHeader = div(ctx, mk(scrollContainer.ent(), 593012), ComponentConfig{}.with_skip_grid_snap()
        .with_size(ComponentSize{percent(1.f), pixels(32)}).with_flex_direction(FlexDirection::Row)
        .with_align_items(AlignItems::Center).with_no_wrap().with_gap(pixels(6))
        .with_padding(Padding{.right = pixels(8)})
        .with_debug_name("commit_meta_compact"));
    auto metadataWidth = [&](const std::string& text, float size) {
        return afterhours::ui::measure_text_line(text, afterhours::ui::UIComponent::DEFAULT_FONT,
            size * ui::zoom::get()).x / ui::zoom::get() + 12.f;
    };
    div(ctx, mk(metadataHeader.ent(), 0), ComponentConfig{}
        .with_label(selectedCommit->author).with_size(ComponentSize{contentW < 680.f ? expand() :
            pixels(std::min(160.f, metadataWidth(selectedCommit->author, 14.f))), pixels(28)})
        .with_font_size(pixels(14)).with_custom_text_color(theme::TEXT_SECONDARY)
        .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis).with_debug_name("commit_author"));
    const auto relativeDate = "· " + git_helpers::relative_time(selectedCommit->authorDate, true);
    auto date = div(ctx, mk(metadataHeader.ent(), 2), ComponentConfig{}
        .with_label(relativeDate)
        .with_size(ComponentSize{pixels(metadataWidth(relativeDate, 14.f)), pixels(28)}).with_font_size(pixels(14))
        .with_custom_text_color(theme::TEXT_SECONDARY).with_debug_name("commit_relative_date"));
    ui::set_tooltip(date.ent(), selectedCommit->authorDate);
    const auto decorations = cdv::parse_decorations(selectedCommit->decorations);
    if (!decorations.empty() && contentW >= 680.f) div(ctx, mk(metadataHeader.ent(), 4),
        preset::Button(decorations.front().label).with_size(ComponentSize{
            pixels(std::min(100.f, metadataWidth(decorations.front().label, 12.f) + 8.f)), pixels(22)})
            .with_transparent_bg().with_border(theme::BORDER, pixels(1)).with_font_size(pixels(12))
            .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
            .with_custom_text_color(theme::TEXT_SECONDARY).with_debug_name("commit_compact_badge"));
    if (contentW >= 680.f) div(ctx, mk(metadataHeader.ent(), 5), ComponentConfig{}
        .with_size(ComponentSize{expand(), pixels(28)}));
    bool detailsExpanded = repo.workspace().document(repo.workspace().active_id())->detailsExpanded;
    if (button(ctx, mk(metadataHeader.ent(), 1), preset::Button(detailsExpanded ? "Less detail" : "Details")
        .with_size(ComponentSize{pixels(80), pixels(28)}).with_transparent_bg()
        .with_custom_text_color(theme::TEXT_SECONDARY).with_font_size(pixels(13)).with_debug_name("commit_meta_toggle")))
    {
        navigation::toggle_commit_details(repo);
        detailsExpanded = !detailsExpanded;
    }

    // While reviewing, comments on a commit's hunks are scoped to its SHA and
    // meant to be applied as fixups — make that explicit (mock's fixup banner).
    if (review && review->reviewing) {
        div(ctx, mk(scrollContainer.ent(), nextId++),
            ComponentConfig{}.with_skip_grid_snap()
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
                .with_font_size(pixels(12))
                .with_rounded_corners(theme::layout::ROUNDED_CORNERS)
                .with_corner_radius(theme::layout::RADIUS_BOX)
                .with_debug_name("commit_fixup_banner"));
    }

    std::istringstream parentStream(detailCache.commitDetailParents);
    std::vector<std::string> parents;
    for (std::string hash; parentStream >> hash;) parents.push_back(std::move(hash));
    if (parents.size() > 1) {
        auto chosen = std::find(parents.begin(), parents.end(), selectedParent);
        size_t index = chosen == parents.end() ? 0 : static_cast<size_t>(chosen - parents.begin());
        if (button(ctx, mk(scrollContainer.ent(), nextId++), preset::Button("Compare against parent " + std::to_string(index + 1) + " · " + parents[index].substr(0, 12))
                .with_size(ComponentSize{children(), pixels(28)}).with_font_size(pixels(12)).with_debug_name("merge_parent_select"))) {
            std::vector<ui::ContextMenuItem> choices;
            for (size_t i = 0; i < parents.size(); ++i)
                choices.push_back(ui::ContextMenuItem::item("Parent " + std::to_string(i + 1) + " · " + parents[i].substr(0, 12),
                    [path = repo.repoPath, commit = repo.selectedCommitHash(), hash = i == 0 ? "" : parents[i]] {
                        auto* active = find_singleton<RepoComponent, ActiveTab>();
                        if (active && active->repoPath == path && active->selectedCommitHash() == commit) {
                            navigation::open(*active, reading::review(hash.empty() ? commit : "parent:" + hash + ":" + commit));
                        }
                    }));
            ui::show_context_menu(ctx.mouse.pos.x, ctx.mouse.pos.y, std::move(choices));
        }
    }

    detailCache.messageVisibleRows = 0;
    if (detailsExpanded) {
        const bool stacked = contentW < 520.f;
        auto metadata = div(ctx, mk(scrollContainer.ent(), 593020), ComponentConfig{}.with_skip_grid_snap()
            .with_size(ComponentSize{pixels(std::max(1.f, std::min(contentW - PAD * 2.f, 680.f))), children()})
            .with_flex_direction(FlexDirection::Column).with_no_wrap().with_gap(pixels(6))
            .with_padding(Padding{.top = pixels(10), .right = pixels(PAD), .bottom = pixels(10), .left = pixels(PAD)})
            .with_margin(Margin{.top = pixels(8), .bottom = pixels(8), .left = pixels(PAD)})
            .with_custom_background(theme::SIDEBAR_BG).with_border(theme::BORDER, pixels(1))
            .with_rounded_corners(theme::layout::ROUNDED_CORNERS).with_corner_radius(theme::layout::RADIUS_BOX)
            .with_debug_name("commit_meta_box"));
        auto metadataRow = [&](int id, const std::string& label, const std::string& value) {
            auto row = div(ctx, mk(metadata.ent(), id), ComponentConfig{}
                .with_size(ComponentSize{percent(1.f), children()}).with_skip_grid_snap()
                .with_flex_direction(stacked ? FlexDirection::Column : FlexDirection::Row)
                .with_no_wrap().with_gap(pixels(4)).with_debug_name("meta_row"));
            div(ctx, mk(row.ent(), 0), ComponentConfig{}.with_label(label)
                .with_size(ComponentSize{stacked ? percent(1.f) : pixels(70), pixels(18)})
                .with_font_size(pixels(12)).with_custom_text_color(theme::TEXT_SECONDARY)
                .with_alignment(TextAlignment::Left).with_debug_name("meta_label"));
            auto text = div(ctx, mk(row.ent(), 1), ComponentConfig{}.with_label(value)
                .with_size(ComponentSize{stacked ? percent(1.f) : expand(), children()})
                .with_font_size(pixels(12)).with_custom_text_color(theme::TEXT_PRIMARY)
                .with_alignment(TextAlignment::Left).with_text_overflow(afterhours::ui::TextOverflow::Wrap)
                .with_debug_name("meta_value"));
            ui::set_tooltip(text.ent(), value);
        };
        metadataRow(0, "Commit:", selectedCommit->hash);
        metadataRow(1, "Author:", selectedCommit->author + (detailCache.commitDetailAuthorEmail.empty() ? "" :
            " <" + detailCache.commitDetailAuthorEmail + ">"));
        metadataRow(2, "Date:", selectedCommit->authorDate);
        if (!detailCache.commitDetailParents.empty()) metadataRow(3, "Parents:", detailCache.commitDetailParents);
        if (!selectedCommit->decorations.empty()) metadataRow(4, "Refs:", selectedCommit->decorations);
    }
    if (detailsExpanded) {
        float bodyWidth = std::max(80.f, contentW - PAD * 2.f - 16.f) * ui::zoom::get();
        float fontSize = 14.f * ui::zoom::get();
        if (detailCache.messageLines.empty() || detailCache.messageWrapWidth != bodyWidth || detailCache.messageFontSize != fontSize) {
            auto& fonts = EntityHelper::get_singleton_cmp_enforce<afterhours::ui::FontManager>();
            const auto font = fonts.get_active_font();
            const auto message = selectedCommit->subject + (detailCache.commitDetailBody.empty() ? "" : "\n\n" + detailCache.commitDetailBody);
            detailCache.messageLines = wrap_measured_text(message, bodyWidth,
                [&](const std::string& text) { return afterhours::measure_text(font, text.c_str(), fontSize, 1.f).x; });
            detailCache.messageWrapWidth = bodyWidth;
            detailCache.messageFontSize = fontSize;
        }
        const auto& bodyLines = detailCache.messageLines;
        size_t count = bodyLines.size();
        auto origin = div(ctx, mk(scrollContainer.ent(), 596000), ComponentConfig{}.with_skip_grid_snap()
            .with_size(ComponentSize{percent(1.f), children()})
            .with_flex_direction(FlexDirection::Column).with_no_wrap());
        float rowHeight = 18.f * ui::zoom::get();
        float scrollY = 0.f, viewport = layout.mainContent.height * ui::zoom::get();
        if (scrollContainer.ent().has<afterhours::ui::HasScrollView>()) {
            const auto& scroll = scrollContainer.ent().get<afterhours::ui::HasScrollView>();
            scrollY = scroll.scroll_offset.y;
            if (scroll.viewport_or_zero().y > 0.f) viewport = scroll.viewport_or_zero().y;
        }
        auto rect = afterhours::ui::detail::apply_scroll_offset(origin.ent(), origin.ent().get<afterhours::ui::UIComponent>().rect());
        float bodyY = std::max(0.f, rect.y + scrollY - scrollContainer.ent().get<afterhours::ui::UIComponent>().rect().y);
        auto [first, last] = visible_rows(count, rowHeight, bodyY, scrollY, viewport);
        div(ctx, mk(origin.ent(), 0), ComponentConfig{}.with_skip_grid_snap()
            .with_size(ComponentSize{percent(1.f), pixels(static_cast<float>(first) * 18.f)}));
        for (size_t i = first; i < last; ++i) {
            ++detailCache.messageVisibleRows;
            const auto& bl = bodyLines[i];
            div(ctx, mk(origin.ent(), 1 + static_cast<int>(i)),
                ComponentConfig{}.with_skip_grid_snap()
                    .with_label(bl.empty() ? " " : bl)
                    .with_size(ComponentSize{percent(1.0f), pixels(18.0f)})
                    .with_padding(Padding{
                        .right = pixels(PAD),
                        .left = pixels(PAD)})
                    .with_transparent_bg()
                    .with_custom_text_color(theme::TEXT_PRIMARY)
                    .with_font_size(pixels(14))
                    .with_alignment(TextAlignment::Left)
                    .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
                    .with_roundness(0.0f)
                    .with_debug_name("commit_body_line"));
        }
        div(ctx, mk(origin.ent(), 1 + static_cast<int>(count)), ComponentConfig{}.with_skip_grid_snap()
            .with_size(ComponentSize{percent(1.f), pixels(static_cast<float>(count - last) * 18.f)}));
    }

    if (detailsExpanded) div(ctx, mk(scrollContainer.ent(), nextId++),
        ComponentConfig{}.with_skip_grid_snap()
            .with_size(ComponentSize{percent(1.0f), pixels(1)})
            .with_custom_background(theme::BORDER)
            .with_margin(Margin{
                .top = pixels(8), .bottom = pixels(8)})
            .with_roundness(0.0f)
            .with_debug_name("commit_sep"));

    if (detailCache.patchFuture.valid()) {
        div(ctx, mk(scrollContainer.ent(), nextId++), ComponentConfig{}.with_skip_grid_snap().with_label("Loading commit details...")
            .with_size(ComponentSize{percent(1.f), pixels(50)}).with_font_size(pixels(14))
            .with_debug_name("commit_detail_loading"));
    } else if (!detailCache.commitDetailError.empty()) {
        div(ctx, mk(scrollContainer.ent(), nextId++),
            ComponentConfig{}.with_skip_grid_snap()
                .with_label(detailCache.commitDetailError)
                .with_size(ComponentSize{percent(1.f), pixels(100)})
                .with_font_size(pixels(14))
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
            ComponentConfig{}.with_skip_grid_snap()
                .with_label("No file changes in this commit")
                .with_size(ComponentSize{percent(1.0f), children()})
                .with_padding(Padding{
                    .top = pixels(16), .right = pixels(PAD),
                    .bottom = pixels(16), .left = pixels(PAD)})
                .with_custom_text_color(theme::TEXT_SECONDARY)
                .with_font_size(pixels(14))
                .with_alignment(TextAlignment::Center)
                .with_roundness(0.0f)
                .with_debug_name("empty_diff_msg"));
    } else if (layout.diffOptionsOpen || detailCache.fileOverviewExpanded) {
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
            ComponentConfig{}.with_skip_grid_snap()
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
                ComponentConfig{}.with_skip_grid_snap()
                    .with_label(text)
                    .with_size(ComponentSize{children(), children()})
                    .with_transparent_bg()
                    .with_custom_text_color(color)
                    .with_font_size(pixels(12))
                    .with_letter_spacing(0.5f)
                    .with_alignment(TextAlignment::Left)
                    .with_roundness(0.0f)
                    .with_debug_name("files_changed_span"));
        };

        summarySpan(1, summaryPrefix, theme::TEXT_SECONDARY);
        summarySpan(2, "+" + std::to_string(totalAdd), theme::STATUS_ADDED);
        summarySpan(3, "-" + std::to_string(totalDel), theme::STATUS_DELETED);
        summarySpan(4, ")", theme::TEXT_SECONDARY);
        if (button(ctx, mk(summaryRow.ent(), 5),
                preset::Button(detailCache.fileOverviewExpanded ? "Hide files" : "Show files")
                    .with_size(ComponentSize{children(), pixels(24)})
                    .with_font_size(pixels(12))
                    .with_debug_name("commit_files_toggle")))
            detailCache.fileOverviewExpanded = !detailCache.fileOverviewExpanded;

        constexpr float STATS_W = 55.0f;
        constexpr float BAR_W = 50.0f;
        constexpr float BADGE_W = 20.0f;
        constexpr float BAR_MARGIN = 6.0f;  // keep change bars off the right edge

        int totalChanges = totalAdd + totalDel;
        if (totalChanges == 0) totalChanges = 1;

        float fileNameW = contentW - PAD * 2 - BADGE_W - BAR_MARGIN - STATS_W - BAR_W - 8.0f * 4;
        if (fileNameW < 80.0f) fileNameW = 80.0f;

        if (detailCache.fileOverviewExpanded) for (size_t fi : visible_review_file_indices(detailCache.commitDetailDiff, repo.fileFilter, review, reviewScope)) {
            auto& fd = detailCache.commitDetailDiff[fi];

            std::string badge = "M";
            afterhours::Color badgeColor = theme::STATUS_MODIFIED;
            if (fd.isNew) { badge = "A"; badgeColor = theme::STATUS_ADDED; }
            else if (fd.isDeleted) { badge = "D"; badgeColor = theme::STATUS_DELETED; }
            else if (fd.isRenamed) { badge = "R"; badgeColor = theme::STATUS_RENAMED; }

            auto fileRow = div(ctx, mk(scrollContainer.ent(), nextId++),
                ComponentConfig{}.with_skip_grid_snap()
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
                navigation::open(repo, reading::review(reviewScope, fd.filePath));
            }

            // Status letter in a filled colored circle (mock style).
            div(ctx, mk(fileRow.ent(), 1),
                ComponentConfig{}.with_skip_grid_snap()
                    .with_label(badge)
                    .with_size(ComponentSize{pixels(18), pixels(18)})
                    .with_custom_background(badgeColor)
                    .with_custom_text_color(theme::WINDOW_BG)
                    .with_font_size(pixels(12))
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
            if (review) fname += unresolved_file_badge(*review, reviewScope, fd.filePath, fd.oldPath);
            auto fileName = button(ctx, mk(fileRow.ent(), 2),
                ComponentConfig{}.with_skip_grid_snap()
                    .with_label(fname)
                    // Match the badge box height + vertical-center so the name
                    // sits on the same center as the M/A/D badge circle.
                    .with_size(ComponentSize{pixels(fileNameW), pixels(18)})
                    .with_align_items(AlignItems::Center)
                    .with_transparent_bg()
                    .with_custom_text_color(theme::TEXT_PRIMARY)
                    .with_font_size(pixels(14))
                    .with_alignment(TextAlignment::Left)
                    .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
                    .with_roundness(0.0f)
                    .with_debug_name("jump_to_diff:" + fd.filePath));
            if (fileName) {
                navigation::open(repo, reading::review(reviewScope, fd.filePath));
            }

            // Colored +N / -N counts in two fixed-width right-aligned columns so
            // the numbers line up vertically across rows (no jitter/run-together).
            constexpr float STAT_COL = 26.0f;
            auto statsBox = div(ctx, mk(fileRow.ent(), 3),
                ComponentConfig{}.with_skip_grid_snap()
                    .with_size(ComponentSize{pixels(STATS_W), children()})
                    .with_flex_direction(FlexDirection::Row)
                    .with_align_items(AlignItems::Center)
                    .with_gap(pixels(3))
                    .with_transparent_bg()
                    .with_roundness(0.0f)
                    .with_debug_name("file_stats"));
            div(ctx, mk(statsBox.ent(), 1),
                ComponentConfig{}.with_skip_grid_snap()
                    .with_label(fd.additions > 0 ? "+" + std::to_string(fd.additions) : "")
                    .with_size(ComponentSize{pixels(STAT_COL), children()})
                    .with_transparent_bg()
                    .with_custom_text_color(theme::STATUS_ADDED)
                    .with_font_size(pixels(12))
                    .with_alignment(TextAlignment::Right)
                    .with_roundness(0.0f)
                    .with_debug_name("file_add"));
            div(ctx, mk(statsBox.ent(), 2),
                ComponentConfig{}.with_skip_grid_snap()
                    .with_label(fd.deletions > 0 ? "-" + std::to_string(fd.deletions) : "")
                    .with_size(ComponentSize{pixels(STAT_COL), children()})
                    .with_transparent_bg()
                    .with_custom_text_color(theme::STATUS_DELETED)
                    .with_font_size(pixels(12))
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
                ComponentConfig{}.with_skip_grid_snap()
                    .with_size(ComponentSize{pixels(BAR_W), pixels(8)})
                    .with_flex_direction(FlexDirection::Row)
                    .with_margin(Margin{.right = pixels(BAR_MARGIN)})
                    .with_custom_background(theme::BORDER)
                    .with_rounded_corners(theme::layout::ROUNDED_CORNERS)
                    .with_roundness(theme::layout::ROUNDNESS_BADGE)
                    .with_debug_name("change_bar"));

            if (greenW > 0.5f) {
                div(ctx, mk(barContainer.ent(), 1),
                    ComponentConfig{}.with_skip_grid_snap()
                        .with_size(ComponentSize{pixels(greenW), pixels(8)})
                        .with_custom_background(theme::STATUS_ADDED)
                        .with_roundness(0.0f)
                        .with_debug_name("bar_green"));
            }
            if (redW > 0.5f) {
                div(ctx, mk(barContainer.ent(), 2),
                    ComponentConfig{}.with_skip_grid_snap()
                        .with_size(ComponentSize{pixels(redW), pixels(8)})
                        .with_custom_background(theme::STATUS_DELETED)
                        .with_roundness(0.0f)
                        .with_debug_name("bar_red"));
            }
        }

        div(ctx, mk(scrollContainer.ent(), nextId++),
            ComponentConfig{}.with_skip_grid_snap()
                .with_size(ComponentSize{percent(1.0f), pixels(1)})
                .with_custom_background(theme::BORDER)
                .with_margin(Margin{
                    .top = pixels(8), .bottom = pixels(8)})
                .with_roundness(0.0f)
                .with_debug_name("diff_sep"));
    }
    if (!detailCache.patchFuture.valid() && detailCache.commitDetailError.empty()) {
        ui::render_diff(ctx, scrollContainer.ent(),
                               detailCache.commitDetailDiff,
                               layout.mainContent.width,
                               layout.mainContent.height,
                               true, false,
                               layout.diffViewMode == LayoutComponent::DiffViewMode::SideBySide,
                               repo.repoPath, review, reviewScope, &findHost.ent());
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
