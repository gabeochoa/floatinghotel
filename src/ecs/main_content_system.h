#pragma once

#include <cstring>
#include <filesystem>
#include <algorithm>
#include <fstream>

#include "../settings.h"
#include "../review_store.h"
#include "../ui/command_log.h"
#include "../ui/commit_detail.h"
#include "../ui/diff_renderer.h"
#include "ui_imports.h"

namespace app_state { extern bool testModeEnabled; }

namespace ecs {

// Export the review basket: durable markdown file (survives reboot / a failed
// AI round-trip) + clipboard.
// TODO(local-first): also sync review comments as git notes / a
// refs/floatinghotel/reviews/* ref so they replicate peer-to-peer via git with
// no server (see docs/afterhours-persistence-proposal.md).
inline void send_review(UIContext<InputAction>& ctx, ReviewComponent& review,
                        RepoComponent* repo) {
    std::string branch = (repo && !repo->currentBranch.empty())
                             ? repo->currentBranch : "HEAD";
    std::string md = build_review_markdown(review, branch);
    if (md.empty()) return;
    if (repo && !repo->repoPath.empty()) {
        std::ofstream f(review_store::markdown_path(repo->repoPath, branch));
        if (f.good()) f << md;
    }
    afterhours::clipboard::set_text(md);
    afterhours::toast::send_info(
        ctx, "Copied " + std::to_string(review.comments.size()) +
                 " comment(s) \xe2\x80\x94 leaves your device when you paste it",
        2.5f);
}

// The feedback basket: an absolute panel on the right listing queued comments.
inline void render_basket(UIContext<InputAction>& ctx, Entity& uiRoot,
                          ReviewComponent& review, RepoComponent* repo) {
    float sw = static_cast<float>(afterhours::graphics::get_screen_width());
    float sh = static_cast<float>(afterhours::graphics::get_screen_height());
    float panelW = std::min(sw * 0.28f, 360.0f);
    float x = sw - panelW;
    // Start below the diff header (Inline/Side-by-Side toggle) so we don't cover it.
    float y = resolve_to_pixels(h720(100.0f), sh);
    float hgt = sh - y - resolve_to_pixels(h720(28.0f), sh);

    auto panel = div(ctx, mk(uiRoot, 7700),
        ComponentConfig{}
            .with_size(ComponentSize{pixels(panelW), pixels(hgt)})
            .with_absolute_position()
            .with_translate(x, y)
            .with_custom_background(theme::SIDEBAR_BG)
            .with_flex_direction(FlexDirection::Column)
            .with_no_wrap()
            .with_padding(Padding{
                .top = h720(8), .right = w1280(10),
                .bottom = h720(8), .left = w1280(10)})
            .with_render_layer(6)
            .with_roundness(0.0f)
            .with_debug_name("feedback_basket"));

    div(ctx, mk(panel.ent(), 0),
        ComponentConfig{}
            .with_label("Feedback basket  " + std::to_string(review.comments.size()))
            .with_size(ComponentSize{percent(1.0f), h720(22)})
            .with_custom_text_color(theme::STATUS_MODIFIED)
            .with_font_size(afterhours::ui::FontSize::Medium)
            .with_debug_name("basket_title"));

    // Comments grouped by scope (working tree vs commit SHA) so it's clear which
    // diff each note targets — mirrors the review markdown that gets sent.
    float itemW = panelW - resolve_to_pixels(w1280(20.0f), sw);
    float txtW = itemW - resolve_to_pixels(w1280(22.0f), sw);
    std::vector<std::string> scopes;
    for (const auto& c : review.comments)
        if (std::find(scopes.begin(), scopes.end(), c.scope) == scopes.end())
            scopes.push_back(c.scope);

    int id = 1;
    int removeIdx = -1;
    for (const auto& scope : scopes) {
        std::string gh;
        if (scope == "wt") {
            gh = "working tree (uncommitted)";
        } else {
            gh = "commit " + scope.substr(0, 7);
            // Enrich with the commit subject so multi-commit reviews are legible.
            if (repo) {
                for (const auto& ce : repo->commitLog) {
                    if (ce.hash == scope || ce.hash.rfind(scope, 0) == 0) {
                        gh += " \xc2\xb7 " + ce.subject;
                        break;
                    }
                }
            }
        }
        div(ctx, mk(panel.ent(), id++),
            ComponentConfig{}
                .with_label(gh)
                .with_size(ComponentSize{percent(1.0f), h720(18)})
                .with_padding(Padding{.top = h720(4)})
                .with_custom_text_color(theme::TEXT_SECONDARY)
                .with_font_size(afterhours::ui::FontSize::Small)
                .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
                .with_debug_name("basket_group"));

        for (int i = 0; i < static_cast<int>(review.comments.size()); ++i) {
            const auto& c = review.comments[i];
            if (c.scope != scope) continue;
            auto itemRow = div(ctx, mk(panel.ent(), id++),
                ComponentConfig{}
                    .with_size(ComponentSize{pixels(itemW), h720(20)})
                    .with_flex_direction(FlexDirection::Row)
                    .with_align_items(AlignItems::Center)
                    .with_no_wrap()
                    .with_transparent_bg()
                    .with_debug_name("basket_item"));
            // Colored monospace file:line token, then the comment text (mock).
            float locW = resolve_to_pixels(w1280(96.0f), sw);
            div(ctx, mk(itemRow.ent(), 3),
                ComponentConfig{}
                    .with_label(c.file + ":" + std::to_string(c.line))
                    .with_size(ComponentSize{pixels(locW), h720(20)})
                    .with_custom_text_color(theme::BUTTON_PRIMARY)
                    .with_font("mono", h720(11.0f))
                    .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
                    .with_debug_name("basket_item_loc"));
            div(ctx, mk(itemRow.ent(), 0),
                ComponentConfig{}
                    .with_label(c.text)
                    .with_size(ComponentSize{pixels(txtW - locW), h720(20)})
                    .with_custom_text_color(theme::TEXT_SECONDARY)
                    .with_font_size(afterhours::ui::FontSize::Small)
                    .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
                    .with_debug_name("basket_item_text"));
            auto rmBtn = button(ctx, mk(itemRow.ent(), 1),
                preset::Button("x")
                    .with_size(ComponentSize{pixels(18), h720(18)})
                    .with_custom_background(afterhours::Color{60, 60, 65, 255})
                    .with_custom_text_color(theme::STATUS_DELETED)
                    .with_font_size(afterhours::ui::FontSize::Small)
                    .with_debug_name("basket_item_remove"));
            if (rmBtn) removeIdx = i;
        }
    }
    if (removeIdx >= 0 && removeIdx < static_cast<int>(review.comments.size()))
        review.comments.erase(review.comments.begin() + removeIdx);

    auto sendBtn = button(ctx, mk(panel.ent(), 900),
        preset::Button("\xe2\x8c\x98\xe2\x8f\x8e Send all feedback")
            .with_size(ComponentSize{percent(1.0f), h720(28)})
            .with_debug_name("basket_send_btn"));
    if (sendBtn) send_review(ctx, review, repo);

    auto copyBtn = button(ctx, mk(panel.ent(), 901),
        preset::Button("Copy all")
            .with_size(ComponentSize{percent(1.0f), h720(24)})
            .with_custom_background(afterhours::Color{62, 62, 64, 255})
            .with_custom_text_color(theme::TEXT_PRIMARY)
            .with_debug_name("basket_copy_btn"));
    if (copyBtn) {
        std::string branch = (repo && !repo->currentBranch.empty())
                                 ? repo->currentBranch : "HEAD";
        std::string md = build_review_markdown(review, branch);
        if (repo && !repo->repoPath.empty()) {
            std::ofstream f(review_store::markdown_path(repo->repoPath, branch));
            if (f.good()) f << md;
        }
        afterhours::clipboard::set_text(md);
        afterhours::toast::send_info(
            ctx, "Copied \xe2\x80\x94 leaves your device when you paste it", 1.5f);
    }

    div(ctx, mk(panel.ent(), 902),
        ComponentConfig{}
            .with_label("saved to your local review folder")
            .with_size(ComponentSize{percent(1.0f), h720(16)})
            .with_padding(Padding{.top = h720(4)})
            .with_custom_text_color(theme::TEXT_SECONDARY)
            .with_font_size(afterhours::ui::FontSize::Small)
            .with_alignment(TextAlignment::Center)
            .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
            .with_debug_name("basket_path_hint"));
}

struct MainContentSystem : afterhours::System<UIContext<InputAction>> {
    void for_each_with(Entity& /*ctxEntity*/, UIContext<InputAction>& ctx,
                       float) override {
        auto* layoutPtr = find_singleton<LayoutComponent>();
        if (!layoutPtr) return;
        auto& layout = *layoutPtr;

        auto* repoPtr = find_singleton<RepoComponent, ActiveTab>();

        // Esc collapses the shelf (clears the current selection) unless a menu
        // is open. Mirrors the mock's "Esc closes the diff shelf".
        if (afterhours::graphics::is_key_pressed(afterhours::keys::ESCAPE)) {
            auto* menu = find_singleton<MenuComponent>();
            bool menuOpen = menu && menu->activeMenuIndex >= 0;
            if (!menuOpen && repoPtr) {
                repoPtr->selectedFilePath.clear();
                repoPtr->selectedCommitHash.clear();
            }
        }

        Entity& uiRoot = ui_imm::getUIRootEntity();

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

        bool hasRepo = repoPtr && !repoPtr->repoPath.empty();

        // Vim-style chunk cursor: j/k/n move, a approve, c comment. Gated on
        // no text input being focused so it never eats typed characters.
        auto* reviewPtr = find_singleton<ReviewComponent, ActiveTab>();
        // Drain the review's dirty flag to durable per-repo storage. Gated off in
        // test mode so make_test_repo runs stay deterministic.
        if (reviewPtr && reviewPtr->dirty && hasRepo &&
            !app_state::testModeEnabled) {
            review_store::save_review(repoPtr->repoPath, *reviewPtr);
            reviewPtr->dirty = false;
        }
        // Cmd/Super held? (GLFW 343/347 = L/R Super) — shared by the vim cursor
        // gate and the ⌘⏎ send-all shortcut below.
        bool superDown = afterhours::graphics::is_key_down(343) ||
                         afterhours::graphics::is_key_down(347);
        if (reviewPtr && ctx.focus_id == ctx.ROOT &&
            reviewPtr->composingKey.empty() && reviewPtr->hunkCount > 0) {
            if (!superDown) {
                if (afterhours::graphics::is_key_pressed(74) ||   // J
                    afterhours::graphics::is_key_pressed(78))     // N
                    reviewPtr->cursor =
                        std::min(reviewPtr->cursor + 1, reviewPtr->hunkCount - 1);
                if (afterhours::graphics::is_key_pressed(75))     // K
                    reviewPtr->cursor = std::max(reviewPtr->cursor - 1, 0);
                if (afterhours::graphics::is_key_pressed(65))     // A
                    reviewPtr->cursorApprove = true;
                if (afterhours::graphics::is_key_pressed(67))     // C
                    reviewPtr->cursorComment = true;
            }
        }

        // Feedback basket + ⌘⏎ send-all (available whenever comments are queued).
        if (reviewPtr && !reviewPtr->comments.empty()) {
            if (superDown && afterhours::graphics::is_key_pressed(257))
                send_review(ctx, *reviewPtr, repoPtr);
            if (reviewPtr->basketOpen)
                render_basket(ctx, uiRoot, *reviewPtr, repoPtr);
        }

        // Shelf collapsed → diff pane hidden (sidebar fills the window).
        if (layout.shelfCollapsed) {
            if (layout.commandLogVisible)
                render_command_log(ctx, uiRoot, layout);
            return;
        }

        if (!hasRepo) {
            render_welcome_screen(ctx, mainBg.ent(), layout);

            if (layout.commandLogVisible) {
                render_command_log(ctx, uiRoot, layout);
            }
            if (layout.sidebarVisible) {
                render_sidebar_divider(ctx, uiRoot, layout);
            }
            return;
        }

        auto& repo = *repoPtr;
        bool hasSelectedFile = !repo.selectedFilePath.empty();
        bool hasSelectedCommit = !repo.selectedCommitHash.empty();

        // In the ballroom: show EVERY working-tree file stacked in one scroll so
        // you can approve -> scroll -> approve without reopening files. A selected
        // commit still takes over (to review/comment that commit's diff).
        if (reviewPtr && reviewPtr->reviewing && !hasSelectedCommit) {
            float diffW = layout.mainContent.width;
            if (repo.currentDiff.empty()) {
                auto done = div(ctx, mk(mainBg.ent(), 3080),
                    ComponentConfig{}
                        .with_size(ComponentSize{percent(1.0f), percent(1.0f)})
                        .with_flex_direction(FlexDirection::Column)
                        .with_justify_content(JustifyContent::Center)
                        .with_align_items(AlignItems::Center)
                        .with_transparent_bg()
                        .with_roundness(0.0f)
                        .with_debug_name("ballroom_done"));
                div(ctx, mk(done.ent(), 1),
                    ComponentConfig{}
                        .with_label("All reviewed")
                        .with_size(ComponentSize{children(), children()})
                        .with_custom_text_color(theme::STATUS_ADDED)
                        .with_font_size(afterhours::ui::FontSize::Large)
                        .with_transparent_bg()
                        .with_debug_name("ballroom_done_msg"));
                div(ctx, mk(done.ent(), 2),
                    ComponentConfig{}
                        .with_label(std::to_string(
                            static_cast<int>(reviewPtr->approvedHunks.size())) +
                            " approved this session \xc2\xb7 refresh to reset")
                        .with_size(ComponentSize{children(), children()})
                        .with_custom_text_color(theme::TEXT_SECONDARY)
                        .with_font_size(afterhours::ui::FontSize::Medium)
                        .with_transparent_bg()
                        .with_debug_name("ballroom_done_sub"));
            } else {
                // Reserve a keyboard-hint footer under the diff (mock cockpit).
                float shH = static_cast<float>(afterhours::graphics::get_screen_height());
                float keyhintH = resolve_to_pixels(h720(24.0f), shH);
                float diffH = layout.mainContent.height - keyhintH;
                if (diffH < 40.0f) diffH = layout.mainContent.height;
                ui::render_inline_diff(ctx, mainBg.ent(), repo.currentDiff,
                                       diffW, diffH, false, /*resetScroll=*/false,
                                       repo.repoPath, reviewPtr);
                div(ctx, mk(mainBg.ent(), 3090),
                    ComponentConfig{}
                        .with_label("j/k move    a approve    c comment    "
                                    "Cmd+Enter send all    esc hide")
                        .with_size(ComponentSize{percent(1.0f), pixels(keyhintH)})
                        .with_flex_direction(FlexDirection::Row)
                        .with_align_items(AlignItems::Center)
                        .with_padding(Padding{
                            .top = h720(0), .right = pixels(theme::layout::SPACE_4),
                            .bottom = h720(0), .left = pixels(theme::layout::SPACE_4)})
                        .with_custom_background(theme::SECTION_HEADER_BG)
                        .with_custom_text_color(theme::TEXT_SECONDARY)
                        .with_font("mono", h720(11.0f))
                        .with_roundness(0.0f)
                        .with_debug_name("diff_keyhint"));
            }
            if (layout.commandLogVisible) render_command_log(ctx, uiRoot, layout);
            if (layout.sidebarVisible) render_sidebar_divider(ctx, uiRoot, layout);
            return;
        }

        if (hasSelectedFile) {
            bool fileJustChanged = (repo.cachedFilePath != repo.selectedFilePath);
            if (fileJustChanged) {
                repo.cachedFilePath = repo.selectedFilePath;
            }

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

            bool reviewing = reviewPtr && reviewPtr->reviewing;
            bool selUntracked = false;
            for (auto& u : repo.untrackedFiles)
                if (u == repo.selectedFilePath ||
                    u.ends_with("/" + repo.selectedFilePath) ||
                    repo.selectedFilePath.ends_with(u)) { selUntracked = true; break; }

            // Synthesize a whole-file "new" diff only for genuinely new/untracked
            // files. During review, a tracked file with no working-tree diff was
            // just approved (staged) — don't fake a new-file diff for it.
            if (selectedDiffs.empty() && (!reviewing || selUntracked)) {
                auto synth = build_new_file_diff(repo.repoPath,
                                                  repo.selectedFilePath);
                if (synth.has_value()) {
                    selectedDiffs.push_back(std::move(*synth));
                }
            }

            if (!selectedDiffs.empty()) {
                // Viewing this file marks it "seen" at its current content.
                // Only flag dirty when the signature actually changes, else we'd
                // re-save every frame while a file is open.
                if (reviewPtr) {
                    std::string sig = diff_signature(selectedDiffs[0]);
                    std::string& slot = reviewPtr->seenSig[repo.selectedFilePath];
                    if (slot != sig) { slot = sig; reviewPtr->dirty = true; }
                }
                bool sideBySide = (layout.diffViewMode ==
                    LayoutComponent::DiffViewMode::SideBySide);
                // Pass the pane width so hunk-header action buttons
                // (Approve/Comment) reserve room instead of overflowing off-screen.
                float diffW = layout.mainContent.width;
                if (sideBySide) {
                    ui::render_side_by_side_diff(ctx, mainBg.ent(), selectedDiffs,
                                                 diffW, 0, false, fileJustChanged);
                } else {
                    auto* review = find_singleton<ReviewComponent, ActiveTab>();
                    ui::render_inline_diff(ctx, mainBg.ent(), selectedDiffs,
                                           diffW, 0, false, fileJustChanged,
                                           repo.repoPath, review);
                }
            } else if (reviewing && !selUntracked) {
                // Reviewed file fully approved (staged) — celebrate instead of
                // faking a "new file" diff. (No auto-advance: the sidebar still
                // lists what's left, so the reviewer picks the next file.)
                int approvedN = reviewPtr
                    ? static_cast<int>(reviewPtr->approvedHunks.size()) : 0;
                auto done = div(ctx, mk(mainBg.ent(), 3070),
                    ComponentConfig{}
                        .with_size(ComponentSize{percent(1.0f), percent(1.0f)})
                        .with_flex_direction(FlexDirection::Column)
                        .with_justify_content(JustifyContent::Center)
                        .with_align_items(AlignItems::Center)
                        .with_transparent_bg()
                        .with_roundness(0.0f)
                        .with_debug_name("all_reviewed"));
                div(ctx, mk(done.ent(), 1),
                    ComponentConfig{}
                        .with_label("All reviewed here")
                        .with_size(ComponentSize{children(), children()})
                        .with_custom_text_color(theme::STATUS_ADDED)
                        .with_font_size(afterhours::ui::FontSize::Large)
                        .with_transparent_bg()
                        .with_debug_name("all_reviewed_msg"));
                div(ctx, mk(done.ent(), 2),
                    ComponentConfig{}
                        .with_label(std::to_string(approvedN) +
                                    " approved this session \xc2\xb7 refresh to reset")
                        .with_size(ComponentSize{children(), children()})
                        .with_custom_text_color(theme::TEXT_SECONDARY)
                        .with_font_size(afterhours::ui::FontSize::Medium)
                        .with_transparent_bg()
                        .with_debug_name("all_reviewed_sub"));
            } else {
                // Even with no textual diff, make it obvious which file is
                // selected: show its name at the top, plus size and change
                // status (vs HEAD) so it's clear why there's nothing to show.
                const std::string& rel = repo.selectedFilePath;

                auto human_size = [](uintmax_t b) -> std::string {
                    if (b < 1024) return std::to_string(b) + " B";
                    if (b < 1024 * 1024)
                        return std::to_string((b + 512) / 1024) + " KB";
                    return std::to_string((b + 512 * 1024) / (1024 * 1024)) + " MB";
                };
                auto same_file = [](const std::string& a, const std::string& b) {
                    return a == b || a.ends_with("/" + b) ||
                           b.ends_with("/" + a) || a.ends_with(b) ||
                           b.ends_with(a);
                };
                auto status_name = [](char c) -> std::string {
                    switch (c) {
                        case 'M': return "Modified";
                        case 'A': return "Added";
                        case 'D': return "Deleted";
                        case 'R': return "Renamed";
                        case 'C': return "Copied";
                        default: return "Changed";
                    }
                };

                std::error_code ec;
                std::filesystem::path full =
                    std::filesystem::path(repo.repoPath) / rel;
                uintmax_t bytes = std::filesystem::file_size(full, ec);
                std::string sizeStr = ec ? "size unavailable" : human_size(bytes);

                std::string changeStatus;
                for (auto& f : repo.unstagedFiles)
                    if (same_file(f.path, rel)) {
                        changeStatus = status_name(f.workTreeStatus) + " (unstaged)";
                        break;
                    }
                if (changeStatus.empty())
                    for (auto& f : repo.stagedFiles)
                        if (same_file(f.path, rel)) {
                            changeStatus = status_name(f.indexStatus) + " (staged)";
                            break;
                        }
                if (changeStatus.empty())
                    for (auto& u : repo.untrackedFiles)
                        if (same_file(u, rel)) { changeStatus = "Untracked"; break; }
                if (changeStatus.empty()) changeStatus = "No changes vs HEAD";

                // File header bar (matches the diff view's file header).
                auto hdr = div(ctx, mk(mainBg.ent(), 3040),
                    ComponentConfig{}
                        .with_size(ComponentSize{percent(1.0f), h720(28)})
                        .with_flex_direction(FlexDirection::Row)
                        .with_align_items(AlignItems::Center)
                        .with_custom_background(theme::SIDEBAR_BG)
                        .with_border_bottom(theme::BORDER)
                        .with_roundness(0.0f)
                        .with_debug_name("no_diff_header"));
                div(ctx, mk(hdr.ent(), 0),
                    ComponentConfig{}
                        .with_label(rel)
                        .with_size(ComponentSize{percent(1.0f), percent(1.0f)})
                        .with_custom_text_color(theme::TEXT_PRIMARY)
                        .with_font_size(afterhours::ui::FontSize::Large)
                        .with_alignment(TextAlignment::Left)
                        .with_padding(Padding{
                            .top = h720(8), .right = w1280(0),
                            .bottom = h720(8), .left = w1280(16)})
                        .with_debug_name("no_diff_filename"));

                div(ctx, mk(mainBg.ent(), 3042),
                    ComponentConfig{}
                        .with_label(sizeStr + "   ·   " + changeStatus)
                        .with_size(ComponentSize{percent(1.0f), children()})
                        .with_custom_text_color(theme::TEXT_SECONDARY)
                        .with_font_size(afterhours::ui::FontSize::Small)
                        .with_alignment(TextAlignment::Left)
                        .with_padding(Padding{
                            .top = h720(10), .right = w1280(16),
                            .bottom = h720(2), .left = w1280(16)})
                        .with_roundness(0.0f)
                        .with_debug_name("no_diff_detail"));

                div(ctx, mk(mainBg.ent(), 3043),
                    ComponentConfig{}
                        .with_label("No diff available for this file")
                        .with_size(ComponentSize{percent(1.0f), children()})
                        .with_custom_text_color(theme::TEXT_SECONDARY)
                        .with_font_size(afterhours::ui::FontSize::Medium)
                        .with_alignment(TextAlignment::Left)
                        .with_padding(Padding{
                            .top = h720(6), .right = w1280(16),
                            .bottom = h720(6), .left = w1280(16)})
                        .with_roundness(0.0f)
                        .with_debug_name("no_diff_msg"));
            }
        } else if (hasSelectedCommit) {
            auto* detailCache = find_singleton<CommitDetailCache, ActiveTab>();
            if (detailCache) {
                render_commit_detail(ctx, mainBg.ent(), repo, *detailCache, layout,
                                     reviewPtr);
            }
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
                        .with_font_size(afterhours::ui::FontSize::Medium)
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
                        .with_font_size(afterhours::ui::FontSize::Medium)
                        .with_padding(Padding{
                            .top = h720(0), .right = w1280(8),
                            .bottom = h720(4), .left = w1280(8)})
                        .with_transparent_bg()
                        .with_custom_text_color(theme::TEXT_TERTIARY)
                        .with_alignment(TextAlignment::Center)
                        .with_roundness(0.0f)
                        .with_debug_name("empty_hint_2"));
            }

            div(ctx, mk(emptyContainer.ent(), 3030),
                ComponentConfig{}
                    .with_label("j/k navigate  Enter view  s stage  c commit")
                    .with_size(ComponentSize{children(), children()})
                    .with_font_size(afterhours::ui::FontSize::Medium)
                    .with_padding(Padding{
                        .top = h720(16), .right = w1280(8),
                        .bottom = h720(0), .left = w1280(8)})
                    .with_transparent_bg()
                    .with_custom_text_color(afterhours::Color{125, 125, 125, 255})
                    .with_alignment(TextAlignment::Center)
                    .with_roundness(0.0f)
                    .with_debug_name("empty_shortcuts"));
        }

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

            auto* lc = find_singleton<LayoutComponent>();
            if (lc) lc->sidebarWidth = newWidth;
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
                .with_font_size(afterhours::ui::FontSize::XL)
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
                .with_font_size(afterhours::ui::FontSize::Medium)
                .with_padding(Padding{.bottom = h720(24)})
                .with_transparent_bg()
                .with_custom_text_color(theme::TEXT_SECONDARY)
                .with_alignment(TextAlignment::Center)
                .with_roundness(0.0f)
                .with_debug_name("welcome_subtitle"));

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
                        .with_font_size(afterhours::ui::FontSize::Medium)
                        .with_transparent_bg()
                        .with_custom_text_color(theme::TEXT_PRIMARY)
                        .with_alignment(TextAlignment::Left)
                        .with_roundness(0.0f)
                        .with_debug_name("recent_name"));

                div(ctx, mk(row.ent(), 2),
                    ComponentConfig{}
                        .with_label(dirPath)
                        .with_size(ComponentSize{percent(1.0f), children()})
                        .with_font_size(afterhours::ui::FontSize::Small)
                        .with_transparent_bg()
                        .with_custom_text_color(afterhours::Color{100, 100, 100, 255})
                        .with_alignment(TextAlignment::Left)
                        .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
                        .with_roundness(0.0f)
                        .with_debug_name("recent_path"));

                if (row) {
                    auto* activeRepo = find_singleton<RepoComponent, ActiveTab>();
                    if (activeRepo) {
                        activeRepo->repoPath = recentRepos[ri];
                        activeRepo->refreshRequested = true;
                        Settings::get().add_recent_repo(recentRepos[ri]);
                        // Restore any saved review for the newly-opened repo.
                        auto* rv = find_singleton<ReviewComponent, ActiveTab>();
                        if (rv && !app_state::testModeEnabled)
                            review_store::load_review(activeRepo->repoPath, *rv);
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

} // namespace ecs
