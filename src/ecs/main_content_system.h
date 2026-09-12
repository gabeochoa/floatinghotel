#pragma once

#include "../util/review_anchor.h"

#include <cstring>
#include <filesystem>
#include <algorithm>
#include <fstream>

#include "../settings.h"
#include "../review_store.h"
#include "../ui/command_log.h"
#include "../ui/commit_detail.h"
#include "../ui/diff_renderer.h"
#include "../ui/full_file_view.h"
#include "../ui/file_picker.h"
#include "../ui/repo_search.h"
#include "../ui/commit_search.h"
#include "../ui/revision_comparison.h"
#include "../ui/review_snapshot.h"
#include "../ui/keyboard_shortcuts.h"
#include "../ui/zoom.h"
#include "../ui/file_tree_style.h"
#include "../ui/chrome_icons.h"
#include "../ui/text_area.h"
#include "../util/navigation.h"
#include "ui_imports.h"

namespace app_state { extern bool testModeEnabled; }

namespace ecs {

inline void persist_pending_review(UIContext<InputAction>& ctx, ReviewComponent& review,
                                    RepoComponent* repo, bool immediate = false) {
    if (app_state::testModeEnabled || !review.dirty || !repo || repo->repoPath.empty()) return;
    auto now = std::chrono::steady_clock::now();
    if (!immediate && now < review.nextSaveAttempt) return;
    if (!review_store::persist_review(review.storageRepoPath.empty() ? repo->repoPath : review.storageRepoPath, review)) {
        review.nextSaveAttempt = now + std::chrono::seconds(2);
        afterhours::toast::send_warning(ctx, "Could not save review; retrying. Keep this tab open.", 3.f);
    }
}

inline void send_review(UIContext<InputAction>& ctx, ReviewComponent& review,
                        RepoComponent* repo) {
    std::string branch = (repo && !repo->currentBranch.empty())
                             ? repo->currentBranch : "HEAD";
    std::string md = build_review_markdown(review, branch);
    if (md.empty()) { afterhours::toast::send_info(ctx, "No unresolved feedback to copy", 1.5f); return; }
    bool saved = false;
    if (repo && !repo->repoPath.empty()) {
        std::ofstream f(review_store::markdown_path(repo->repoPath, branch));
        f << md;
        f.close();
        saved = !f.fail();
    }
    afterhours::clipboard::set_text(md);
    if (saved) afterhours::toast::send_success(ctx,
        "Copied " + std::to_string(unresolved_comment_count(review)) + " comment(s). Local Markdown saved.", 4.f);
    else afterhours::toast::send_warning(ctx,
        "Feedback copied, but the local Markdown could not be saved. Paste it somewhere safe.", 4.f);
}

inline void render_basket(UIContext<InputAction>& ctx, Entity& uiRoot,
                          ReviewComponent& review, RepoComponent* repo,
                          const LayoutComponent::Rect& bounds) {
    const float zoom = ui::zoom::get();
    float panelW = bounds.width;
    float x = bounds.x;
    float y = bounds.y;
    float hgt = bounds.height;

    auto panel = div(ctx, mk(uiRoot, 7700),
        ComponentConfig{}
            .with_size(ComponentSize{pixels(panelW), pixels(hgt)})
            .with_absolute_position()
            .with_translate(x, y)
            .with_custom_background(theme::SIDEBAR_BG)
            .with_flex_direction(FlexDirection::Column)
            .with_no_wrap()
            .with_gap(pixels(8))
            .with_border_left(theme::BORDER)
            .with_padding(Padding{
                .top = pixels(12), .right = pixels(12),
                .bottom = pixels(12), .left = pixels(12)})
            .with_render_layer(6)
            .with_roundness(0.0f)
            .with_debug_name("feedback_basket"));

    auto title = div(ctx, mk(panel.ent(), 0), ComponentConfig{}
        .with_size(ComponentSize{percent(1.f), pixels(28)}).with_flex_direction(FlexDirection::Row)
        .with_gap(pixels(8)));
    div(ctx, mk(title.ent(), 0),
        ComponentConfig{}
            .with_label("Feedback  " + std::to_string(unresolved_comment_count(review)))
            .with_size(ComponentSize{expand(), pixels(28)})
            .with_custom_text_color(theme::TEXT_PRIMARY)
            .with_font_size(pixels(14))
            .with_debug_name("basket_title"));
    if (button(ctx, mk(title.ent(), 2), preset::Button("x")
            .with_size(ComponentSize{pixels(28), pixels(28)})
            .with_padding(Padding{.left = pixels(0)})
            .with_debug_name("basket_close"))) review.basketOpen = false;
    const bool hasResolved = unresolved_comment_count(review) < review.comments.size();
    if (button(ctx, mk(panel.ent(), 904), preset::Button(review.showResolved ? "Hide resolved" : "Show resolved", hasResolved)
            .with_size(ComponentSize{percent(1.f), pixels(28)}).with_font_size(pixels(12))
            .with_debug_name("basket_toggle_resolved"))) review.showResolved = !review.showResolved;

    float itemW = panelW - 24.f;
    float txtW = std::max(20.f, itemW - 16.f);
    constexpr float fontSize = 14.f;
    auto& measure = EntityHelper::get_singleton_cmp_enforce<afterhours::ui::TextMeasureCache>();
    auto list = div(ctx, mk(panel.ent(), 903),
        ComponentConfig{}
            .with_size(ComponentSize{pixels(itemW), pixels(std::max(0.f, hgt - 164.f))})
            .with_overflow(Overflow::Scroll, Axis::Y)
            .with_flex_direction(FlexDirection::Column)
            .with_no_wrap()
            .with_gap(pixels(8))
            .with_debug_name("basket_scroll"));
    std::vector<std::string> scopes;
    for (const auto& c : review.comments)
        if ((!c.resolved || review.showResolved) && std::find(scopes.begin(), scopes.end(), c.scope) == scopes.end())
            scopes.push_back(c.scope);

    int id = 1;
    int removeIdx = -1;
    for (const auto& scope : scopes) {
        std::string gh;
        if (diff_target(scope).kind != DiffTarget::Kind::Commit) {
            gh = diff_target_label(scope);
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
        div(ctx, mk(list.ent(), id++),
            ComponentConfig{}
                .with_label(gh)
                .with_size(ComponentSize{percent(1.0f), pixels(24)})
                .with_custom_text_color(theme::TEXT_SECONDARY)
                .with_font_size(pixels(12))
                .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
                .with_debug_name("basket_group"));

        for (int i = 0; i < static_cast<int>(review.comments.size()); ++i) {
            const auto& c = review.comments[i];
            if (c.scope != scope || (c.resolved && !review.showResolved)) continue;
            const std::vector<FileDiff>* anchorFiles = nullptr;
            if (repo) {
                if (c.scope == "wt") anchorFiles = &repo->currentDiff;
                else if (c.scope == "index") anchorFiles = &repo->stagedDiff;
                else if (c.scope == repo->comparisonScope()) anchorFiles = &repo->comparisonDiff;
                else if (auto* cache = find_singleton<CommitDetailCache, ActiveTab>(); cache) {
                    const auto target = diff_target(c.scope);
                    if (cache->cachedCommitHash == target.after &&
                        ((target.kind == DiffTarget::Kind::Commit && cache->cachedParentHash.empty()) ||
                         (target.kind == DiffTarget::Kind::ParentComparison && cache->cachedParentHash == target.before)))
                        anchorFiles = &cache->commitDetailDiff;
                }
            }
            auto anchor = review_anchor::locate(c, anchorFiles);
            auto commentText = c.kind == ReviewCommentKind::Comment ? c.text : review_comment_kind_label(c.kind) + ": " + c.text;
            commentText += "\nLocation: " + review_anchor::label(anchor);
            if (anchor.status == review_anchor::Status::Outdated || anchor.status == review_anchor::Status::Relocated)
                commentText += "\nSaved: " + review_anchor::preview(anchor.saved) + "\nNow: " + review_anchor::preview(anchor.current);
            float textH = afterhours::ui::measure_text_wrapped(
                measure, commentText, "mono", fontSize * zoom, (txtW - 10.f) * zoom).height / zoom + 10.f;
            bool editing = review.editingComment == i;
            if (editing) textH = 104.f;
            auto itemRow = div(ctx, mk(list.ent(), id++),
                ComponentConfig{}
                    .with_size(ComponentSize{pixels(itemW), pixels(textH + 88.f)})
                    .with_flex_direction(FlexDirection::Column)
                    .with_no_wrap()
                    .with_gap(pixels(8))
                    .with_padding(Padding{.top = pixels(8), .right = pixels(8), .bottom = pixels(8), .left = pixels(8)})
                    .with_custom_background(theme::WINDOW_BG)
                    .with_border(theme::BORDER, pixels(1))
                    .with_corner_radius(6.f).with_rounded_corners(theme::layout::ROUNDED_CORNERS)
                    .with_debug_name("basket_item"));
            auto heading = div(ctx, mk(itemRow.ent(), 4),
                ComponentConfig{}
                    .with_size(ComponentSize{pixels(txtW), pixels(28)})
                    .with_flex_direction(FlexDirection::Row)
                    .with_debug_name("basket_item_heading"));
            auto location = button(ctx, mk(heading.ent(), 3),
                ComponentConfig{}
                    .with_label((c.resolved ? "Resolved · " : "") + comment_location(c))
                    .with_size(ComponentSize{expand(), pixels(28)})
                    .with_padding(Padding{.left = pixels(0)})
                    .with_alignment(TextAlignment::Left)
                    .with_custom_background(theme::WINDOW_BG)
                    .with_custom_text_color(afterhours::Color{100, 180, 255, 255})
                    .with_font("mono", pixels(12.0f))
                    .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
                    .with_debug_name("basket_item_loc"));
            if (location && repo) {
                auto [before, after] = diff_revisions(c.scope);
                navigation::open(*repo, reading::source(c.file, c.oldSide ? before : after,
                    anchor.status == review_anchor::Status::Relocated ? anchor.line : c.line,
                    reading::review(c.scope, c.file)));
            }
            if (editing) {
                auto previousEdit = review.editingCommentText;
                ui::text_area(ctx, mk(itemRow.ent(), 5), review.editingCommentText,
                    ComponentConfig{}.with_size(ComponentSize{pixels(txtW), pixels(104)})
                        .with_font("mono", pixels(fontSize)).with_line_height(pixels(22.f))
                        .with_corner_radius(6.f).with_rounded_corners(theme::layout::ROUNDED_CORNERS)
                        .with_word_wrap(true).with_overflow(Overflow::Hidden)
                        .with_debug_name("basket_edit_input"));
                if (previousEdit != review.editingCommentText) review.dirty = true;
                auto actions = div(ctx, mk(itemRow.ent(), 6), ComponentConfig{}
                    .with_size(ComponentSize{pixels(txtW), pixels(28)}).with_flex_direction(FlexDirection::Row)
                    .with_gap(pixels(8)));
                if (button(ctx, mk(actions.ent(), 0), preset::Button("Save")
                        .with_size(ComponentSize{pixels(52), pixels(28)}).with_font_size(pixels(12))
                        .with_debug_name("basket_edit_save"))) {
                    if (!save_comment_edit(review)) afterhours::toast::send_info(ctx, "A comment cannot be empty", 2.f);
                }
                if (button(ctx, mk(actions.ent(), 1), preset::Button("Cancel")
                        .with_size(ComponentSize{pixels(60), pixels(28)}).with_font_size(pixels(12))
                        .with_debug_name("basket_edit_cancel"))) {
                    review.editingComment = -1;
                    review.editingCommentText.clear();
                    review.dirty = true;
                }
                ui::render_comment_kind(ctx, actions.ent(), 2, review, true);
            } else div(ctx, mk(itemRow.ent(), 0),
                ComponentConfig{}
                    .with_label(commentText)
                    .with_size(ComponentSize{pixels(txtW), pixels(textH)})
                    .with_custom_text_color(theme::TEXT_PRIMARY)
                    .with_font("mono", pixels(fontSize))
                    .with_alignment(TextAlignment::Left)
                    .with_text_inset(5.f)
                    .with_text_overflow(afterhours::ui::TextOverflow::Wrap)
                    .with_debug_name("basket_item_text"));
            if (editing) continue;
            auto actions = div(ctx, mk(itemRow.ent(), 7), ComponentConfig{}
                .with_size(ComponentSize{pixels(txtW), pixels(28)})
                .with_flex_direction(FlexDirection::Row).with_no_wrap().with_gap(pixels(8))
                .with_debug_name("basket_item_actions"));
            if (button(ctx, mk(actions.ent(), 2), preset::Button("Edit")
                    .with_size(ComponentSize{expand(), pixels(28)}).with_font_size(pixels(12))
                    .with_custom_background(theme::BUTTON_SECONDARY).with_debug_name("basket_item_edit"))) {
                review.editingComment = i;
                review.editingCommentText = c.text;
                review.editingCommentKind = c.kind;
                review.dirty = true;
            }
            if (button(ctx, mk(actions.ent(), 7), preset::Button(c.resolved ? "Reopen" : "Resolve")
                    .with_size(ComponentSize{expand(), pixels(28)}).with_font_size(pixels(12))
                    .with_custom_background(theme::BUTTON_SECONDARY).with_debug_name("basket_item_resolve"))) {
                review.comments[i].resolved = !c.resolved;
                review.dirty = true;
            }
            auto rmBtn = button(ctx, mk(actions.ent(), 1),
                preset::Button("Delete")
                    .with_size(ComponentSize{expand(), pixels(28)})
                    .with_custom_background(afterhours::Color{60, 60, 65, 255})
                    .with_custom_text_color(theme::STATUS_DELETED)
                    .with_font_size(pixels(12))
                    .with_debug_name("basket_item_remove"));
            if (rmBtn) removeIdx = i;
        }
    }
    if (removeIdx >= 0 && removeIdx < static_cast<int>(review.comments.size())) {
        erase_comment(review, static_cast<size_t>(removeIdx));
        persist_pending_review(ctx, review, repo, true);
    }

    auto sendBtn = button(ctx, mk(panel.ent(), 900),
        preset::Button("Copy feedback", unresolved_comment_count(review) > 0)
            .with_size(ComponentSize{percent(1.0f), pixels(32)})
            .with_custom_background(theme::TEXT_ACCENT).with_custom_text_color(theme::WINDOW_BG)
            .with_debug_name("basket_send_btn"));
    if (sendBtn) send_review(ctx, review, repo);

    div(ctx, mk(panel.ent(), 902),
        ComponentConfig{}
            .with_label(review.dirty ? "Local draft pending save" : "Local draft · Cmd+Enter to copy")
            .with_size(ComponentSize{percent(1.0f), pixels(20)})
            .with_custom_text_color(theme::TEXT_SECONDARY)
            .with_font_size(pixels(12))
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

        bool shortcutsActive = ui::render_keyboard_shortcuts(ctx, layout);

        auto* repoPtr = find_singleton<RepoComponent, ActiveTab>();
        if (repoPtr) {
            if (auto* cache = find_singleton<CommitDetailCache, ActiveTab>())
                navigation::release_inactive_review(*repoPtr, *cache);
        }
        if (repoPtr) cancel_hidden_file_read(*repoPtr);
        if (repoPtr && repoPtr->hasLoadedOnce) {
            bool alt = afterhours::input::is_key_down(342) || afterhours::input::is_key_down(346);
            if (!shortcutsActive && alt && afterhours::input::is_key_pressed(263)) navigation::step(*repoPtr, -1);
            if (!shortcutsActive && alt && afterhours::input::is_key_pressed(262)) navigation::step(*repoPtr, 1);
            if (repoPtr->navigationEffect) {
                auto effect = *repoPtr->navigationEffect;
                repoPtr->navigationEffect.reset();
                bool dismissPicker = layout.filePickerOpen;
                if (effect.changed) {
                    layout.diffFindOpen = layout.shelfCollapsed = false;
                    layout.reviewTab = repoPtr->selectedFileStaged() ? LayoutComponent::ReviewTab::Staged : LayoutComponent::ReviewTab::ToReview;
                    if (auto* review = find_singleton<ReviewComponent, ActiveTab>()) {
                        review->sinceReviewOpen = false;
                        if (effect.reviewing && review->reviewing != *effect.reviewing) {
                            review->reviewing = *effect.reviewing;
                            review->dirty = true;
                        }
                    }
                    ui::diff_sel::reset();
                }
                layout.filePickerOpen = false;
                if (effect.changed || effect.dismissedPanel || dismissPicker) ctx.set_focus(ctx.ROOT);
            }
        }

        // Esc collapses the shelf (clears the current selection) unless a menu
        // is open. Mirrors the mock's "Esc closes the diff shelf".
        if (!shortcutsActive && afterhours::input::is_key_pressed(afterhours::keys::ESCAPE)) {
            if (layout.diffFindOpen) {
                layout.diffFindOpen = false;
                ctx.set_focus(ctx.ROOT);
                return;
            }
            if (repoPtr && repoPtr->commitSearchOpen) {
                repoPtr->commitSearchOpen = false;
                return;
            }
            if (repoPtr && repoPtr->fileHistoryOpen) {
                repoPtr->fileHistoryOpen = false;
                return;
            }
            if (repoPtr && repoPtr->repoSearchOpen) {
                repoPtr->repoSearchOpen = false;
                return;
            }
            if (layout.filePickerOpen) {
                layout.filePickerOpen = false;
                return;
            }
            if (repoPtr && source_tab_active(*repoPtr)) {
                navigation::activate(*repoPtr, reading::Slot::Review);
                return;
            }
            if (repoPtr && repoPtr->comparisonOpen()) {
                navigation::open(*repoPtr, reading::review("wt"));
                return;
            }
            auto* menu = find_singleton<MenuComponent>();
            bool menuOpen = menu && menu->activeMenuIndex >= 0;
            if (!menuOpen && repoPtr) {
                if (!repoPtr->reviewQueueScope.empty()) close_review_queue(*repoPtr);
                else {
                    navigation::open(*repoPtr, reading::review("wt"));
                }
            }
        }

        Entity& uiRoot = ui_imm::getUIRootEntity();

        if (repoPtr && layout.contentTabs.height > 0.f) {
            auto tabs = div(ctx, mk(uiRoot, 2990), ComponentConfig{}
                .with_size(ComponentSize{pixels(layout.contentTabs.width), pixels(layout.contentTabs.height)})
                .with_absolute_position().with_translate(layout.contentTabs.x, layout.contentTabs.y).with_skip_grid_snap()
                .with_custom_background(theme::SIDEBAR_BG).with_border_bottom(theme::BORDER)
                .with_flex_direction(FlexDirection::Row).with_no_wrap()
                .with_overflow(Overflow::Hidden).with_debug_name("content_tabs"));
            const auto& workspace = repoPtr->workspace();
            const auto* recentReview = workspace.document(workspace.review());
            const auto* recentSource = workspace.recent(reading::Slot::Source);
            std::optional<reading::DocumentId> activate;
            bool closeSource = false;
            for (const auto& document : workspace.documents()) {
                const auto* source = std::get_if<reading::SourceLocation>(&document.location);
                const bool active = document.id == workspace.active_id();
                std::string title;
                if (source) title = std::filesystem::path(source->destination.path).filename().string();
                else {
                    const auto& review = std::get<reading::ReviewLocation>(document.location);
                    if (const auto* commit = std::get_if<reading::CommitReview>(&review.destination))
                        title = "Commit " + reading::revision_text(commit->commit).substr(0, 7);
                    else if (const auto* changes = std::get_if<reading::WorkingChanges>(&review.destination))
                        title = changes->staged ? "Staged changes" : "Working changes";
                    else title = "Comparison";
                }
                const float textWidth = afterhours::ui::measure_text_line(title, afterhours::ui::UIComponent::DEFAULT_FONT,
                    14.f * ui::zoom::get()).x / ui::zoom::get();
                const float available = layout.contentTabs.width / static_cast<float>(workspace.documents().size());
                const float width = std::min(textWidth + 60.f, std::max(80.f, available));
                auto tab = button(ctx, mk(tabs.ent(), static_cast<int>(document.id.value)), preset::Button("")
                    .with_size(ComponentSize{pixels(width), percent(1.f)})
                    .with_padding(Padding{.top = pixels(0), .right = pixels(10), .bottom = pixels(0), .left = pixels(10)})
                    .with_flex_direction(FlexDirection::Row).with_align_items(AlignItems::Center)
                    .with_gap(pixels(6)).with_no_wrap()
                    .with_custom_background(active ? theme::WINDOW_BG : theme::SIDEBAR_BG)
                    .with_custom_text_color(active ? theme::TEXT_PRIMARY : theme::TEXT_SECONDARY)
                    .with_roundness(0.f).with_corner_radius(0.f)
                    .with_debug_name("content_document_" + std::to_string(document.id.value)));
                if (source) div(ctx, mk(tab.ent(), 10), ComponentConfig{}
                    .with_label(ui::file_tree_style::type_marker(source->destination.path))
                    .with_size(ComponentSize{pixels(24), pixels(28)}).with_font("mono", pixels(12))
                    .with_custom_text_color(theme::TEXT_ACCENT).with_debug_name("source_tab_type"));
                else ui::chrome_icon(ctx, mk(tab.ent(), 10), ui::ChromeIcon::Commit, theme::TEXT_SECONDARY, "commit_tab_icon");
                std::string alias;
                if (recentReview && document.id == recentReview->id) alias = "content_review_tab";
                if (recentSource && document.id == recentSource->id) alias = "content_source_tab";
                div(ctx, mk(tab.ent(), 11), ComponentConfig{}.with_label(title)
                    .with_size(ComponentSize{expand(), pixels(28)}).with_font_size(pixels(14))
                    .with_custom_text_color(active ? theme::TEXT_PRIMARY : theme::TEXT_SECONDARY)
                    .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis).with_debug_name(alias));
                if (active) div(ctx, mk(tab.ent(), 12), ComponentConfig{}
                    .with_size(ComponentSize{pixels(std::max(0.f, width - 20.f)), pixels(2)})
                    .with_absolute_position(10.f, layout.contentTabs.height - 2.f)
                    .with_custom_background(theme::SELECTED_ACCENT).with_debug_name("content_tab_indicator"));
                ui::set_tooltip(tab.ent(), source ? source->destination.path + " @ " +
                    (reading::revision_text(source->destination.revision).empty() ? "working tree" : reading::revision_text(source->destination.revision)) : title);
                if (tab) activate = document.id;
                if (source && active && button(ctx, mk(tab.ent(), 20), preset::Button("×")
                        .with_size(ComponentSize{pixels(24), percent(1.f)})
                        .with_debug_name("content_source_close"))) closeSource = true;
            }
            if (closeSource) navigation::close_source(*repoPtr);
            else if (activate) navigation::activate(*repoPtr, *activate);
        }

        auto mainBg = div(ctx, mk(uiRoot, 3000),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(layout.mainContent.width),
                                        pixels(layout.mainContent.height)})
                .with_absolute_position()
                .with_translate(layout.mainContent.x, layout.mainContent.y)
                .with_skip_grid_snap()
                .with_custom_background(theme::WINDOW_BG)
                .with_flex_direction(FlexDirection::Column)
                .with_overflow(Overflow::Hidden)
                .with_roundness(0.0f)
                .with_debug_name("main_content"));

        bool hasRepo = repoPtr && !repoPtr->repoPath.empty();
        if (hasRepo) {
            if (repoPtr->fullFilePath().empty()) {
                repoPtr->blameFuture = {};
                repoPtr->blameOpen = false;
            }
            if (!repoPtr->repoSearchOpen) {
                repoPtr->repoSearchFuture = {};
                repoPtr->repoSearchPreviewFuture = {};
            }
            if (!repoPtr->fileHistoryOpen) repoPtr->fileHistoryFuture = {};
            if (!repoPtr->commitSearchOpen) repoPtr->commitSearchFuture = {};
            if (!repoPtr->comparisonOpen()) repoPtr->comparisonFuture = {};
            if (repoPtr->selectedCommitHash().empty()) {
                if (auto* detail = find_singleton<CommitDetailCache, ActiveTab>();
                    detail && (detail->patchFuture.valid() || detail->infoFuture.valid())) {
                    detail->patchFuture = {};
                    detail->infoFuture = {};
                    detail->cachedCommitHash.clear();
                }
            }
        }

        // Vim-style chunk cursor: j/k/n move, a approve, c comment. Gated on
        // no text input being focused so it never eats typed characters.
        auto* reviewPtr = find_singleton<ReviewComponent, ActiveTab>();
        if (reviewPtr && hasRepo && repoPtr->hasLoadedOnce && !repoPtr->isRefreshing && !repoPtr->refreshRequested) {
            auto scope = selected_review_storage_scope(*repoPtr, *reviewPtr);
            if (reviewPtr->storageScope != scope || reviewPtr->storageRepoPath != repoPtr->repoPath) {
                bool initialScope = reviewPtr->storageScope.empty();
                if (!review_store::switch_review_scope(repoPtr->repoPath, scope, *reviewPtr, !app_state::testModeEnabled)) {
                    div(ctx, mk(mainBg.ent(), 591000), ComponentConfig{}
                        .with_label("Cannot save the previous review. Keep this tab open and retry after checking storage.")
                        .with_size(ComponentSize{percent(1.f), pixels(80)}).with_font_size(pixels(14)));
                    return;
                }
                navigation::restore_draft(*repoPtr, *reviewPtr);
                if (initialScope && !app_state::testModeEnabled && std::filesystem::exists(review_store::review_path(repoPtr->repoPath)))
                    afterhours::toast::send_info(ctx, "Older unscoped review kept in your local review folder", 4.f);
            }
        }
        if (reviewPtr && hasRepo) {
            poll_range_diff(*repoPtr);
            poll_review_queue(*repoPtr, *reviewPtr);
            poll_review_snapshot(*reviewPtr);
            persist_pending_review(ctx, *reviewPtr, repoPtr);
        }
        if (repoPtr && !repoPtr->reviewQueueScope.empty() && repoPtr->selectedCommitHash().empty()) {
            div(ctx, mk(mainBg.ent(), 592000), ComponentConfig{}
                .with_label(repoPtr->reviewQueueFuture.valid() ? "Loading review queue..." : repoPtr->reviewQueueError)
                .with_size(ComponentSize{percent(1.f), pixels(40)}).with_font_size(pixels(12)));
            if (button(ctx, mk(mainBg.ent(), 592001), preset::Button("Close review queue")
                    .with_size(ComponentSize{children(), pixels(30)}))) close_review_queue(*repoPtr);
            return;
        }
        // Cmd/Super held? (GLFW 343/347 = L/R Super) — shared by the vim cursor
        // gate and the ⌘⏎ send-all shortcut below.
        bool superDown = afterhours::input::is_key_down(343) ||
                         afterhours::input::is_key_down(347) ||
                         afterhours::input::is_key_down(341);
        if (!shortcutsActive && superDown && afterhours::input::is_key_pressed(70)) {
            if (repoPtr && afterhours::input::is_key_down(340)) {
                repoPtr->repoSearchOpen = true;
                repoPtr->repoSearchFocus = true;
                layout.filePickerOpen = false;
            } else {
                layout.diffFindOpen = true;
                layout.diffFindFocus = true;
            }
        }
        if (!shortcutsActive && superDown && afterhours::input::is_key_pressed(80)) {
            layout.filePickerOpen = true;
            layout.filePickerFocus = true;
        }
        auto focused = afterhours::ui::UICollectionHolder::getEntityForID(ctx.focus_id);
        bool editingText = focused.valid() && focused->has<afterhours::text_input::HasTextInputState>();
        auto* keyboardMenu = find_singleton<MenuComponent>();
        bool keyboardMenuOpen = keyboardMenu && keyboardMenu->activeMenuIndex >= 0;
        if (!shortcutsActive && reviewPtr && repoPtr && !source_tab_active(*repoPtr) && !editingText && !keyboardMenuOpen &&
            reviewPtr->composingKey.empty() && reviewPtr->hunkCount > 0) {
            if (!superDown) {
                int previousCursor = reviewPtr->cursor;
                if (afterhours::input::is_key_pressed(74) ||
                    afterhours::input::is_key_pressed(78))
                    reviewPtr->cursor =
                        std::min(reviewPtr->cursor + 1, reviewPtr->hunkCount - 1);
                if (afterhours::input::is_key_pressed(75))
                    reviewPtr->cursor = std::max(reviewPtr->cursor - 1, 0);
                if (previousCursor != reviewPtr->cursor) reviewPtr->cursorMoved = true;
                if (afterhours::input::is_key_pressed(65))
                    reviewPtr->cursorApprove = true;
                if (afterhours::input::is_key_pressed(67))
                    reviewPtr->cursorComment = true;
            }
        }

        // Feedback basket + ⌘⏎ send-all (available whenever comments are queued).
        if (reviewPtr && !reviewPtr->comments.empty()) {
            if (!shortcutsActive && superDown && afterhours::input::is_key_pressed(257))
                send_review(ctx, *reviewPtr, repoPtr);
            if (reviewPtr->basketOpen && layout.feedback.width > 0)
                render_basket(ctx, uiRoot, *reviewPtr, repoPtr, layout.feedback);
        }

        // Shelf collapsed → diff pane hidden and the window shrinks to the
        // sidebar, so LayoutSystem gives the command log no rect. Building it
        // anyway made a 0x0 panel whose every child overflowed it -- a few
        // hundred layout warnings a run for a panel nobody could see. It comes
        // back with the shelf, when there is somewhere to put it.
        if (layout.shelfCollapsed) return;

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
        if (reviewPtr && reviewPtr->sinceReviewOpen) {
            render_review_snapshot(ctx, mainBg.ent(), repo, *reviewPtr, layout);
            return;
        }
        if (repo.commitSearchOpen) {
            render_commit_search(ctx, mainBg.ent(), repo, layout);
            return;
        }
        if (repo.fileHistoryOpen) {
            render_file_history(ctx, mainBg.ent(), repo, layout);
            return;
        }
        if (repo.repoSearchOpen) {
            render_repo_search(ctx, mainBg.ent(), repo, layout);
            return;
        }
        if (layout.filePickerOpen) {
            render_file_picker(ctx, mainBg.ent(), repo, layout);
            return;
        }
        if (source_tab_active(repo)) {
            render_full_file(ctx, mainBg.ent(), repo, layout);
            return;
        }
        if (repo.comparisonOpen()) {
            render_revision_comparison(ctx, mainBg.ent(), repo, layout, reviewPtr);
            return;
        }
        bool hasSelectedFile = std::holds_alternative<reading::WorkingChanges>(repo.workspace().review().destination) &&
            !repo.selectedFilePath().empty();
        bool hasSelectedCommit = !repo.selectedCommitHash().empty();

        // In the ballroom: show EVERY working-tree file stacked in one scroll so
        // you can approve -> scroll -> approve without reopening files. A selected
        // commit still takes over (to review/comment that commit's diff).
        if (reviewPtr && reviewPtr->reviewing && !hasSelectedCommit && !(hasSelectedFile && repo.selectedFileStaged())) {
            float diffW = layout.mainContent.width;
            auto baselineActions = div(ctx, mk(mainBg.ent(), 592010), ComponentConfig{}
                .with_size(ComponentSize{percent(1.f), pixels(30)}).with_flex_direction(FlexDirection::Row)
                .with_gap(pixels(4)));
            if (!reviewPtr->snapshotFuture.valid()) {
                if (button(ctx, mk(baselineActions.ent(), 0), preset::Button("Save review baseline")
                    .with_size(ComponentSize{pixels(165), pixels(28)}).with_debug_name("save_review_baseline")))
                    start_review_snapshot(repo, *reviewPtr, true);
                if (!reviewPtr->baselineSnapshot.empty() && button(ctx, mk(baselineActions.ent(), 1), preset::Button("Since last review")
                    .with_size(ComponentSize{pixels(145), pixels(28)}).with_debug_name("since_last_review"))) {
                    reviewPtr->sinceReviewOpen = true;
                    start_review_snapshot(repo, *reviewPtr, false);
                }
            }
            div(ctx, mk(baselineActions.ent(), 2), ComponentConfig{}
                .with_label(reviewPtr->snapshotFuture.valid() ? "Saving contents..." : reviewPtr->snapshotError)
                .with_size(ComponentSize{expand(), pixels(28)}).with_font_size(pixels(12)));
            if (repo.currentDiff.empty()) {
                auto done = div(ctx, mk(mainBg.ent(), 3080),
                    ComponentConfig{}
                        .with_size(ComponentSize{percent(1.0f), pixels(layout.mainContent.height - 30.f)})
                        .with_flex_direction(FlexDirection::Column)
                        .with_justify_content(JustifyContent::Center)
                        .with_align_items(AlignItems::Center)
                        .with_transparent_bg()
                        .with_roundness(0.0f)
                        .with_debug_name("ballroom_done"));
                div(ctx, mk(done.ent(), 1),
                    ComponentConfig{}
                        .with_label("Working tree matches the index")
                        .with_size(ComponentSize{children(), children()})
                        .with_custom_text_color(theme::STATUS_ADDED)
                        .with_font_size(pixels(16))
                        .with_transparent_bg()
                        .with_debug_name("ballroom_done_msg"));
                div(ctx, mk(done.ent(), 2),
                    ComponentConfig{}
                        .with_label(std::to_string(
                            static_cast<int>(reviewPtr->approvedHunks.size())) +
                            " review approvals saved")
                        .with_size(ComponentSize{children(), children()})
                        .with_custom_text_color(theme::TEXT_SECONDARY)
                        .with_font_size(pixels(14))
                        .with_transparent_bg()
                        .with_debug_name("ballroom_done_sub"));
            } else {
                // Reserve a keyboard-hint footer under the diff (mock cockpit).
                constexpr float keyhintH = 24.f;
                float diffH = layout.mainContent.height - keyhintH - 30.f;
                if (diffH < 40.0f) diffH = layout.mainContent.height;
                ui::render_diff(ctx, mainBg.ent(), repo.currentDiff,
                                       diffW, diffH, false, false,
                                       layout.diffViewMode == LayoutComponent::DiffViewMode::SideBySide,
                                       repo.repoPath, reviewPtr);
                div(ctx, mk(mainBg.ent(), 3090),
                    ComponentConfig{}
                        .with_label("j/k move    a approve    c comment    "
                                    "Cmd+Enter copy feedback    esc hide")
                        .with_size(ComponentSize{percent(1.0f), pixels(keyhintH)})
                        .with_flex_direction(FlexDirection::Row)
                        .with_align_items(AlignItems::Center)
                        .with_padding(Padding{
                            .top = h720(0), .right = pixels(theme::layout::SPACE_4),
                            .bottom = h720(0), .left = pixels(theme::layout::SPACE_4)})
                        .with_custom_background(theme::SECTION_HEADER_BG)
                        .with_custom_text_color(theme::TEXT_SECONDARY)
                        .with_font("mono", pixels(11.f))
                        .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
                        .with_roundness(0.0f)
                        .with_debug_name("diff_keyhint"));
            }
            if (layout.commandLogVisible) render_command_log(ctx, uiRoot, layout);
            if (layout.sidebarVisible) render_sidebar_divider(ctx, uiRoot, layout);
            return;
        }

        if (hasSelectedFile) {
            bool fileJustChanged = (repo.cachedFilePath != repo.selectedFilePath());
            if (fileJustChanged) {
                repo.cachedFilePath = repo.selectedFilePath();
            }

            std::vector<FileDiff> selectedDiffs;
            const auto& fileDiffs = repo.selectedFileStaged() ? repo.stagedDiff : repo.currentDiff;
            for (auto& d : fileDiffs) {
                if (d.filePath == repo.selectedFilePath() ||
                    d.filePath.ends_with("/" + repo.selectedFilePath()) ||
                    repo.selectedFilePath().ends_with("/" + d.filePath) ||
                    repo.selectedFilePath().ends_with(d.filePath)) {
                    selectedDiffs.push_back(d);
                    break;
                }
            }

            bool reviewing = reviewPtr && reviewPtr->reviewing;
            bool selUntracked = false;
            for (auto& u : repo.untrackedFiles)
                if (u == repo.selectedFilePath() ||
                    u.ends_with("/" + repo.selectedFilePath()) ||
                    repo.selectedFilePath().ends_with(u)) { selUntracked = true; break; }

            // Synthesize a whole-file "new" diff only for genuinely new/untracked
            // files. During review, a tracked file with no working-tree diff was
            // just approved (staged) — don't fake a new-file diff for it.
            if (selectedDiffs.empty() && selUntracked && !repo.selectedFileStaged()) {
                std::string key = repo.repoPath + "\n" + repo.selectedFilePath() + "\n" + std::to_string(repo.dataGeneration);
                if (repo.untrackedDiffKey != key) {
                    repo.untrackedDiffKey = std::move(key);
                    repo.untrackedDiff = build_new_file_diff(repo.repoPath, repo.selectedFilePath());
                }
                if (repo.untrackedDiff) selectedDiffs.push_back(*repo.untrackedDiff);
            } else {
                repo.untrackedDiffKey.clear();
                repo.untrackedDiff.reset();
            }

            if (!selectedDiffs.empty()) {
                // Viewing this file marks it "seen" at its current content.
                // Only flag dirty when the signature actually changes, else we'd
                // re-save every frame while a file is open.
                if (reviewPtr) {
                    std::string sig = ui::diff_metrics().signature(selectedDiffs[0]);
                    std::string& slot = reviewPtr->seenSig[repo.selectedFilePath()];
                    if (slot != sig) { slot = sig; reviewPtr->dirty = true; }
                }
                bool sideBySide = (layout.diffViewMode ==
                    LayoutComponent::DiffViewMode::SideBySide);
                // Pass the pane width so hunk-header action buttons
                // (Approve/Comment) reserve room instead of overflowing off-screen.
                float diffW = layout.mainContent.width;
                auto* review = find_singleton<ReviewComponent, ActiveTab>();
                ui::render_diff(ctx, mainBg.ent(), selectedDiffs,
                               diffW, layout.mainContent.height, false, fileJustChanged, sideBySide,
                               repo.repoPath, repo.selectedFileStaged() ? nullptr : review,
                               repo.selectedFileStaged() ? "index" : "wt");
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
                        .with_font_size(pixels(16))
                        .with_transparent_bg()
                        .with_debug_name("all_reviewed_msg"));
                div(ctx, mk(done.ent(), 2),
                    ComponentConfig{}
                        .with_label(std::to_string(approvedN) +
                                    " approved this session \xc2\xb7 refresh to reset")
                        .with_size(ComponentSize{children(), children()})
                        .with_custom_text_color(theme::TEXT_SECONDARY)
                        .with_font_size(pixels(14))
                        .with_transparent_bg()
                        .with_debug_name("all_reviewed_sub"));
            } else {
                // Even with no textual diff, make it obvious which file is
                // selected: show its name at the top, plus size and change
                // status (vs HEAD) so it's clear why there's nothing to show.
                const std::string& rel = repo.selectedFilePath();

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
                        .with_font_size(pixels(16))
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
                        .with_font_size(pixels(12))
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
                        .with_font_size(pixels(14))
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
                        .with_font_size(pixels(14))
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
                        .with_font_size(pixels(16))
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
                        .with_font_size(pixels(14))
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
                    .with_font_size(pixels(14))
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
        // imm::divider handles the drag: it reports this frame's travel in
        // rect() space, which is already letterbox-corrected. The old form
        // read the raw backend mouse position and undid letterboxing by hand,
        // and jumped the bar whenever you grabbed it off centre.
        auto vDivider = afterhours::ui::imm::divider(
            ctx, mk(uiRoot, 3100), afterhours::ui::Axis::X,
            ComponentConfig{}
                .with_size(ComponentSize{pixels(8), pixels(layout.sidebar.height)})
                .with_absolute_position()
                .with_translate(layout.sidebar.width, layout.sidebar.y)
                .with_custom_background(theme::WINDOW_BG)
                .with_border_left(theme::BORDER)
                .with_border_right(theme::BORDER)
                .with_roundness(0.0f)
                .with_debug_name("sidebar_divider"));

        if (vDivider) {
            const float delta = vDivider.as<float>() / ui::zoom::get();
            auto* lc = find_singleton<LayoutComponent>();
            if (lc)
                lc->sidebarWidth =
                    std::clamp(lc->sidebarWidth + delta,
                               layout.sidebarMinWidth, 640.0f);
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
                .with_font_size(pixels(14))
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

        std::vector<std::string> openPaths;
        afterhours::EntityQuery({.force_merge = true})
            .whereHasComponent<Tab, RepoComponent>()
            .for_each_stream([&](afterhours::Entity& t) {
                auto& r = t.get<RepoComponent>();
                if (!r.repoPath.empty()) {
                    openPaths.push_back(canonicalize(r.repoPath));
                }
            });

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
                    .with_font_size(pixels(14))
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
                        .with_corner_radius(4.0f)
                        .with_margin(Margin{.bottom = h720(2)})
                        .with_cursor(afterhours::ui::CursorType::Pointer)
                        .with_debug_name("recent_repo_" + basename));

                div(ctx, mk(row.ent(), 1),
                    ComponentConfig{}
                        .with_label(basename)
                        .with_size(ComponentSize{percent(1.0f), children()})
                        .with_font_size(pixels(14))
                        .with_transparent_bg()
                        .with_custom_text_color(theme::TEXT_PRIMARY)
                        .with_alignment(TextAlignment::Left)
                        .with_roundness(0.0f)
                        .with_debug_name("recent_name"));

                div(ctx, mk(row.ent(), 2),
                    ComponentConfig{}
                        .with_label(dirPath)
                        .with_size(ComponentSize{percent(1.0f), children()})
                        .with_font_size(pixels(12))
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
                    }
                }
            }
        } else {
            div(ctx, mk(container.ent(), 10),
                ComponentConfig{}
                    .with_label("No recent repositories")
                    .with_size(ComponentSize{children(), children()})
                    .with_font_size(pixels(14))
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
                .with_font_size(pixels(14))
                .with_padding(Padding{.top = h720(20)})
                .with_transparent_bg()
                .with_custom_text_color(afterhours::Color{60, 60, 60, 255})
                .with_alignment(TextAlignment::Center)
                .with_roundness(0.0f)
                .with_debug_name("welcome_hint"));
    }
};

} // namespace ecs
