#pragma once

#include "../ui/repository_actions.h"

#include "../git/commit_patch.h"

#include <algorithm>
#include <cstdlib>
#include <ctime>
#include <numeric>
#include <optional>
#include <string>
#include <vector>

#include "../git/git_commands.h"
#include "../git/git_runner.h"
#include "../settings.h"
#include "../util/git_helpers.h"
#include "../util/file_tree.h"
#include "../util/commit_graph.h"
#include "../util/commit_message.h"
#include "../util/review_anchor.h"
#include "network_ops_system.h"
#include "agent_panel.h"
#include "ui_imports.h"
#include "../ui/context_menu.h"
#include "../ui/focus.h"
#include "../ui/repo_search.h"
#include "../ui/file_history.h"
#include "../ui/review_snapshot.h"
#include "../ui/diff_metrics.h"
#include <afterhours/src/plugins/ui/imm_virtual_list.h>
#include <afterhours/src/plugins/ui/text_input/text_area.h>
#include "../ui/zoom.h"
#include "../ui/file_tree_style.h"
#include "../ui/chrome_icons.h"

#include <afterhours/src/plugins/clipboard.h>
#include <afterhours/src/plugins/modal.h>
#include <afterhours/src/plugins/ui/text_input/text_input.h>

namespace ecs {

namespace sidebar_detail {

// Extract just the filename (basename) from a path. Git reports untracked
// directories with a trailing slash (e.g. "docs/mocks/"); return the folder
// name with the slash kept so it reads as a directory rather than blank.
inline std::string basename_from_path(const std::string& path) {
    bool isDir = !path.empty() && path.back() == '/';
    std::string p = isDir ? path.substr(0, path.size() - 1) : path;
    auto slashPos = p.find_last_of('/');
    std::string name = (slashPos == std::string::npos) ? p : p.substr(slashPos + 1);
    return isDir ? name + "/" : name;
}

// Extract short directory context: just the immediate parent dir
inline std::string dir_from_path(const std::string& path) {
    std::string base = (!path.empty() && path.back() == '/')
                           ? path.substr(0, path.size() - 1) : path;
    auto slashPos = base.find_last_of('/');
    if (slashPos == std::string::npos) return "";
    std::string dir = base.substr(0, slashPos);
    // Remove leading "./" or "../" prefixes for cleaner display
    while (dir.size() >= 2 && dir[0] == '.' && (dir[1] == '/' || (dir[1] == '.' && dir.size() >= 3 && dir[2] == '/'))) {
        dir = (dir[1] == '/') ? dir.substr(2) : dir.substr(3);
    }
    return dir;
}

} // namespace sidebar_detail

// Commit log helpers now live in src/util/git_helpers.h
namespace commit_log_detail = git_helpers;

// Compact relative age ("now", "5m", "3h", "2d", "6w", "4mo", "2y") from a
// git %aI ISO-8601 timestamp (e.g. "2026-07-28T23:15:00-07:00"). The tz offset
// is ignored, so results can be off by a few hours -- fine for coarse display.
inline std::string relative_time(const std::string& iso) {
    if (iso.size() < 19) return "";
    std::tm tm{};
    tm.tm_year = std::atoi(iso.substr(0, 4).c_str()) - 1900;
    tm.tm_mon  = std::atoi(iso.substr(5, 2).c_str()) - 1;
    tm.tm_mday = std::atoi(iso.substr(8, 2).c_str());
    tm.tm_hour = std::atoi(iso.substr(11, 2).c_str());
    tm.tm_min  = std::atoi(iso.substr(14, 2).c_str());
    tm.tm_sec  = std::atoi(iso.substr(17, 2).c_str());
    tm.tm_isdst = -1;
    std::time_t t = std::mktime(&tm);
    if (t == static_cast<std::time_t>(-1)) return "";
    double secs = std::difftime(std::time(nullptr), t);
    if (secs < 0) secs = 0;
    long s = static_cast<long>(secs);
    if (s < 60) return "now";
    long m = s / 60;
    if (m < 60) return std::to_string(m) + "m";
    long h = m / 60;
    if (h < 24) return std::to_string(h) + "h";
    long d = h / 24;
    if (d < 7) return std::to_string(d) + "d";
    if (d < 30) return std::to_string(d / 7) + "w";
    if (d < 365) return std::to_string(d / 30) + "mo";
    return std::to_string(d / 365) + "y";
}

// ---- Commit workflow helpers (T030) ----
namespace commit_workflow {

// Build a commit message from subject + body
inline std::string build_message(const std::string& subject,
                                  const std::string& body) {
    if (body.empty()) return subject;
    return subject + "\n\n" + body;
}

// Start a commit off the UI thread (staging first when asked); the git
// work runs async and poll_commit applies the outcome. Returns false when
// the message is not committable or a commit is already in flight.
inline bool begin_commit(RepoComponent& repo,
                         CommitEditorComponent& editor,
                         bool stageAllFirst) {
    if (editor.commitFuture.valid()) return false;
    std::string message = build_message(editor.subject, editor.body);
    if (message.empty()) message = "Update";
    // git commit's editor refuses an untouched template; -m would not.
    if (!editor.templateText.empty() && commit_message::strip_template(message) == editor.templateText) {
        if (auto* menu = find_singleton<MenuComponent>())
            menu->pendingToasts.push_back({"Fill in the commit template first", MenuComponent::Notice::Kind::Error});
        editor.abort_commit();
        return false;
    }
    editor.commitMessage = message;
    editor.commitFuture = async_work::launch(
        [path = repo.repoPath, message, stageAllFirst](std::stop_token stop) {
            CommitEditorComponent::CommitOutcome outcome;
            if (stageAllFirst) {
                outcome.result = git::stage_all(path);
                if (!outcome.result.success()) { outcome.stageFailed = true; return outcome; }
            }
            outcome.result = git::git_commit(path, message);
            return outcome;
        }, async_work::Priority::Foreground,
        CommitEditorComponent::CommitOutcome{true, git::GitResult{{"", "Background queue is full; commit again", -1}}});
    editor.begin_committing();
    return true;
}

// Finish an in-flight async commit (called every frame from the sidebar).
inline void poll_commit(RepoComponent& repo, CommitEditorComponent& editor) {
    if (!editor.commitFuture.valid() ||
        editor.commitFuture.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
    auto outcome = editor.commitFuture.get();
    editor.commitFuture = {};
    if (outcome.stageFailed) { editor.abort_commit(); toast_on_git_failure(outcome.result, "Stage All"); return; }
    const auto& result = outcome.result;
    if (result.success()) {
        editor.subject = editor.templateText;
        editor.placeCaret = 0;
        editor.body.clear();
        editor.isVisible = false;
        repo.request_refresh();
        auto* review = find_singleton<ReviewComponent, ActiveTab>();
        if (review && std::ranges::any_of(review->comments, [](const auto& c) { return c.scope == "wt" || c.scope == "index"; })) {
            auto patch = git::read_commit_patch({repo.repoPath, "HEAD"});
            auto carried = patch.error.empty() ? review_anchor::carry_to_commit(review->comments, patch.resolvedCommit, patch.files)
                                               : review_anchor::Carried{};
            if (carried.moved) review->mark_dirty();
            auto* menu = find_singleton<MenuComponent>();
            if (menu && carried.moved) menu->pendingToasts.push_back({"Moved " + std::to_string(carried.moved) +
                " comment" + (carried.moved == 1 ? "" : "s") + " to commit " + patch.resolvedCommit.substr(0, 7), MenuComponent::Notice::Kind::Info});
            if (menu && carried.ambiguous) menu->pendingToasts.push_back({std::to_string(carried.ambiguous) +
                " comment" + (carried.ambiguous == 1 ? "" : "s") + " left on the working tree: matches several places in the commit",
                MenuComponent::Notice::Kind::Info});
        }
        editor.finish_commit();
        return;
    }
    editor.abort_commit();
    toast_on_git_failure(result, "Commit");
}

// Handle the commit request — checks unstaged policy and either
// commits directly or opens the dialog
inline void handle_commit_request(RepoComponent& repo,
                                   CommitEditorComponent& editor) {
    bool hasStaged = !repo.stagedFiles.empty();
    bool hasUnstaged = !repo.unstagedFiles.empty() ||
                       !repo.untrackedFiles.empty();

    if (!hasStaged) return; // Nothing to commit yet: stay Committing (pending)

    if (!hasUnstaged) {
        // No unstaged changes — commit directly
        begin_commit(repo, editor, false);
        return;
    }

    // Both staged and unstaged exist — check policy. The one-shot
    // split-button override is consumed here exactly as before (only
    // on this both-present path), so the main button keeps Ask.
    const auto policy = editor.commitPolicyOverride.value_or(editor.unstagedPolicy);
    editor.commitPolicyOverride.reset();
    switch (policy) {
        case CommitEditorComponent::UnstagedPolicy::StageAll:
            begin_commit(repo, editor, true);
            break;
        case CommitEditorComponent::UnstagedPolicy::CommitStagedOnly:
            begin_commit(repo, editor, false);
            break;
        case CommitEditorComponent::UnstagedPolicy::Ask:
        default:
            // Open the dialog
            editor.await_policy();
            break;
    }
}

// Save the unstaged policy to settings
inline void save_policy(CommitEditorComponent::UnstagedPolicy policy) {
    switch (policy) {
        case CommitEditorComponent::UnstagedPolicy::StageAll:
            Settings::get().set_unstaged_policy("stage_all");
            break;
        case CommitEditorComponent::UnstagedPolicy::CommitStagedOnly:
            Settings::get().set_unstaged_policy("staged_only");
            break;
        case CommitEditorComponent::UnstagedPolicy::Ask:
        default:
            Settings::get().set_unstaged_policy("ask");
            break;
    }
}

} // namespace commit_workflow

// SidebarSystem: Renders the sidebar with branch header, changed files list,
// and commit log panel. Files are grouped into Staged Changes, Changes, and
// Untracked sections with status badges and change stats.
struct SidebarSystem : afterhours::System<UIContext<InputAction>> {
    void for_each_with(Entity& /*ctxEntity*/, UIContext<InputAction>& ctx,
                       float) override {
        auto* layoutPtr = find_singleton<LayoutComponent>();
        if (!layoutPtr) return;
        auto& layout = *layoutPtr;

        auto* repoPtr = find_singleton<RepoComponent, ActiveTab>();
        bool prefetchAvailable = true;
        for (Entity& entity : afterhours::EntityQuery({.force_merge = true}).whereHasComponent<RepoComponent>().gen()) {
            if (entity.has<ActiveTab>()) continue;
            auto& state = entity.get<RepoComponent>().commitPrefetch;
            state.observe({});
            prefetchAvailable &= !state.future.valid();
        }
        hoveredCommit_.clear();
        focusedCommit_.clear();
        if (lastPrefetchMouse_.x != ctx.mouse.pos.x || lastPrefetchMouse_.y != ctx.mouse.pos.y) hoverPrefetch_ = true;
        lastPrefetchMouse_ = ctx.mouse.pos;
        if (repoPtr && (!layout.sidebarVisible || layout.sidebar.width <= 0.f || repoPtr->repo_search_open()))
            repoPtr->commitPrefetch.observe({});
        if ((!layout.sidebarVisible && !(repoPtr && repoPtr->repo_search_open())) || layout.sidebar.width <= 0.f) return;

        // Working tree clean = nothing staged/unstaged/untracked. Used to hide
        // the commit area and shrink the empty files pane (#8, #24).
        bool treeClean = repoPtr && repoPtr->stagedFiles.empty() &&
                         repoPtr->unstagedFiles.empty() &&
                         repoPtr->untrackedFiles.empty();

        Entity& uiRoot = ui_imm::getUIRootEntity();

        // === Sidebar background (absolute, contains all sidebar sections via flow) ===
        auto sidebarRoot = div(ctx, mk(uiRoot, 2000),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(layout.sidebar.width),
                                        pixels(layout.sidebar.height)})
                .with_absolute_position()
                .with_translate(layout.sidebar.x, layout.sidebar.y)
                .with_custom_background(theme::SIDEBAR_BG)
                .with_border_right(theme::BORDER)
                .with_flex_direction(FlexDirection::Column)
                .with_overflow(Overflow::Hidden, Axis::Y)
                .with_roundness(0.0f)
                .with_debug_name("sidebar_bg"));
        if (repoPtr) ui::bind_focus_region(sidebarRoot.ent(), *repoPtr, reading::focus::Region::Tree);

        float sidebarW = layout.sidebar.width;
        sidebarPixelWidth_ = sidebarW;  // Set early for all child rendering

        const float zoom = ::ui::zoom::get();
        const bool historyCollapsed = repoPtr && Settings::get().section_collapsed(repoPtr->repoPath, "history");
        float filesH = 0.f;
        float commitsH = 0.f;
        float splitAvailable = std::max(0.f, layout.sidebar.height - LayoutComponent::kCommitSplitterHeight);

        render_repo_header(ctx, sidebarRoot.ent(), repoPtr);
        // No repository open: hide dead repo chrome (sync row, mode tabs,
        // progress, commit box, history) and let the Welcome picker own the
        // main canvas. A single neutral sidebar note replaces the repeated
        // "No repository open" placeholders.
        if (!repoPtr || repoPtr->repoPath.empty()) {
            div(ctx, mk(sidebarRoot.ent(), 2199), ComponentConfig{}
                .with_label("Repositories")
                .with_size(ComponentSize{percent(1.f), pixels(24)})
                .with_margin(Margin{.top = pixels(12), .left = pixels(16)})
                .with_font("ui-bold", pixels(12))
                .with_custom_text_color(theme::SECTION_HEADER_TEXT)
                .with_debug_name("sidebar_no_repo_title"));
            div(ctx, mk(sidebarRoot.ent(), 2201), ComponentConfig{}
                .with_label("Choose a recent repository, or press Cmd+O to open another folder.")
                .with_size(ComponentSize{pixels(std::max(0.f, sidebarW - 32.f)), pixels(48)})
                .with_margin(Margin{.left = pixels(16), .bottom = pixels(8)})
                .with_font_size(pixels(12))
                .with_custom_text_color(theme::TEXT_SECONDARY)
                .with_text_overflow(afterhours::ui::TextOverflow::Wrap)
                .with_debug_name("sidebar_no_repo_hint"));
            return;
        }
        if (layout.sidebarMode == LayoutComponent::SidebarMode::Agent) {
            // The ACP agent panel takes the whole sidebar under the header
            // (same early-return shape as repo search): files, commit log
            // and the commit box all belong to the other modes.
            render_sidebar_mode_tabs(ctx, sidebarRoot.ent(), layout);
            if (auto* agent = find_singleton<AgentComponent, ActiveTab>())
                agent_panel::render(ctx, sidebarRoot.ent(), *repoPtr, layout, *agent,
                                    sidebarW, std::max(0.f, layout.sidebar.height - 94.f));
            return;
        }
        if (repoPtr->repo_search_open()) {
            auto pane = div(ctx, mk(sidebarRoot.ent(), 2090), ComponentConfig{}
                .with_size(ComponentSize{percent(1.f), expand()})
                .with_padding(Padding{.top = pixels(8), .right = pixels(8), .bottom = pixels(8), .left = pixels(8)})
                .with_overflow(Overflow::Hidden).with_debug_name("repo_search_pane"));
            render_repo_search(ctx, pane.ent(), *repoPtr, layout,
                std::max(0.f, layout.sidebar.width - 16.f), std::max(0.f, layout.sidebar.height - 78.f));
            return;
        }
        // One sidebar view (the Review/Files toggle is gone): the Files
        // frame below hosts git controls, the Unstaged/Staged track, and
        // the review tree. 62 = repo header alone; the removed navigation
        // row used to add 42 (Review branch added the views track for 140).
        const float repoHeaderH = 62.f;
        {
            auto* editor = find_singleton<CommitEditorComponent, ActiveTab>();
            // While a commit/comparison document is open the upper pane is
            // the review tree; the working-tree controls (sync, commit box,
            // progress strip) are hidden so the tree gets the space — they
            // return when a working document is active again.
            const bool reviewingDoc = repoPtr &&
                layout.sidebarMode == LayoutComponent::SidebarMode::Changes &&
                layout.fileViewMode != LayoutComponent::FileViewMode::All &&
                (!repoPtr->selectedCommitHash().empty() || !repoPtr->comparisonScope().empty());
            const bool showGit = repoPtr && !repoPtr->repoPath.empty();
            const bool showCommit = showGit && !treeClean && editor && !reviewingDoc &&
                layout.sidebarMode == LayoutComponent::SidebarMode::Changes;
            const bool showProgress = repoPtr && !reviewingDoc && layout.sidebarMode == LayoutComponent::SidebarMode::Changes &&
                layout.fileViewMode != LayoutComponent::FileViewMode::All;
            const float viewportH = std::max(0.f, layout.sidebarFiles.height - repoHeaderH);
            // The fixed chrome above the file list, at exactly the heights
            // the sections below subtract from bodyH. The controls body is
            // the viewport height whenever that chrome plus a minimal list
            // fits: any extra height hands the outer scroll panel a range,
            // and it then clips the bottom of the file list's viewport and
            // competes with the list for the wheel and the scrollbar.
            const bool renderCommitArea = showCommit &&
                layout.fileViewMode != LayoutComponent::FileViewMode::All;
            const float fixedControlsH =
                (showGit && !reviewingDoc && layout.fileViewMode != LayoutComponent::FileViewMode::All
                     ? 38.f : 0.f) +
                (renderCommitArea ? (39.0f + COMMIT_INPUT_H) : 0.f) +
                28.f +
                (showProgress ? 60.f : 0.f);
            const float bodyH = std::max(viewportH, fixedControlsH + 20.f);
            // Ask layout to keep this pane tall enough for the header,
            // the fixed chrome, and a five-row file list; the history
            // pane below yields when the window is short (see
            // LayoutUpdateSystem). Applies from the next layout pass.
            layout.sidebarFilesFloor =
                repoPtr && layout.sidebarMode == LayoutComponent::SidebarMode::Changes &&
                        layout.fileViewMode != LayoutComponent::FileViewMode::All
                    ? repoHeaderH + fixedControlsH + 5.f * 28.f
                    : 0.f;
            auto controlsScroll = div(ctx, mk(sidebarRoot.ent(), 2090), preset::ScrollPanel()
                .with_size(ComponentSize{pixels(sidebarW), pixels(viewportH)})
                .with_debug_name("files_controls_scroll"));
            auto controlsBody = div(ctx, mk(controlsScroll.ent(), 0), ComponentConfig{}
                .with_size(ComponentSize{pixels(sidebarW), pixels(bodyH)})
                .with_min_height(pixels(bodyH))
                .with_flex_direction(FlexDirection::Column).with_no_wrap()
                .with_debug_name("files_controls_body"));
            float controlsH = 0.f;

            // === Sync row (Push / Pull / Stash), grouped under the repo header ===
            float syncRowH = 0.0f;
            if (repoPtr && !repoPtr->repoPath.empty() && !reviewingDoc &&
                layout.fileViewMode != LayoutComponent::FileViewMode::All) {
                render_sync_row(ctx, controlsBody.ent(), repoPtr);
                syncRowH = 38.f;
            }

            // === Commit area: rendered at the BOTTOM of the Changes pane
            // (below the file list), only in Changes mode. Height is still
            // reserved here so the file list math is unchanged. ===
            const float COMMIT_AREA_H = 39.0f + COMMIT_INPUT_H;
            float commitAreaH = 0.0f;
            if (layout.sidebarMode == LayoutComponent::SidebarMode::Changes && repoPtr &&
                !treeClean && !reviewingDoc &&
                layout.fileViewMode != LayoutComponent::FileViewMode::All && editor)
                commitAreaH = COMMIT_AREA_H;

            render_sidebar_mode_tabs(ctx, controlsBody.ent(), layout);
            float tabH = 28.f;

            // === Review-progress strip ("In the ballroom") ===
            float progressH = 0.0f;
            if (showProgress) {
                auto* rv = find_singleton<ReviewComponent, ActiveTab>();
                render_review_progress(ctx, controlsBody.ent(), *repoPtr, rv);
                progressH = 60.f;
            }

            // === Unstaged/Staged track (moved from the removed Review
            // view): switches the active document between working-tree and
            // index scopes. Shown in Changes mode; clicking it also returns
            // from a commit document to the working changes. ===
            // The review tree replaces the Files tree only while a commit or
            // comparison document is open (selectedCommitHash derives from
            // the active document, so this switches itself back when a
            // working document becomes active). Working state keeps the
            // Files tree — its sections and staging menus are the primary
            // working surface.
            const bool useReviewTree = reviewingDoc;
            // The Staged/Unstaged segment track that used to live here was
            // permanently disabled when grouped file sections replaced
            // it; the section headers carry its debug names and action.
            float viewsH = 0.f;

            float conflictH = 0.f;
            if (repoPtr && !reviewingDoc && layout.sidebarMode == LayoutComponent::SidebarMode::Changes &&
                layout.fileViewMode != LayoutComponent::FileViewMode::All) {
                render_conflict_banner(ctx, controlsBody.ent(), *repoPtr);
                conflictH = conflict_banner_height(*repoPtr);
            }

            // === Changed Files / Refs section (flow child of sidebar, NOT absolute) ===
            filesH = bodyH - tabH - commitAreaH - progressH -
                           controlsH - syncRowH - viewsH - conflictH;
            if (filesH < 20.0f) filesH = 20.0f;
            if (useReviewTree) {
                // The review tree in the Files view: the open commit's or
                // comparison's files, with reviewed/comment indicators.
                render_commit_files(ctx, controlsBody.ent(), repoPtr, filesH);
                commitsH = layout.sidebarLog.height;
            } else {
            // The file list is windowed: only the rows inside the viewport (plus a
            // few of overscan) are built each frame, so a 5000-file status costs
            // the same as a 30-file one. Empty tabs and the spinner keep the plain
            // panel so render_file_list can draw its empty states.
            auto filesPanel = preset::ScrollPanel()
                .with_size(ComponentSize{pixels(sidebarW), pixels(filesH)})
                .with_debug_name("sidebar_files");
            const auto fileList = mk(controlsBody.ent(), 2100);
            if (repoPtr && layout.sidebarMode == LayoutComponent::SidebarMode::Changes) {
                auto& state = repoPtr->filesTreeNavigation;
                auto& collapsed = layout.collapsedDirectories[repoPtr->repoPath];
                const std::string scope = active_review_tab() == LayoutComponent::ReviewTab::Staged ? "index" : "wt";
                const auto context = repoPtr->repoPath + "\nfiles:" + scope + ":" + std::to_string(static_cast<int>(layout.fileViewMode)) +
                    ":" + std::to_string(static_cast<int>(active_review_tab()));
                update_file_tree(ctx, fileList, *repoPtr, layout, context);
                if (!repoPtr->refresh_busy() && file_tree::reveal_navigation(state, context,
                        repoPtr->workspace().generation(), file_tree::destination_path(repoPtr->workspace(),
                            allFilesMode_ || scope == "wt" ? file_tree::Scope::Working : file_tree::Scope::Index),
                        allFilesMode_ ? repoPtr->allFilePaths : treePaths_, collapsed)) update_file_tree(ctx, fileList, *repoPtr, layout, context);
                if (auto move = ui::tree_keys(ctx, *repoPtr, layout, state, context, treeRows_, collapsed)) {
                    if (move->toggle) {
                        file_tree::toggle_directory(treeRows_, *move->toggle, collapsed);
                        update_file_tree(ctx, fileList, *repoPtr, layout, context);
                    }
                    if (move->open) {
                        // Staged rows open the index scope, others the
                        // working tree - the row's section decides now.
                        std::string rowScope = scope;
                        if (!allFilesMode_)
                            for (const auto& node : treeRows_)
                                if (node.path == move->path && !node.directory && !node.section) {
                                    rowScope = node.sourceIndex < treeKinds_.size() && treeKinds_[node.sourceIndex] == 'S' ? "index" : "wt";
                                    break;
                                }
                        reading::Location destination = allFilesMode_ ? reading::Location{reading::source(move->path)} :
                            reading::Location{reading::review(rowScope, move->path)};
                        if (move->keep) navigation::click(*repoPtr, std::move(destination), true);
                        else navigation::preview(*repoPtr, std::move(destination));
                    }
                }
                ui::reveal_tree_row(ctx, fileList, state, treeRows_);
            }
            const bool windowedFiles =
                layout.sidebarMode == LayoutComponent::SidebarMode::Changes &&
                repoPtr && active_file_count(*repoPtr) > 0;
            const float fileRowPx = 28.f;
            auto filesBg = windowedFiles
                ? afterhours::ui::imm::virtual_list(
                      ctx, fileList, active_file_count(*repoPtr),
                      fileRowPx,
                      [&](size_t i, Entity& row) {
                          render_active_file_row(ctx, row, i, *repoPtr);
                      },
                      filesPanel)
                : div(ctx, mk(controlsBody.ent(), 2100), filesPanel);

            if (layout.sidebarMode == LayoutComponent::SidebarMode::Changes) {
                // Render file list directly into filesBg (no intermediate container)
                // to avoid framework bug where nested container children render wrong
                if (repoPtr) {
                    if (!windowedFiles) render_file_list(ctx, filesBg.ent(), *repoPtr);
                } else {
                    render_no_repo(ctx, filesBg.ent(), 2150, "no_repo");
                }
            } else {
                // === Refs view (T031) ===
                if (repoPtr) {
                    render_refs_view(ctx, filesBg.ent(), *repoPtr, layout);
                } else {
                    render_no_repo(ctx, filesBg.ent(), 2150, "no_repo_refs");
                }
            }

            // Commit box at the bottom of the Changes pane.
            if (commitAreaH > 0.f && editor) render_commit_area(ctx, controlsBody.ent(), *repoPtr, *editor);

            commitsH = layout.sidebarLog.height;
            }
        }
        if (historyCollapsed) commitsH = 32.f;
        auto divider = afterhours::ui::imm::divider(ctx, mk(sidebarRoot.ent(), 2200), Axis::Y,
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.f), pixels(LayoutComponent::kCommitSplitterHeight)})
                .with_custom_background(theme::SIDEBAR_BG)
                .with_justify_content(JustifyContent::Center)
                .with_flex_direction(FlexDirection::Column)
                .with_roundness(0.f)
                .with_debug_name("sidebar_h_divider"));
        div(ctx, mk(divider.ent(), 0), ComponentConfig{}
            .with_size(ComponentSize{percent(1.f), pixels(1)})
            .with_custom_background(theme::BORDER)
            .with_debug_name("sidebar_h_divider_line"));
        if (divider && splitAvailable > 0.f) {
            layout.commitLogRatio = std::clamp(
                layout.commitLogRatio - divider.as<float>() / (zoom * splitAvailable), 0.2f,
                std::clamp(1.f - repoHeaderH / splitAvailable, 0.2f, 0.8f));
        }
        auto logBg = div(ctx, mk(sidebarRoot.ent(), 2300),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(sidebarW), pixels(commitsH)})
                .with_custom_background(theme::SIDEBAR_BG)
                .with_flex_direction(FlexDirection::Column)
                .with_overflow(Overflow::Hidden, Axis::Y)
                .with_roundness(0.0f)
                .with_debug_name("sidebar_log"));

        // Commit log header (matches section header style)
        auto logW = sidebarPixelWidth_ > 0 ? pixels(sidebarPixelWidth_) : percent(1.0f);
        {
            std::string branch;
            if (repoPtr) {
                branch = repoPtr->currentBranch;
            }
            auto heading = div(ctx, mk(logBg.ent(), 2310),
                ComponentConfig{}
                    .with_size(ComponentSize{logW, pixels(32)})
                    .with_flex_direction(FlexDirection::Row).with_no_wrap()
                    .with_padding(Padding{.left = pixels(14), .right = pixels(12)})
                    .with_debug_name("log_header"));
            auto historyToggle = button(ctx, mk(heading.ent(), 0), preset::Button("")
                .with_size(ComponentSize{expand(), pixels(32)}).with_transparent_bg()
                .with_flex_direction(FlexDirection::Row).with_align_items(AlignItems::Center)
                .with_justify_content(JustifyContent::FlexStart).with_gap(pixels(2)).with_no_wrap()
                .with_debug_name("history_section_toggle"));
            ui::chrome_icon(ctx, mk(historyToggle.ent(), 0),
                historyCollapsed ? ui::ChromeIcon::ChevronRight : ui::ChromeIcon::ChevronDown,
                theme::TEXT_TERTIARY, "history_section_chevron");
            div(ctx, mk(historyToggle.ent(), 1), ComponentConfig{}
                .with_label("COMMIT HISTORY")
                .with_size(ComponentSize{children(), children()}).with_font("ui-bold", pixels(12))
                .with_custom_text_color(theme::TEXT_TERTIARY)
                .with_debug_name("history_section_label"));
            if (historyToggle && repoPtr)
                Settings::get().set_section_collapsed(repoPtr->repoPath, "history", !historyCollapsed);
            if (repoPtr && repoPtr->historyScope.mode == git::HistoryScope::Mode::All) branch = "All refs";
            else if (repoPtr && repoPtr->historyScope.mode == git::HistoryScope::Mode::Branch) branch = repoPtr->historyScope.branch;
            const bool hasScope = repoPtr && !branch.empty();
            // Fit-to-content pill with a drawn chevron: the old version was
            // a fixed 92px right-aligned box, so "main" sat alone at the
            // right edge of an empty rectangle and nothing said it opens a
            // menu. (A "▾" suffix would be invisible — the UI font has no
            // such glyph; it renders as a tofu box, cf. the section toggle.)
            if (hasScope) {
            auto branchBtn = button(ctx, mk(heading.ent(), 1), preset::Button("")
                .with_size(ComponentSize{children(), pixels(24)})
                .with_flex_direction(FlexDirection::Row).with_align_items(AlignItems::Center)
                .with_gap(pixels(4)).with_no_wrap()
                .with_debug_name("history_branch"));
            div(ctx, mk(branchBtn.ent(), 0), ComponentConfig{}
                .with_label(branch)
                .with_size(ComponentSize{children(), children()}).with_font_size(pixels(12))
                .with_custom_text_color(theme::TEXT_SECONDARY)
                .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
                .with_debug_name("history_branch_label"));
            ui::chrome_icon(ctx, mk(branchBtn.ent(), 1), ui::ChromeIcon::ChevronDown,
                            theme::TEXT_SECONDARY, "history_branch_chevron");
            if (branchBtn && repoPtr) {
                std::vector<ui::ContextMenuItem> items;
                auto add = [&](const std::string& label, git::HistoryScope scope) {
                    items.push_back(ui::ContextMenuItem::item(label, [scope] {
                        if (auto* active = find_singleton<RepoComponent, ActiveTab>()) change_history_scope(*active, scope);
                    }));
                };
                add("Current branch", {});
                add("All refs", {git::HistoryScope::Mode::All, "", repoPtr->historyScope.remotes});
                auto remoteScope = repoPtr->historyScope;
                remoteScope.remotes = !remoteScope.remotes;
                add(remoteScope.remotes ? "Include remote refs" : "Hide remote refs", remoteScope);
                items.push_back(ui::ContextMenuItem::separator());
                for (const auto& branchInfo : repoPtr->branches) if (branchInfo.isLocal)
                    add(branchInfo.name, {git::HistoryScope::Mode::Branch, "refs/heads/" + branchInfo.name, repoPtr->historyScope.remotes});
                ui::show_context_menu(ctx.mouse.pos.x, ctx.mouse.pos.y, std::move(items));
            }
            }
        }

        // === Scrollable commit log entries ===
        float logHeaderConsumed = 32.f;
        float logScrollH = commitsH - logHeaderConsumed;
        if (logScrollH < 0.f) logScrollH = 0.f;

        // Windowed like the file list: a 100-commit log is 600 UI nodes when
        // every row is built, and only a dozen rows fit the panel. The last
        // "row" is the load-more indicator when there is more to load.
        auto logPanel = preset::ScrollPanel()
            .with_size(ComponentSize{logW, pixels(logScrollH)})
            .with_debug_name("commit_log_scroll");
        const size_t logRows = repoPtr && !historyCollapsed ? repoPtr->commitLog.size() : 0;
        const bool windowedLog = repoPtr && logRows > 0;
        const bool logHasMore = windowedLog && repoPtr->commitLogPage.hasMore;
        constexpr float commitRowPx = theme::layout::COMMIT_ROW_HEIGHT;
        constexpr float lazyRowPx = 24.f;
        if (repoPtr) {
            auto key = repoPtr->repoPath + ":" + repoPtr->historyScope.key() + ":" + std::to_string(repoPtr->dataGeneration) + ":" + std::to_string(repoPtr->commitLog.size());
            if (!repoPtr->commitLog.empty()) key += repoPtr->commitLog.front().hash;
            if (key != graphKey_) { graphKey_ = key; graph_ = commit_graph::build(repoPtr->commitLog); }
        }
        const auto logList = mk(logBg.ent(), 2320);
        auto focusedHistory = afterhours::ui::UICollectionHolder::getEntityForID(ctx.focus_id);
        const auto historyTarget = focusedHistory.valid() ? ui::focus_target(**focusedHistory) : std::nullopt;
        const bool retryFinished = repoPtr && historyTarget && historyTarget->repository == repoPtr->repoPath &&
            historyTarget->control == "lazy_load" && !repoPtr->commitLogPage.future.valid() &&
            repoPtr->commitLogPage.phase == LogPhase::Idle && repoPtr->commitLogPage.error.empty();
        if (retryFinished) {
            auto [listEntity, listOwner] = afterhours::ui::imm::deref(logList);
            ctx.set_focus(listEntity.id);
        }
        const auto historyMove = repoPtr && !historyCollapsed ? history_keys(ctx, *repoPtr, layout, logList) : std::nullopt;
        auto logScroll = windowedLog
            ? afterhours::ui::imm::virtual_list(
                  ctx, logList, logRows + (logHasMore ? 1 : 0),
                  [&](size_t i) { return i < logRows ? commitRowPx : lazyRowPx; },
                  [&](size_t i, Entity& row) {
                      if (i < logRows) {
                          render_commit_row(ctx, row, 0, repoPtr->commitLog[i],
                                            *repoPtr, historyMove == i || (!historyMove && retryFinished &&
                                                repoPtr->commitLog[i].hash == repoPtr->selectedCommitHash()), !historyMove);
                      } else {
                          render_lazy_load_row(ctx, row, *repoPtr);
                      }
                  },
                  logPanel)
            : div(ctx, mk(logBg.ent(), 2320), logPanel);

        if (repoPtr) {
            ui::bind_focus_region(logScroll.ent(), *repoPtr, reading::focus::Region::History);
            if (logScroll.ent().has<afterhours::ui::HasScrollView>() && !repoPtr->commitLog.empty()) {
                auto& scroll = logScroll.ent().get<afterhours::ui::HasScrollView>();
                auto& position = repoPtr->historyPositions[repoPtr->historyScope.key()];
                const auto rowHeight = theme::layout::COMMIT_ROW_HEIGHT * ui::zoom::get();
                if (repoPtr->historyRestorePosition) {
                    auto found = std::find_if(repoPtr->commitLog.begin(), repoPtr->commitLog.end(), [&](const auto& commit) { return commit.hash == position.top; });
                    const float offset = found == repoPtr->commitLog.end() ? 0.f : (static_cast<float>(found - repoPtr->commitLog.begin()) + position.fraction) * rowHeight;
                    if (found == repoPtr->commitLog.end() && !position.top.empty() && repoPtr->commitLogPage.hasMore && repoPtr->commitLog.size() < 5000) {
                        repoPtr->commitLogPage.request_page();
                    } else {
                        scroll.scroll_offset.y = scroll.scroll_target.y = scroll.last_eased_offset.y = offset;
                        scroll.anchor_child = -1;
                        repoPtr->historyRestorePosition = false;
                    }
                } else {
                    const auto index = std::min(repoPtr->commitLog.size() - 1, static_cast<size_t>(std::max(0.f, scroll.scroll_offset.y) / rowHeight));
                    position.top = repoPtr->commitLog[index].hash;
                    position.fraction = scroll.scroll_offset.y / rowHeight - static_cast<float>(index);
                }
            }
            if (!windowedLog && !historyCollapsed) render_commit_log_entries(ctx, logScroll.ent(), *repoPtr);
            if (historyMove) {
                navigation::preview(*repoPtr, reading::review(repoPtr->commitLog[*historyMove].hash));
                if (afterhours::input::is_key_pressed(afterhours::keys::ENTER)) navigation::keep(*repoPtr, repoPtr->workspace().active_id());
            }
        } else {
            render_no_repo(ctx, logScroll.ent(), 0, "no_repo_log");
        }

        if (repoPtr) {
            if (historyMove) hoverPrefetch_ = false;
            const auto& hash = hoverPrefetch_ && !hoveredCommit_.empty() ? hoveredCommit_ : focusedCommit_;
            std::optional<commit_prefetch::Candidate> candidate;
            if (reading::is_object_id(hash) && hash != repoPtr->selectedCommitHash())
                candidate = commit_prefetch::Candidate{repoPtr->repoPath, hash, repoPtr->diffContext, repoPtr->ignoreWhitespace};
            auto& state = repoPtr->commitPrefetch;
            if (state.observe(candidate) && prefetchAvailable) {
                if (auto pending = git::prefetch_commit_patch_async({candidate->repository, candidate->commit, {}, candidate->context, candidate->ignoreWhitespace})) {
                    state.attempted = true;
                    ++state.submitted;
                    state.future = std::move(*pending);
                }
            }
        }


        // === Commit workflow + Unstaged Changes Dialog (T030) ===
        if (repoPtr && repoPtr->reviewWorkspace) {
            if (auto* editor = find_singleton<CommitEditorComponent, ActiveTab>()) {
                editor->cancel_request();
            }
            if (auto* layout = find_singleton<LayoutComponent>()) {
                using reading::focus::Popup;
                if (layout->modal_is(Popup::NewBranch) || layout->modal_is(Popup::DeleteBranch) ||
                    layout->modal_is(Popup::ForceDeleteBranch)) layout->close_top_modal();
            }
        }
        if (repoPtr && !repoPtr->reviewWorkspace) {
            auto& repo = *repoPtr;

            auto* editor = find_singleton<CommitEditorComponent, ActiveTab>();
            if (editor) {

                // Process commit request
                if (editor->commit_requested()) {
                    commit_workflow::handle_commit_request(repo, *editor);
                }
                commit_workflow::poll_commit(repo, *editor);

                // Render unstaged changes dialog
                render_unstaged_dialog(ctx, uiRoot, repo, *editor);
            }

            // === Branch dialogs (T031) ===
            auto* branchDialog = find_singleton<BranchDialogState, ActiveTab>();
            auto* layoutPtr = find_singleton<LayoutComponent>();
            if (branchDialog && layoutPtr) {
                render_new_branch_dialog(ctx, uiRoot, repo, *branchDialog, *layoutPtr);
                render_delete_branch_dialog(ctx, uiRoot, repo, *branchDialog, *layoutPtr);
                render_force_delete_dialog(ctx, uiRoot, repo, *branchDialog, *layoutPtr);
                render_stash_dialog(ctx, uiRoot, repo, *branchDialog, *layoutPtr);
            }
        }
    }

private:
    std::string hoveredCommit_;
    std::string focusedCommit_;
    // Per-commit display text (badges, tooltip, age) is identical every
    // frame, but computing it allocates: decoration parsing, tooltip
    // concatenation, and a mktime-based relative age. Cache it per log
    // generation; the age bucket is one minute, matching its granularity.
    struct CommitRowText {
        std::vector<commit_log_detail::Decoration> badges;
        std::string tooltip;
        std::string age;
    };
    // Row text retained per (repo, generation, minute), up to 4 repos,
    // so tab switches don't rebuild it.
    struct CommitRowTextCache {
        std::string repo;
        unsigned generation = 0;
        long minute = -1;
        std::unordered_map<std::string, CommitRowText> rows;
    };
    std::vector<CommitRowTextCache> commitRowTextCaches_;

    const CommitRowText& commit_row_text(const CommitEntry& commit, const RepoComponent& repo) {
        const long minute = static_cast<long>(std::time(nullptr) / 60);
        auto found = std::find_if(commitRowTextCaches_.begin(), commitRowTextCaches_.end(), [&](const CommitRowTextCache& cache) {
            return cache.repo == repo.repoPath && cache.generation == repo.dataGeneration && cache.minute == minute;
        });
        if (found == commitRowTextCaches_.end()) {
            commitRowTextCaches_.insert(commitRowTextCaches_.begin(), {repo.repoPath, repo.dataGeneration, minute, {}});
            if (commitRowTextCaches_.size() > 4) commitRowTextCaches_.pop_back();
            found = commitRowTextCaches_.begin();
        }
        auto& commitRowText_ = found->rows;
        auto [it, inserted] = commitRowText_.try_emplace(commit.hash);
        if (inserted) {
            it->second.badges = commit_log_detail::parse_decorations(commit.decorations);
            it->second.tooltip = commit.subject + "\n" + commit.hash + "\n" + commit.decorations;
            it->second.age = relative_time(commit.authorDate);
        }
        return it->second;
    }
    std::string histDragAnchor_;
    bool histDragMoved_ = false;
    vec2 lastPrefetchMouse_{};
    bool hoverPrefetch_ = true;
    std::string commitFileQuery_;
    std::string commitTreeKey_;
    std::vector<file_tree::Row> commitTreeRows_;
    std::vector<std::string> commitTreePaths_;
    std::vector<size_t> commitFileIndices_;
    std::optional<review_files::Filter> commitFileFilter_;

    void open_review_file_menu(UIContext<InputAction>& ctx, const RepoComponent& repo, const FileDiff& file) {
        const auto* owner = find_singleton_entity<RepoComponent, ActiveTab>();
        if (!owner) return;
        auto target = [id = owner->id, path = repo.repoPath, document = repo.workspace().active_id(),
                       generation = repo.workspace().generation()]() -> RepoComponent* {
            auto* active = find_singleton_entity<RepoComponent, ActiveTab>();
            if (!active || active->id != id || active->cleanup) return nullptr;
            auto& current = active->get<RepoComponent>();
            return current.repoPath == path && current.workspace().active_id() == document &&
                current.workspace().generation() == generation ? &current : nullptr;
        };
        auto destination = repo.workspace().review();
        destination.file = file.filePath;
        const auto source = reading::source_at_diff(destination, file);
        std::vector<ui::ContextMenuItem> items{
            ui::ContextMenuItem::item("Open diff", [target, destination] {
                if (auto* current = target()) navigation::open(*current, destination, true);
            }),
            ui::ContextMenuItem::item("Open source", [target, source] {
                if (auto* current = target()) navigation::open(*current, source);
            }),
            ui::ContextMenuItem::item("Keep open", [target, destination] {
                if (auto* current = target()) navigation::open(*current, destination, true, reading::OpenMode::Keep);
            }),
            ui::ContextMenuItem::separator(),
            ui::ContextMenuItem::item("Copy relative path", [target, path = file.filePath] {
                if (target()) afterhours::clipboard::set_text(path);
            })
        };
        if (const auto* working = std::get_if<reading::WorkingChanges>(&destination.destination)) {
            const auto tab = working->staged ? LayoutComponent::ReviewTab::Staged :
                std::find(repo.untrackedFiles.begin(), repo.untrackedFiles.end(), file.filePath) != repo.untrackedFiles.end()
                    ? LayoutComponent::ReviewTab::Untracked : LayoutComponent::ReviewTab::ToReview;
            items.push_back(ui::ContextMenuItem::item("Reveal in tree", [target, destination, tab] {
                if (auto* current = target()) {
                    navigation::open(*current, destination, true);
                    auto& layout = *find_singleton<LayoutComponent>();
                    layout.sidebarNavigation = LayoutComponent::SidebarNavigation::Files;
                    layout.sidebarMode = LayoutComponent::SidebarMode::Changes;
                    layout.reviewTab = tab;
                    layout.fileViewMode = LayoutComponent::FileViewMode::Tree;
                    current->filesTreeNavigation.navigationGeneration.reset();
                }
            }));
        }
        ui::show_context_menu(ctx.mouse.pos.x, ctx.mouse.pos.y, std::move(items));
    }

    // The document a commit-tree row opens: the file itself, as a source
    // document at the open review's revision (the commit for a commit
    // review, INDEX or working tree for working changes, the after side
    // for a comparison), with the review kept as its origin. Clicking a
    // file used to navigate inside the review (filtering the open commit
    // view to that file); opening the file is what the click should do.
    reading::SourceLocation commit_file_source(RepoComponent& repo, const std::string& path) {
        const auto origin = repo.workspace().review();
        return std::visit([&](const auto& destination) -> reading::SourceLocation {
            using T = std::decay_t<decltype(destination)>;
            if constexpr (std::is_same_v<T, reading::WorkingChanges>)
                return destination.staged ? reading::source(path, "INDEX", 0, origin)
                                           : reading::source(path, {}, 0, origin);
            else if constexpr (std::is_same_v<T, reading::CommitReview>)
                return reading::source(path, reading::revision_text(destination.commit), 0, origin);
            else
                return reading::source(path, reading::revision_text(destination.after), 0, origin);
        }, origin.destination);
    }

    void render_commit_files(UIContext<InputAction>& ctx, Entity& parent,
                             RepoComponent* repo, float height) {
        auto* layout = find_singleton<LayoutComponent>();
        auto* review = find_singleton<ReviewComponent, ActiveTab>();
        auto* cache = find_singleton<CommitDetailCache, ActiveTab>();
        const std::vector<FileDiff>* files = nullptr;
        std::string scope = "wt";
        std::string empty = "Select a commit to review";
        if (repo && source_tab_active(*repo) &&
            !std::holds_alternative<reading::WorkingChanges>(repo->workspace().review().destination)) {
            scope = commit_review_scope(*repo);
            const auto* origin = repo->workspace().retained_review();
            if (origin && origin->files) files = &repo->originFileSummaries;
            else empty = "Review files have not loaded";
        } else if (repo && !repo->comparisonScope().empty()) {
            scope = repo->comparisonScope();
            if (repo->comparisonLoadedScope == scope && !repo->comparisonFuture.valid() && repo->comparisonError.empty())
                files = &repo->comparisonDiff;
            else empty = !repo->comparisonError.empty() ? "Unable to load comparison files" :
                repo->comparisonLoading.visible(repo->comparisonFuture.valid()) ? "Loading comparison files..." : "";
        } else if (repo && !repo->selectedCommitHash().empty()) {
            scope = commit_review_scope(*repo);
            const bool matching = cache && cache->patch.key == PatchKey{repo->repoPath, repo->selectedCommitHash(),
                selected_commit_parent(*repo), repo->diffContext, repo->ignoreWhitespace};
            if (matching && !cache->patch_loading()) {
                if (cache->commitDetailError.empty()) files = &cache->commitDetailDiff;
                else empty = "Unable to load commit files";
            } else empty = matching && cache->loading.visible(cache->patch_loading()) ? "Loading commit files..." : "";
        } else if (repo && repo->has_loaded_once()) {
            scope = repo->selectedFileStaged() ? "index" : "wt";
            files = repo->selectedFileStaged() ? &repo->stagedDiff : &repo->currentDiff;
        }
        if (files && files->empty()) empty = "No changed files";
        auto section = div(ctx, mk(parent, 2400), ComponentConfig{}
            .with_size(ComponentSize{percent(1.f), pixels(height)})
            .with_flex_direction(FlexDirection::Column).with_overflow(Overflow::Hidden)
            .with_debug_name("review_changed_files"));
        auto heading = div(ctx, mk(section.ent(), 0), ComponentConfig{}
            .with_size(ComponentSize{percent(1.f), pixels(32)})
            .with_flex_direction(FlexDirection::Row).with_no_wrap()
            .with_padding(Padding{.left = pixels(14), .right = pixels(12)}).with_debug_name("changed_files_header"));
        const bool sectionCollapsed = repo && Settings::get().section_collapsed(repo->repoPath, "changes");
        auto changesToggle = button(ctx, mk(heading.ent(), 0), preset::Button("")
            .with_size(ComponentSize{expand(), pixels(32)}).with_transparent_bg()
            .with_flex_direction(FlexDirection::Row).with_align_items(AlignItems::Center)
            .with_justify_content(JustifyContent::FlexStart).with_gap(pixels(2)).with_no_wrap()
            .with_debug_name("changes_section_toggle"));
        ui::chrome_icon(ctx, mk(changesToggle.ent(), 0),
            sectionCollapsed ? ui::ChromeIcon::ChevronRight : ui::ChromeIcon::ChevronDown,
            theme::TEXT_TERTIARY, "changes_section_chevron");
        div(ctx, mk(changesToggle.ent(), 1), ComponentConfig{}
            .with_label("CHANGED FILES" + (files ? "  " + std::to_string(files->size()) : ""))
            .with_size(ComponentSize{children(), children()}).with_font("ui-bold", pixels(12))
            .with_custom_text_color(theme::TEXT_TERTIARY)
            .with_debug_name("changes_section_label"));
        if (changesToggle && repo)
            Settings::get().set_section_collapsed(repo->repoPath, "changes", !sectionCollapsed);
        if (files && review) {
            const auto* origin = repo && source_tab_active(*repo) ? repo->workspace().retained_review() : nullptr;
            const auto progress = origin && origin->files ? ui::cached_review_progress(*review, scope, *origin->files) :
                ui::cached_review_progress(*review, scope, *files);
            if (layout && layout->shelf_collapsed() && progress.reviewed < progress.total) {
                const auto remaining = progress.total - progress.reviewed;
                if (button(ctx, mk(heading.ent(), 1), preset::Button(std::to_string(remaining) + " left")
                        .with_size(ComponentSize{pixels(72), pixels(28)}).with_font_size(pixels(12))
                        .with_debug_name("dock_review_remaining"))) {
                    auto destination = repo->workspace().review();
                    if (origin && origin->files) {
                        for (const auto& file : *origin->files) {
                            if (ui::reviewed_file(*review, scope, file)) continue;
                            destination.file = file.path;
                            break;
                        }
                    } else {
                        for (const auto& file : *files) {
                            if (ui::reviewed_file(*review, scope, file)) continue;
                            destination.file = file.filePath;
                            break;
                        }
                    }
                    navigation::open(*repo, destination, true);
                }
            } else {
                div(ctx, mk(heading.ent(), 1), preset::BodyText(std::to_string(progress.reviewed) + " / " + std::to_string(progress.total))
                    .with_size(ComponentSize{pixels(64), pixels(32)}).with_font("mono", pixels(12))
                    .with_alignment(TextAlignment::Right).with_custom_text_color(theme::TEXT_SECONDARY)
                    .with_debug_name("tree_review_progress"));
            }
        }
        if (sectionCollapsed || height <= 32.f || (layout && layout->shelf_collapsed() && files && files->empty())) return;
        auto search = div(ctx, mk(section.ent(), 1), ComponentConfig{}
            .with_size(ComponentSize{percent(1.f), pixels(38)})
            .with_padding(Padding{.left = pixels(16), .right = pixels(16), .bottom = pixels(8)})
            .with_flex_direction(FlexDirection::Row).with_no_wrap().with_gap(pixels(6)));
        auto searchIcon = div(ctx, mk(search.ent(), 1), ComponentConfig{}
            .with_size(ComponentSize{pixels(16), pixels(30)}).with_debug_name("tree_search_icon"));
        div(ctx, mk(searchIcon.ent(), 0), ComponentConfig{}
            .with_size(ComponentSize{pixels(9), pixels(9)}).with_absolute_position(1.f, 8.f)
            .with_border(theme::TEXT_TERTIARY, pixels(1))
            .with_rounded_corners(theme::layout::ROUNDED_CORNERS).with_corner_radius(4.5f));
        div(ctx, mk(searchIcon.ent(), 1), ComponentConfig{}
            .with_size(ComponentSize{pixels(4), pixels(1)}).with_absolute_position(10.f, 18.f)
            .with_custom_background(theme::TEXT_TERTIARY));
        afterhours::text_input::text_input(ctx, mk(search.ent(), 0), commitFileQuery_,
            ComponentConfig{}.with_size(ComponentSize{expand(), pixels(30)})
                .with_font_size(pixels(12))
                .with_custom_background(theme::SIDEBAR_BG).with_border_bottom(theme::BORDER)
                .with_roundness(0.f).with_placeholder("Filter files...").with_debug_name("commit_file_filter"));
        if (!files || !repo || !layout) {
            div(ctx, mk(section.ent(), 2), preset::BodyText(empty)
                .with_size(ComponentSize{percent(1.f), pixels(36)}).with_font_size(pixels(12))
                .with_padding(Padding{.left = pixels(14), .right = pixels(10)}));
            return;
        }
        const auto fileList = mk(section.ent(), 3);
        const auto collapseKey = repo->repoPath + "\n" + scope;
        auto& collapsed = layout->collapsedDirectories[collapseKey];
        std::string key = collapseKey + "\n" + commitFileQuery_;
        for (const auto& file : *files) key += ":" + std::to_string(file.renderIdentity);
        for (const auto& directory : collapsed) key += "\n" + directory;
        if (repo->fileFilter.onlyUnresolved && review)
            for (const auto& comment : review->comments)
                if (comment.scope == scope && !comment.resolved) key += "\n" + comment.file;
        if (key != commitTreeKey_ || !commitFileFilter_ || *commitFileFilter_ != repo->fileFilter) {
            commitTreeKey_ = std::move(key);
            commitFileFilter_ = repo->fileFilter;
            commitFileIndices_.clear();
            commitTreePaths_.clear();
            for (size_t index : visible_review_file_indices(*files, repo->fileFilter, review, scope)) {
                if (!commitFileQuery_.empty() && (*files)[index].filePath.find(commitFileQuery_) == std::string::npos) continue;
                commitTreePaths_.push_back((*files)[index].filePath);
                commitFileIndices_.push_back(index);
            }
            ui::replace_tree_rows(ctx, fileList, *repo, repo->reviewTreeNavigation, collapseKey,
                commitTreeRows_, file_tree::flatten(commitTreePaths_, collapsed));
        }
        auto& treeState = repo->reviewTreeNavigation;
        const auto activeTreePath = file_tree::destination_path(repo->workspace(), file_tree::Scope::Review);
        if (file_tree::reveal_navigation(treeState, collapseKey, repo->workspace().generation(), activeTreePath, commitTreePaths_, collapsed)) {
            ui::replace_tree_rows(ctx, fileList, *repo, repo->reviewTreeNavigation, collapseKey,
                commitTreeRows_, file_tree::flatten(commitTreePaths_, collapsed));
            commitTreeKey_.clear();
        }
        const auto move = ui::tree_keys(ctx, *repo, *layout, treeState, collapseKey, commitTreeRows_, collapsed);
        if (move) {
            if (move->toggle) {
                file_tree::toggle_directory(commitTreeRows_, *move->toggle, collapsed);
                ui::replace_tree_rows(ctx, fileList, *repo, repo->reviewTreeNavigation, collapseKey,
                    commitTreeRows_, file_tree::flatten(commitTreePaths_, collapsed));
                commitTreeKey_.clear();
            }
        }
        auto config = preset::ScrollPanel().with_size(ComponentSize{percent(1.f), pixels(std::max(0.f, height - 70.f))})
            .with_debug_name("commit_files_scroll");
        if (commitTreeRows_.empty()) {
            auto emptyPanel = div(ctx, mk(section.ent(), 3), config);
            div(ctx, mk(emptyPanel.ent(), 0), preset::BodyText(files->empty() ? empty : "No matching files")
                .with_size(ComponentSize{percent(1.f), pixels(28)})
                .with_font_size(pixels(12)).with_text_inset(12.f, 0.f)
                .with_debug_name("commit_files_empty"));
            return;
        }
        std::optional<std::string> selectedPath;
        ui::reveal_tree_row(ctx, fileList, treeState, commitTreeRows_);
        afterhours::ui::imm::virtual_list(ctx, fileList, commitTreeRows_.size(), 28.f,
            [&](size_t index, Entity& wrapper) {
                const auto& node = commitTreeRows_[index];
                if (node.directory) {
                    if (ui::file_tree_style::directory(ctx, wrapper, node, sidebarPixelWidth_,
                            file_tree::directory_collapsed(node, collapsed), "commit_directory:" + node.path, *repo, treeState)) {
                        file_tree::toggle_directory(node, collapsed);
                    }
                    return;
                }
                const auto& file = (*files)[commitFileIndices_[node.sourceIndex]];
                auto row = button(ctx, mk(wrapper, 0), ui::file_tree_style::row_config(sidebarPixelWidth_, node.depth,
                    activeTreePath == node.path)
                    .with_debug_name("commit_changed_file"));
                ui::bind_tree_row(ctx, row.ent(), *repo, treeState, node.path);
                div(ctx, mk(row.ent(), 3), ComponentConfig{}.with_label(ui::file_tree_style::type_marker(node.path))
                    .with_size(ComponentSize{pixels(24), pixels(28)}).with_font("mono", pixels(12))
                    .with_custom_text_color(theme::TEXT_ACCENT).with_debug_name("tree_file_type"));
                auto fileLabel = div(ctx, mk(row.ent(), 0), ComponentConfig{}.with_label(sidebar_detail::basename_from_path(node.path))
                        .with_size(ComponentSize{expand(), pixels(28)}).with_transparent_bg()
                        .with_custom_text_color(theme::TEXT_PRIMARY).with_alignment(TextAlignment::Left)
                        .with_padding(Padding{.left = pixels(0)}).with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
                        .with_font_size(pixels(14)).with_debug_name("jump_to_diff:" + node.path));
                ui::set_truncated_tooltip(row.ent(), node.path, fileLabel.ent());
                if (row) selectedPath = file.filePath;
                if (ctx.is_right_click(row.ent().id)) {
                    ui::remember_focus_origin(ctx, row.ent());
                    open_review_file_menu(ctx, *repo, file);
                }
                if (review) {
                    bool reviewed = ui::reviewed_file(*review, scope, file);
                    if (source_tab_active(*repo)) {
                        const auto* origin = repo->workspace().retained_review();
                        if (origin && origin->files)
                            for (const auto& summary : *origin->files)
                                if (summary.path == file.filePath) { reviewed = ui::reviewed_file(*review, scope, summary); break; }
                    }
                    bool changed = false;
                    if (scope == "wt") {
                        const auto seen = review->seenSig.find(file.filePath);
                        changed = seen != review->seenSig.end() && ui::diff_metrics().signature(file) != seen->second;
                    }
                    ui::file_tree_style::review_indicator(ctx, row.ent(), reviewed,
                        unresolved_file_count(*review, scope, file.filePath, file.oldPath), changed);
                }
                // Natural-width counts: each used to own a fixed 32px
                // right-aligned column, so "+3" and "-1" floated in 68px of
                // mostly-empty space. Sized to their text they sit one row
                // gap (4px) apart and the filename gets the room back.
                if (file.additions > 0) div(ctx, mk(row.ent(), 1), ComponentConfig{}
                    .with_label("+" + std::to_string(file.additions))
                    .with_size(ComponentSize{children(), pixels(28)}).with_font("mono", pixels(12))
                    .with_custom_text_color(theme::DIFF_ADD_TEXT).with_alignment(TextAlignment::Right)
                    .with_debug_name("tree_additions"));
                if (file.deletions > 0) div(ctx, mk(row.ent(), 2), ComponentConfig{}
                    .with_label("-" + std::to_string(file.deletions))
                    .with_size(ComponentSize{children(), pixels(28)}).with_font("mono", pixels(12))
                    .with_custom_text_color(theme::DIFF_DEL_TEXT).with_alignment(TextAlignment::Right)
                    .with_debug_name("tree_deletions"));
            }, config);
        if (move && move->open) {
            if (move->keep) navigation::click(*repo, commit_file_source(*repo, move->path), true);
            else navigation::preview(*repo, commit_file_source(*repo, move->path));
            treeState.pendingFocus = true;
        } else if (selectedPath) navigation::click(*repo, commit_file_source(*repo, *selectedPath), afterhours::input::is_key_pressed(afterhours::keys::ENTER));
    }

    // ---- Sidebar mode toggle (T031) ----
    // Render the Changes/Refs toggle tabs at the top of the sidebar
    void render_sidebar_mode_tabs(UIContext<InputAction>& ctx,
                                   Entity& parent,
                                   LayoutComponent& layout) {
        constexpr float TAB_HEIGHT = 28.0f;

        auto tabRowW = sidebarPixelWidth_ > 0 ? pixels(sidebarPixelWidth_) : percent(1.0f);
        auto tabRow = div(ctx, mk(parent, 2090),
            ComponentConfig{}
                .with_size(ComponentSize{tabRowW, pixels(TAB_HEIGHT)})
                .with_flex_direction(FlexDirection::Row)
                .with_align_items(AlignItems::Center)
                .with_gap(pixels(2))
                .with_padding(Padding{
                    .top = pixels(2), .right = pixels(10),
                    .bottom = pixels(2), .left = pixels(10)})
                .with_custom_background(theme::SIDEBAR_BG)
                .with_roundness(0.0f)
                .with_debug_name("sidebar_mode_tabs"));
        auto track = div(ctx, mk(tabRow.ent(), 0), ComponentConfig{}
            .with_size(ComponentSize{percent(1.f), pixels(28)})
            .with_flex_direction(FlexDirection::Row)
            .with_align_items(AlignItems::Center)
            .with_gap(pixels(2))
            .with_padding(Padding{.top = pixels(2), .right = pixels(2), .bottom = pixels(2), .left = pixels(2)})
            .with_custom_background(theme::BUTTON_SECONDARY)
            .with_border(theme::BORDER, pixels(1))
            .with_rounded_corners(theme::layout::ROUNDED_CORNERS)
            .with_corner_radius(theme::layout::RADIUS_MODAL)
            .with_debug_name("sidebar_mode_track"));

        auto* countRepo = find_singleton<RepoComponent, ActiveTab>();
        int nReview = countRepo ? static_cast<int>(countRepo->unstagedFiles.size()) : 0;
        int nStaged = countRepo ? static_cast<int>(countRepo->stagedFiles.size()) : 0;
        int nUntracked = countRepo ? static_cast<int>(countRepo->untrackedFiles.size()) : 0;
        int nRefs = countRepo ? static_cast<int>(countRepo->branches.size() + countRepo->tags.size() + countRepo->stashes.size()) : 0;
        const int nChanges = nReview + nStaged + nUntracked;

        // Emit one tab. `active` highlights it; on click, `apply` mutates layout.
        auto makeTab = [&](int id, const std::string& label, bool active,
                           void (*apply)(LayoutComponent&)) {
            // Recessed segmented control matching Unified/Split: transparent
            // inactive segments inside a bordered track, filled active pill.
            auto config = preset::Button(label)
                .with_size(ComponentSize{afterhours::ui::expand(),
                                         pixels(24)})
                .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
                .with_padding(Padding{
                    .top = pixels(4), .right = pixels(6),
                    .bottom = pixels(4), .left = pixels(6)})
                .with_font_size(pixels(12))
                .with_transparent_bg()
                .with_rounded_corners(theme::layout::ROUNDED_CORNERS)
                .with_corner_radius(theme::layout::RADIUS_CONTROL)
                .with_debug_name("tab_" + label);
            if (active) {
                config = config.with_custom_background(theme::SEGMENT_SELECTED_BG)
                               .with_custom_text_color(theme::TEXT_PRIMARY)
                               .with_border(theme::BORDER, pixels(1));
            } else {
                config = config.with_custom_text_color(theme::TEXT_SECONDARY);
            }
            if (button(ctx, mk(track.ent(), id), config)) {
                auto* lc = find_singleton<LayoutComponent>();
                if (lc) apply(*lc);
            }
        };

        using SM = LayoutComponent::SidebarMode;
        using RT = LayoutComponent::ReviewTab;
        bool inChanges = (layout.sidebarMode == SM::Changes);
        makeTab(2091, nChanges > 0 ? "Changes " + std::to_string(nChanges) : "Changes",
                inChanges,
                [](LayoutComponent& l) { l.sidebarMode = SM::Changes; l.reviewTab = RT::ToReview; });
        makeTab(2094, nRefs > 0 ? "Refs " + std::to_string(nRefs) : "Refs", layout.sidebarMode == SM::Refs,
                [](LayoutComponent& l) { l.sidebarMode = SM::Refs; });
        makeTab(2095, "Agent", layout.sidebarMode == SM::Agent,
                [](LayoutComponent& l) { l.sidebarMode = SM::Agent; });
    }

    // Repo-panel header (mock): "<repo name> · <branch>[*]". A single panel for
    // now; nesting per-submodule panels needs submodule support.
    void render_repo_header(UIContext<InputAction>& ctx, Entity& parent,
                            RepoComponent* repo) {
        std::string name = "No repository", branch;
        bool dirty = false;
        if (repo && !repo->repoPath.empty()) {
            name = repo->repoPath;
            auto slash = name.find_last_of('/');
            if (slash != std::string::npos) name = name.substr(slash + 1);
            if (name.empty() || name == ".") name = "repo";
            branch = repo->currentBranch;
            dirty = repo->isDirty;
        }
        auto w = sidebarPixelWidth_ > 0 ? pixels(sidebarPixelWidth_) : percent(1.0f);
        auto row = div(ctx, mk(parent, 2079),
            ComponentConfig{}
                .with_size(ComponentSize{w, pixels(66)})
                .with_flex_direction(FlexDirection::Row)
                .with_align_items(AlignItems::Center)
                .with_gap(pixels(8))
                .with_padding(Padding{
                    .top = pixels(10), .right = pixels(14),
                    .bottom = pixels(10), .left = pixels(14)})
                .with_custom_background(theme::SIDEBAR_BG)
                .with_roundness(0.0f)
                .with_debug_name("repo_header"));
        auto icon = div(ctx, mk(row.ent(), 1), ComponentConfig{}
            .with_size(ComponentSize{pixels(32), pixels(32)})
            .with_custom_background(theme::BUTTON_SECONDARY)
            .with_border(theme::BORDER, pixels(1))
            .with_rounded_corners(theme::layout::ROUNDED_CORNERS).with_corner_radius(theme::layout::RADIUS_CARD)
            .with_debug_name("repo_folder_icon"));
        div(ctx, mk(icon.ent(), 0), ComponentConfig{}
            .with_size(ComponentSize{pixels(17), pixels(12)})
            .with_absolute_position(7.f, 11.f).with_transparent_bg()
            .with_border(theme::TEXT_SECONDARY, pixels(1.f)));
        div(ctx, mk(icon.ent(), 1), ComponentConfig{}
            .with_size(ComponentSize{pixels(7), pixels(4)})
            .with_absolute_position(7.f, 8.f).with_custom_background(theme::BUTTON_SECONDARY)
            .with_border_top(theme::TEXT_SECONDARY, pixels(1.f))
            .with_border_left(theme::TEXT_SECONDARY, pixels(1.f))
            .with_border_right(theme::TEXT_SECONDARY, pixels(1.f)));
        auto text = div(ctx, mk(row.ent(), 2), ComponentConfig{}
            .with_size(ComponentSize{expand(), pixels(46)})
            .with_flex_direction(FlexDirection::Column));
        div(ctx, mk(text.ent(), 0),
            ComponentConfig{}
                .with_label(name)
                .with_size(ComponentSize{percent(1.f), pixels(22)})
                .with_custom_text_color(theme::TEXT_PRIMARY)
                .with_font("ui-bold", pixels(16))
                .with_alignment(TextAlignment::Left)
                .with_transparent_bg()
                .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
                .with_roundness(0.0f)
                .with_debug_name("repo_header_label"));
        // Branch badge: a compact inline pill (not a full-width empty input
        // box). Click lists local branches for checkout.
        auto branchBadge = div(ctx, mk(text.ent(), 1), ComponentConfig{}
            .with_size(ComponentSize{percent(1.f), pixels(24)})
            .with_flex_direction(FlexDirection::Row)
            .with_align_items(AlignItems::Center)
            .with_debug_name("repo_branch_row"));
        auto branchBtn = button(ctx, mk(branchBadge.ent(), 0), preset::Button(branch.empty() ? (repo && !repo->repoPath.empty() ? "Reading branch..." : "Open a repository") : branch + (dirty ? " *" : ""))
            .with_size(ComponentSize{children(), pixels(22)})
            .with_padding(Padding{.top = pixels(2), .right = pixels(10), .bottom = pixels(2), .left = pixels(10)})
            .with_text_inset(10.f, 0.f)
            .with_custom_background(theme::BADGE_BRANCH_BG)
            .with_custom_text_color(theme::TEXT_ACCENT).with_font("mono", pixels(12))
            .with_alignment(TextAlignment::Left)
            .with_rounded_corners(theme::layout::ROUNDED_CORNERS)
            .with_corner_radius(theme::layout::RADIUS_CONTROL)
            .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis).with_debug_name("branch_switcher"));
        ui::set_tooltip(branchBtn.ent(), "Switch branch");
        if (branchBtn && repo && !repo->repoPath.empty()) {
            std::vector<ui::ContextMenuItem> items;
            for (const auto& candidate : repo->branches) {
                if (!candidate.isLocal) continue;
                items.push_back(ui::ContextMenuItem::item(
                    candidate.name + (candidate.isCurrent ? " (current)" : ""),
                    [path = repo->repoPath, name = candidate.name, dirty] {
                        if (dirty) {
                            if (auto* menu = find_singleton<MenuComponent>())
                                menu->pendingToasts.push_back({"Checking out with uncommitted changes in the working tree",
                                                               MenuComponent::Notice::Kind::Info});
                        }
                        enqueue_network_op("Checkout", git::git_run_async(path, {"switch", name}));
                    }, !candidate.isCurrent));
            }
            if (!items.empty()) ui::show_context_menu(ctx.mouse.pos.x, ctx.mouse.pos.y, std::move(items));
        }
        // No Sync pill here: the sync row under this header (Push / Pull /
        // Fetch / Stash with ahead/behind counts) is the one sync surface.
        // The pill duplicated it, with a subtly different Push (direct,
        // bypassing the push dialog) — one surface, one semantics.
        if (repo && !repo->repoPath.empty() && button(ctx, mk(row.ent(), 3), preset::Button("…")
            .with_size(ComponentSize{pixels(28), pixels(28)}).with_tooltip("Repository navigation")
            .with_debug_name("repository_navigation"))) {
            std::vector<ui::ContextMenuItem> items;
            for (auto kind : {git::catalog::Kind::Worktree, git::catalog::Kind::Submodule, git::catalog::Kind::Reflog, git::catalog::Kind::Stash})
                items.push_back(ui::ContextMenuItem::item(git::catalog::label(kind), [kind] {
                    auto* active = find_singleton<RepoComponent, ActiveTab>();
                    auto* layout = find_singleton<LayoutComponent>();
                    if (active && layout) navigation::open_catalog(*active, *layout, kind);
                }));
            const auto& pins = Settings::get().get_pinned_repos();
            const bool pinned = std::find(pins.begin(), pins.end(), repo->repoPath) != pins.end();
            items.push_back(ui::ContextMenuItem::item(pinned ? "Unpin repository" : "Pin repository", [path = repo->repoPath, pinned] {
                Settings::get().set_repo_pinned(path, !pinned);
            }));
            ui::show_context_menu(ctx.mouse.pos.x, ctx.mouse.pos.y, std::move(items));
        }
    }

    // Pull-conflict banner + resolve rows. Marking resolved is explicit:
    // staging a file says "this resolution is final"; completing the
    // merge is a separate button once no conflicts remain.
    static float conflict_banner_height(const RepoComponent& repo) {
        if (!repo.conflictedFiles.empty())
            return 30.f + static_cast<float>(std::min<size_t>(repo.conflictedFiles.size(), 5)) * 28.f;
        return repo.mergeInProgress ? 58.f : 0.f;
    }
    void render_conflict_banner(UIContext<InputAction>& ctx, Entity& parent, RepoComponent& repo) {
        const float height = conflict_banner_height(repo);
        if (height <= 0.f) return;
        auto banner = div(ctx, mk(parent, 2070), ComponentConfig{}
            .with_size(ComponentSize{percent(1.f), pixels(height)}).with_flex_direction(FlexDirection::Column)
            .with_custom_background(theme::STATUS_CONFLICT).with_debug_name("conflict_banner"));
        div(ctx, mk(banner.ent(), 0), ComponentConfig{}
            .with_label(repo.conflictedFiles.empty() ? "All conflicts resolved"
                : std::to_string(repo.conflictedFiles.size()) + " merge conflict(s) need resolution")
            .with_size(ComponentSize{percent(1.f), pixels(24)}).with_font_size(pixels(12))
            .with_padding(Padding{.left = pixels(8)})
            .with_custom_text_color(afterhours::Color{0, 0, 0, 255}).with_debug_name("conflict_banner_title"));
        if (repo.conflictedFiles.empty() && repo.mergeInProgress) {
            auto complete = button(ctx, mk(banner.ent(), 9), preset::Button("Complete merge")
                .with_size(ComponentSize{children(), pixels(26)}).with_font_size(pixels(12))
                .with_debug_name("conflict_complete_merge"));
            if (complete)
                enqueue_network_op("Complete merge", git::git_run_async(repo.repoPath, {"commit", "--no-edit"}));
            return;
        }
        const auto shown = std::min<size_t>(repo.conflictedFiles.size(), 5);
        for (size_t i = 0; i < shown; ++i) {
            const auto& path = repo.conflictedFiles[i];
            auto line = div(ctx, mk(banner.ent(), static_cast<int>(1 + i)), ComponentConfig{}
                .with_size(ComponentSize{percent(1.f), pixels(26)}).with_flex_direction(FlexDirection::Row)
                .with_align_items(AlignItems::Center).with_debug_name("conflict_file_row"));
            auto open = button(ctx, mk(line.ent(), 0), preset::Button(path)
                .with_size(ComponentSize{expand(), pixels(24)}).with_alignment(TextAlignment::Left)
                .with_font_size(pixels(12)).with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
                .with_debug_name("conflict_open_" + std::to_string(i)));
            ui::set_tooltip(open.ent(), "Open " + path + " to edit the conflict");
            if (open) navigation::open(repo, reading::source(path), true, reading::OpenMode::Keep);
            auto resolve = button(ctx, mk(line.ent(), 1), preset::Button("Mark resolved")
                .with_size(ComponentSize{children(), pixels(24)}).with_font_size(pixels(12))
                .with_debug_name("conflict_resolve_" + std::to_string(i)));
            if (resolve)
                enqueue_network_op("Mark resolved", git::git_run_async(repo.repoPath, {"add", "--", path}));
        }
        if (repo.conflictedFiles.size() > shown)
            div(ctx, mk(banner.ent(), 8), ComponentConfig{}
                .with_label("+" + std::to_string(repo.conflictedFiles.size() - shown) + " more in the file list")
                .with_size(ComponentSize{percent(1.f), pixels(20)}).with_font_size(pixels(11))
                .with_custom_text_color(theme::TEXT_SECONDARY).with_debug_name("conflict_more"));
    }

    // Sync actions (Push / Pull / Stash) grouped under the repo header so they
    // read as repo-scoped actions rather than floating chrome.
    void render_sync_row(UIContext<InputAction>& ctx, Entity& parent,
                         RepoComponent* repo) {
        bool hasRepo = repo && !repo->repoPath.empty();
        auto w = sidebarPixelWidth_ > 0 ? pixels(sidebarPixelWidth_) : percent(1.0f);
        auto row = div(ctx, mk(parent, 2085),
            ComponentConfig{}
                .with_size(ComponentSize{w, pixels(38)})
                .with_flex_direction(FlexDirection::Row)
                .with_align_items(AlignItems::Center)
                .with_gap(pixels(6))
                .with_custom_background(theme::SIDEBAR_BG)
                .with_padding(Padding{
                    .top = pixels(2), .right = pixels(10),
                    .bottom = pixels(6), .left = pixels(10)})
                .with_roundness(0.0f)
                .with_debug_name("sync_row"));

        auto syncBtn = [&](int id, const std::string& label, bool enabled, const std::string& debugName,
                           const std::string& tooltip = "") -> bool {
            // Compact, not expand(): stretching these across the row loses the
            // mock's look. Tighter side padding is what makes all three fit a
            // 200px sidebar, which is the minimum the divider can be dragged to.
            auto config = preset::Button(label, enabled)
                .with_size(ComponentSize{children(), pixels(28)})
                .with_padding(Padding{
                    .top = pixels(4), .right = pixels(8),
                    .bottom = pixels(4), .left = pixels(8)})
                .with_font_size(pixels(12))
                .with_cursor(afterhours::ui::CursorType::Pointer)
                .with_debug_name(debugName);
            if (enabled)
                config = config.with_transparent_bg()
                               .with_border(theme::BORDER, pixels(1))
                               .with_custom_text_color(theme::TEXT_SECONDARY);
            auto result = button(ctx, mk(row.ent(), id), config);
            if (!tooltip.empty()) ui::set_tooltip(result.ent(), tooltip);
            return static_cast<bool>(result);
        };

        // Plain ASCII labels (font atlas has no arrow glyphs); show ahead/behind
        // counts like the old toolbar did.
        std::string pushLabel = "Push";
        std::string pullLabel = "Pull";
        if (repo && repo->aheadCount > 0)
            pushLabel += " (" + std::to_string(repo->aheadCount) + ")";
        if (repo && repo->behindCount > 0)
            pullLabel += " (" + std::to_string(repo->behindCount) + ")";

        const bool pushBlocked = hasRepo && repo->behindCount > 0;
        if (syncBtn(2087, pushLabel, hasRepo && !pushBlocked, "sync_push",
                    pushBlocked ? "Pull first: " + std::to_string(repo->behindCount) + " commit(s) behind upstream" : "Push to upstream"))
            open_push_dialog(*repo);
        if (syncBtn(2088, pullLabel, hasRepo, "sync_pull", "Pull from upstream"))
            enqueue_network_op("Pull", git::git_run_async(repo->repoPath, {"pull"}));
        if (syncBtn(2090, "Fetch", hasRepo, "sync_fetch", "Fetch all remotes"))
            enqueue_network_op("Fetch", git::git_run_async(repo->repoPath, {"fetch", "--all"}));
        if (syncBtn(2089, "Stash", hasRepo, "sync_stash")) {
            auto* bd = find_singleton<BranchDialogState, ActiveTab>();
            auto* layout = find_singleton<LayoutComponent>();
            if (bd && layout) layout->open_modal(reading::focus::Popup::Stash);
        }

        // Upstream + last-fetched line under the buttons.
        if (hasRepo) {
            std::string upstream;
            for (const auto& branch : repo->branches)
                if (branch.isCurrent && !branch.upstream.empty()) upstream = branch.upstream;
            std::string stamp;
            const auto nowClock = std::chrono::steady_clock::now();
            if (fetchStampRepo_ != repo->repoPath || fetchStampVersion_ != repo->repoVersion ||
                nowClock - fetchStampCheckedAt_ >= std::chrono::seconds(3)) {
                fetchStampRepo_ = repo->repoPath;
                fetchStampVersion_ = repo->repoVersion;
                fetchStampCheckedAt_ = nowClock;
                fetchStampText_.clear();
                std::filesystem::path gitDir = std::filesystem::path(repo->repoPath) / ".git";
                std::error_code ec;
                if (std::filesystem::is_regular_file(gitDir, ec)) {
                    std::ifstream in(gitDir);
                    std::string line;
                    std::getline(in, line);
                    if (line.starts_with("gitdir: ")) gitDir = line.substr(8);
                }
                const auto fetchHead = gitDir / "FETCH_HEAD";
                if (std::filesystem::exists(fetchHead, ec)) {
                    const auto written = std::filesystem::last_write_time(fetchHead, ec);
                    if (!ec) {
                        const auto age = std::chrono::duration_cast<std::chrono::seconds>(
                            std::filesystem::file_time_type::clock::now() - written).count();
                        fetchStampText_ = age < 60 ? " · fetched just now" : age < 3600 ? " · fetched " + std::to_string(age / 60) + "m ago"
                            : age < 86400 ? " · fetched " + std::to_string(age / 3600) + "h ago"
                            : " · fetched " + std::to_string(age / 86400) + "d ago";
                    }
                }
            }
            stamp = fetchStampText_;
            if (!upstream.empty() || !stamp.empty()) {
                auto info = div(ctx, mk(parent, 2086), ComponentConfig{}
                    .with_size(ComponentSize{w, pixels(18)})
                    .with_custom_background(theme::SIDEBAR_BG)
                    .with_padding(Padding{.left = pixels(10)})
                    .with_debug_name("sync_upstream"));
                div(ctx, mk(info.ent(), 0), ComponentConfig{}
                    .with_label((upstream.empty() ? repo->currentBranch + " · no upstream"
                                                  : repo->currentBranch + " → " + upstream) + stamp)
                    .with_size(ComponentSize{afterhours::ui::expand(), pixels(18)})
                    .with_font_size(pixels(12)).with_custom_text_color(theme::TEXT_TERTIARY)
                    .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
                    .with_debug_name("sync_upstream_label"));
            }
        }
    }

    // Centered "No repository open" placeholder, shown in each sidebar pane
    // (files / refs / commit log) when no repo is loaded.
    void render_no_repo(UIContext<InputAction>& ctx, Entity& parent, int id,
                        const std::string& debugName) {
        div(ctx, mk(parent, id),
            ComponentConfig{}
                .with_label("Open a repository to begin.")
                .with_size(ComponentSize{percent(1.0f), pixels(32)})
                .with_padding(Padding{
                    .top = pixels(16), .right = pixels(8),
                    .bottom = pixels(8), .left = pixels(8)})
                .with_custom_text_color(theme::TEXT_SECONDARY)
                .with_alignment(TextAlignment::Center)
                .with_roundness(0.0f)
                .with_debug_name(debugName));
    }

    void render_review_progress(UIContext<InputAction>& ctx, Entity& parent,
                                RepoComponent& repo, ReviewComponent* review) {
        bool reviewing = review && review->reviewing;
        int toReview = static_cast<int>(repo.unstagedFiles.size());
        int approvedHunks = 0;
        int remainingHunks = 0;
        const bool checkApproved = review && !review->approvedHunks.empty();
        if (!checkApproved) {
            for (const auto& file : repo.currentDiff)
                remainingHunks += static_cast<int>(file.hunks.size());
        } else {
            auto& metrics = ui::diff_metrics();
            for (const auto& file : repo.currentDiff)
                for (const auto& hunk : file.hunks) {
                    if (review->approvedHunks.contains("wt\n" + metrics.hunk_key(file, hunk))) ++approvedHunks;
                    else ++remainingHunks;
                }
        }
        int totalHunks = approvedHunks + remainingHunks;
        float frac = totalHunks > 0
                         ? static_cast<float>(approvedHunks) / static_cast<float>(totalHunks) : 0.f;

        bool aside = reviewing && !repo.selectedCommitHash().empty();
        const std::string txt = totalHunks > 0
            ? std::to_string(approvedHunks) + " of " + std::to_string(totalHunks) + " hunks approved · " +
                  std::to_string(remainingHunks) + " to go"
            : "";

        auto w = sidebarPixelWidth_ > 0 ? pixels(sidebarPixelWidth_) : percent(1.0f);
        auto row = div(ctx, mk(parent, 2085),
            ComponentConfig{}
                .with_size(ComponentSize{w, pixels(60)})
                .with_flex_direction(FlexDirection::Column)
                .with_gap(pixels(4))
                .with_padding(Padding{
                    .top = pixels(4), .right = pixels(12),
                    .bottom = pixels(4), .left = pixels(12)})
                .with_custom_background(theme::SIDEBAR_BG)
                .with_roundness(0.0f)
                .with_debug_name("review_progress"));

        auto action = button(ctx, mk(row.ent(), 2), preset::Button(
            !reviewing ? "Review working changes" : aside ? "Back to working changes" : "Close working review", review != nullptr)
            .with_size(ComponentSize{percent(1.f), pixels(28)}).with_font_size(pixels(12))
            .with_debug_name("working_review_toggle"));
        if (review) {
            if (action) {
                if (aside) {
                    // Return to the stacked ballroom view without disembarking.
                    navigation::open(repo, reading::review("wt"), review->reviewing);
                } else {
                    review->reviewing = !review->reviewing;
                    review->mark_dirty();
                    if (review->reviewing) {
                        for (auto& fd : repo.currentDiff) {
                            review->seenSig[fd.filePath] = diff_signature(fd);
                        }
                        // The ballroom shows every working-tree file stacked, so
                        // don't pin a single selection — just clear it.
                        navigation::open(repo, reading::review("wt"), review->reviewing);
                        afterhours::toast::send_info(
                            ctx, "Review opened", 2.0f);
                    } else {
                        navigation::open(repo, reading::review("wt"), false);
                        afterhours::toast::send_info(ctx, "Working review closed",
                                                     1.5f);
                    }
                }
            }
        }

        auto status = div(ctx, mk(row.ent(), 3), ComponentConfig{}
            .with_size(ComponentSize{percent(1.f), pixels(20)})
            .with_flex_direction(FlexDirection::Row).with_align_items(AlignItems::Center)
            .with_gap(pixels(8)).with_debug_name("working_review_status"));
        if (totalHunks > 0) {
            auto bar = div(ctx, mk(status.ent(), 0),
                ComponentConfig{}
                    .with_size(ComponentSize{pixels(72), pixels(4)})
                    .with_custom_background(theme::BORDER)
                    .with_corner_radius(2.0f)
                    .with_debug_name("prog_bar"));
            if (frac > 0.f) div(ctx, mk(bar.ent(), 0),
                ComponentConfig{}
                    .with_size(ComponentSize{percent(frac), percent(1.0f)})
                    .with_custom_background(theme::STATUS_BAR_CLEAN)
                    .with_corner_radius(2.0f)
                    .with_debug_name("prog_fill"));
            div(ctx, mk(status.ent(), 1),
                ComponentConfig{}
                    .with_label(txt)
                    .with_size(ComponentSize{expand(), pixels(20)})
                    .with_custom_text_color(theme::TEXT_SECONDARY)
                    .with_font_size(pixels(12))
                    .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
                    .with_debug_name("prog_text"));
        }
    }

    // Height of the multi-line commit message box (logical px). Kept in sync
    // with COMMIT_AREA_H, which reserves the whole commit area's space.
    static constexpr float COMMIT_INPUT_H = 44.0f;

    // ---- Commit area (VS Code parity: always-visible input + button) ----
    void render_commit_area(UIContext<InputAction>& ctx,
                            Entity& parent,
                            RepoComponent& repo,
                            CommitEditorComponent& editor) {
        auto secWidth = sidebarPixelWidth_ > 0 ? pixels(sidebarPixelWidth_) : percent(1.0f);
        // Children must fit the commit_area content box (its width minus the 8px
        // left/right padding); using the full sidebar width overflows it now that
        // FlexWrap defaults to NoWrap.
        auto childW = sidebarPixelWidth_ > 0 ? pixels(sidebarPixelWidth_ - 16.0f)
                                             : percent(1.0f);

        auto commitArea = div(ctx, mk(parent, 2095),
            ComponentConfig{}
                .with_size(ComponentSize{secWidth, children()})
                .with_flex_direction(FlexDirection::Column)
                .with_padding(Padding{
                    .top = pixels(4), .right = pixels(8),
                    .bottom = pixels(2), .left = pixels(8)})
                .with_gap(pixels(3))
                .with_custom_background(theme::SIDEBAR_BG)
                .with_roundness(0.0f)
                .with_debug_name("commit_area"));

        const std::string branch = repo.currentBranch.empty() ? "main" : repo.currentBranch;

        // Multi-line message: Enter commits (submit_on_enter), Shift+Enter adds
        // a body line. The whole string is the commit message — build_message
        // passes a multi-line subject through as subject + body.
        auto inputResult = afterhours::text_input::text_area(
            ctx, mk(commitArea.ent(), 1),
            editor.subject,
            ComponentConfig{}
                .with_size(ComponentSize{childW, pixels(COMMIT_INPUT_H)})
                .with_custom_background(theme::INPUT_BG)
                .with_border(theme::BORDER, pixels(1.0f))
                .with_corner_radius(theme::layout::RADIUS_CONTROL)
                .with_line_height(pixels(18.0f))
                .with_placeholder("Commit message for " + branch)
                .with_submit_on_enter()
                .with_debug_name("commit_msg_input"));

        // The area puts the caret at the end of text set from outside, which
        // scrolls a multi-line template to its last line.
        if (editor.placeCaret && inputResult.ent().has<afterhours::text_input::HasTextAreaState>()) {
            auto& area = inputResult.ent().get<afterhours::text_input::HasTextAreaState>();
            area.cursor_position = std::min(*editor.placeCaret, editor.subject.size());
            area.scroll_offset_y = 0.f;
            editor.placeCaret.reset();
        }
        if (ctx.is_right_click(inputResult.ent().id)) {
            std::vector<ui::ContextMenuItem> types;
            for (auto type : commit_message::kConventionalTypes)
                types.push_back(ui::ContextMenuItem::item(std::string(type) + ":", [type] {
                    if (auto* ed = find_singleton<CommitEditorComponent, ActiveTab>()) {
                        ed->subject = commit_message::with_type(ed->subject, type);
                        ed->placeCaret = type.size() + 2;
                    }
                }));
            ui::show_context_menu(ctx.mouse.pos.x, ctx.mouse.pos.y, std::move(types));
        }

        // Enter fires on_submit -> request a commit.
        inputResult.ent().addComponentIfMissing<afterhours::text_input::HasTextInputListener>();
        inputResult.ent()
            .get<afterhours::text_input::HasTextInputListener>()
            .on_submit = [](Entity&) {
                auto* ed = find_singleton<CommitEditorComponent, ActiveTab>();
                if (ed) ed->request_commit();
            };

        // Split button: Commit keeps the configured unstaged policy (ask
        // by default); the chevron commits with a one-shot policy.
        bool hasStaged = !repo.stagedFiles.empty();
        auto commitRow = div(ctx, mk(commitArea.ent(), 2), ComponentConfig{}
            .with_size(ComponentSize{childW, pixels(28)}).with_flex_direction(FlexDirection::Row)
            .with_no_wrap().with_gap(pixels(3)).with_debug_name("commit_split_row"));
        auto commitBtn = button(ctx, mk(commitRow.ent(), 0),
            preset::Button("Commit", hasStaged)
                .with_size(ComponentSize{expand(), pixels(28)})
                .with_font_size(pixels(14))
                .with_debug_name("commit_btn_inline"));
        if (commitBtn && hasStaged) editor.request_commit();
        auto splitBtn = button(ctx, mk(commitRow.ent(), 1),
            preset::Button("v", hasStaged)
                .with_size(ComponentSize{pixels(36), pixels(28)})
                .with_font_size(pixels(12))
                .with_debug_name("commit_split_btn"));
        ui::set_tooltip(splitBtn.ent(), "Commit with a specific unstaged policy");
        if (splitBtn && hasStaged) {
            auto* ed = &editor;
            auto item = [ed](const std::string& label, CommitEditorComponent::UnstagedPolicy policy) {
                return ui::ContextMenuItem::item(label, [ed, policy] {
                    ed->commitPolicyOverride = policy;
                    ed->request_commit();
                });
            };
            ui::show_context_menu(ctx.mouse.pos.x, ctx.mouse.pos.y, {
                item("Commit Staged Only", CommitEditorComponent::UnstagedPolicy::CommitStagedOnly),
                item("Stage All & Commit", CommitEditorComponent::UnstagedPolicy::StageAll),
            });
        }
    }

    // ---- Refs view (T031) ----
    // Render the full refs/branches view in the sidebar
    void render_refs_view(UIContext<InputAction>& ctx,
                          Entity& parent,
                          RepoComponent& repo,
                          LayoutComponent& layout) {
        // Header with branch count and "+ New" button
        auto headerRow = div(ctx, mk(parent, 2160),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(28)})
                .with_flex_direction(FlexDirection::Row)
                .with_align_items(AlignItems::Center)
                .with_padding(Padding{
                    .top = pixels(4), .right = pixels(8),
                    .bottom = pixels(4), .left = pixels(8)})
                .with_custom_background(theme::SIDEBAR_BG)
                .with_roundness(0.0f)
                .with_debug_name("refs_header"));

        const auto localCount = std::count_if(repo.branches.begin(), repo.branches.end(),
            [](const BranchInfo& branch) { return branch.isLocal; });
        std::string branchLabel = "Branches · " + std::to_string(localCount);
        div(ctx, mk(headerRow.ent(), 1),
            preset::SectionHeader(branchLabel)
                // expand(), not percent(1): "+ New" shares this row, and a
                // full-width label pushed it outside the header.
                .with_size(ComponentSize{afterhours::ui::expand(), children()})
                .with_font("ui-bold", pixels(12))
                .with_font_size(pixels(12))
                .with_custom_text_color(theme::SECTION_HEADER_TEXT)
                .with_debug_name("branches_label"));

        // "+ New" button
        if (!repo.reviewWorkspace) {
            auto newBranchBtn = button(ctx, mk(headerRow.ent(), 2),
                preset::Button("+ New")
                    .with_size(ComponentSize{children(), pixels(28)})
                    .with_padding(Padding{
                        .top = pixels(6), .right = pixels(8),
                        .bottom = pixels(6), .left = pixels(8)})
                    .with_font_size(pixels(12))
                    .with_debug_name("new_branch_btn"));

            if (newBranchBtn) {
                auto* bd = find_singleton<BranchDialogState, ActiveTab>();
                auto* layout = find_singleton<LayoutComponent>();
                if (bd && layout) {
                    layout->open_modal(reading::focus::Popup::NewBranch);
                    bd->newBranchName.clear();
                }
            }
        }

        // Branch list. Render rows directly into `parent` (the outer files
        // ScrollPanel), NOT into a nested ScrollPanel — see render_file_list:
        // a container nested inside filesBg fails to render its children after
        // the UI has been exercised (framework nested-container bug).
        (void)layout;
        // The stash list is no longer spawned by every refresh (only this
        // pane reads it); ask for a Refs refresh once per generation.
        if (!repo.refresh_busy() && !repo.stashesRequested && repo.stashesGeneration != repo.dataGeneration) {
            repo.stashesRequested = true;
            repo.request_refresh(refresh_scope::Scope::Refs);
        }
        if (repo.branches.empty() && repo.tags.empty() && repo.stashes.empty()) {
            div(ctx, mk(parent, 2170),
                ComponentConfig{}
                    .with_label("No branches found")
                    .with_size(ComponentSize{percent(1.0f), pixels(32)})
                    .with_padding(Padding{
                        .top = pixels(16), .right = pixels(8),
                        .bottom = pixels(8), .left = pixels(8)})
                    .with_custom_text_color(theme::TEXT_TERTIARY)
                    .with_alignment(TextAlignment::Center)
                    .with_roundness(0.0f)
                    .with_debug_name("no_branches"));
            return;
        }

        int rowIndex = 0;
        for (const auto& branch : repo.branches)
            if (branch.isLocal) render_branch_row(ctx, parent, rowIndex++, branch, repo);

        auto section = [&](int id, const std::string& label, const std::string& debugName) {
            div(ctx, mk(parent, id), preset::SectionHeader(label)
                .with_size(ComponentSize{percent(1.0f), children()})
                .with_padding(Padding{.top = pixels(10), .left = pixels(8)})
                .with_font("ui-bold", pixels(12))
                .with_font_size(pixels(12))
                .with_custom_text_color(theme::SECTION_HEADER_TEXT)
                .with_debug_name(debugName));
        };
        const auto remoteCount = static_cast<int>(repo.branches.size()) - static_cast<int>(localCount);
        if (remoteCount > 0) {
            section(2180, "Remotes · " + std::to_string(remoteCount), "remotes_label");
            for (const auto& branch : repo.branches)
                if (!branch.isLocal) render_branch_row(ctx, parent, rowIndex++, branch, repo);
        }
        if (!repo.tags.empty()) {
            section(2181, "Tags · " + std::to_string(repo.tags.size()), "tags_label");
            for (size_t i = 0; i < repo.tags.size(); ++i) {
                auto tagRow = button(ctx, mk(parent, static_cast<int>(2400 + i)), preset::Button(repo.tags[i])
                    .with_size(ComponentSize{percent(1.0f), pixels(26)}).with_alignment(TextAlignment::Left)
                    .with_font_size(pixels(12)).with_debug_name("refs_tag_" + repo.tags[i]));
                ui::set_tooltip(tagRow.ent(), "Review tag " + repo.tags[i]);
                if (tagRow) navigation::click(repo, reading::review(repo.tags[i]), true, reading::ClickRegion::Tree);
            }
        }
        if (!repo.stashes.empty()) {
            section(2182, "Stashes · " + std::to_string(repo.stashes.size()), "stashes_label");
            for (size_t i = 0; i < repo.stashes.size(); ++i) {
                const auto& entry = repo.stashes[i];
                const auto ref = entry.substr(0, entry.find(':'));
                auto stashRow = button(ctx, mk(parent, static_cast<int>(2600 + i)), preset::Button(entry)
                    .with_size(ComponentSize{percent(1.0f), pixels(26)}).with_alignment(TextAlignment::Left)
                    .with_font_size(pixels(12)).with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
                    .with_debug_name("refs_stash_" + std::to_string(i)));
                ui::set_tooltip(stashRow.ent(), entry + " · click to review, right-click for Apply / Pop / Drop");
                if (stashRow)
                    navigation::open(repo, reading::review("compare:" + ref + "^1:" + ref), true, reading::OpenMode::Keep);
                if (ctx.is_right_click(stashRow.ent().id)) {
                    const auto path = repo.repoPath;
                    auto item = [&](const std::string& label, std::vector<std::string> args) {
                        return ui::ContextMenuItem::item(label, [path, args = std::move(args), label] {
                            enqueue_network_op(label, git::git_run_async(path, args));
                        });
                    };
                    ui::show_context_menu(ctx.mouse.pos.x, ctx.mouse.pos.y, {
                        item("Apply stash", {"stash", "apply", ref}),
                        item("Pop stash", {"stash", "pop", ref}),
                        item("Drop stash", {"stash", "drop", ref}),
                    });
                }
            }
        }
    }

    std::string lastBranchClick_;
    std::chrono::steady_clock::time_point lastBranchClickAt_{};

    // Render a single branch row with checkout and delete actions
    void render_branch_row(UIContext<InputAction>& ctx,
                           Entity& parent, int index,
                           const BranchInfo& branch,
                           RepoComponent& repo) {
        constexpr float ROW_H = 28.0f;
        bool isCurrent = branch.isCurrent;

        auto rowBg = isCurrent ? theme::SELECTED_BG : theme::SIDEBAR_BG;

        // Row container (use div + HasClickListener for reliable E2E click detection)
        auto rowResult = div(ctx, mk(parent, 2200 + index * 10),
            preset::SelectableRow(isCurrent)
                .with_size(ComponentSize{percent(1.0f), pixels(ROW_H)})
                .with_custom_background(rowBg)
                .with_margin(Margin{.left = pixels(8), .right = pixels(14)})
                .with_padding(Padding{
                    .top = pixels(0), .right = pixels(8),
                    .bottom = pixels(0), .left = pixels(0)})
                .with_rounded_corners(theme::layout::ROUNDED_CORNERS)
                .with_corner_radius(theme::layout::RADIUS_CONTROL)
                .with_debug_name("branch_row_" + branch.name));
        ui::set_tooltip(rowResult.ent(), isCurrent || !branch.isLocal ? branch.name : branch.name + " · Double-click to check out");

        rowResult.ent().addComponentIfMissing<HasClickListener>([](Entity&){});

        // Double-click -> checkout this branch. A single stray click used to
        // switch branches immediately, dirty tree or not.
        const bool clicked = rowResult.ent().get<HasClickListener>().down;
        const auto now = std::chrono::steady_clock::now();
        const bool doubleClicked = clicked && lastBranchClick_ == branch.name &&
                                   now - lastBranchClickAt_ <= std::chrono::milliseconds(500);
        if (clicked) {
            lastBranchClick_ = doubleClicked ? std::string{} : branch.name;
            lastBranchClickAt_ = now;
        }
        if (doubleClicked && branch.isLocal && !isCurrent && !repo.reviewWorkspace) {
            const bool dirty = !repo.unstagedFiles.empty() || !repo.stagedFiles.empty() || !repo.untrackedFiles.empty();
            if (dirty) {
                if (auto* menu = find_singleton<MenuComponent>())
                    menu->pendingToasts.push_back({"Checking out with uncommitted changes in the working tree",
                                                   MenuComponent::Notice::Kind::Info});
            }
            enqueue_network_op("Checkout", git::git_run_async(repo.repoPath, {"switch", branch.name}));
        }

        // Current branch: 2px accent left bar + "current" caption (colour is
        // never the only cue). Non-current rows keep a 2px spacer so names
        // share one left edge.
        if (isCurrent) {
            div(ctx, mk(rowResult.ent(), 1),
                ComponentConfig{}
                    .with_size(ComponentSize{pixels(2), pixels(ROW_H)})
                    .with_custom_background(theme::SELECTED_ACCENT)
                    .with_roundness(0.0f)
                    .with_debug_name("current_indicator"));
        } else {
            div(ctx, mk(rowResult.ent(), 1),
                ComponentConfig{}
                    .with_size(ComponentSize{pixels(2), pixels(ROW_H)})
                    .with_transparent_bg()
                    .with_roundness(0.0f));
        }

        // No L/R badge: the section header already says whether these
        // are local branches or remotes.

        // Branch name takes whatever the badge, tracking and delete button
        // leave. This used to subtract each sibling by hand because percent(1)
        // resolved to the whole row and overflowed them; expand() does it now.
        div(ctx, mk(rowResult.ent(), 3),
            ComponentConfig{}
                .with_label(branch.name)
                .with_margin(Margin{.left = pixels(8)})
                .with_size(ComponentSize{afterhours::ui::expand(), pixels(ROW_H)})
                .with_custom_text_color(theme::TEXT_PRIMARY)
                .with_font_size(pixels(14))
                .with_alignment(TextAlignment::Left)
                .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
                .with_roundness(0.0f)
                .with_debug_name("branch_name"));
        if (isCurrent) {
            div(ctx, mk(rowResult.ent(), 5), ComponentConfig{}
                .with_label("current")
                .with_size(ComponentSize{children(), pixels(ROW_H)})
                .with_font_size(pixels(12))
                .with_custom_text_color(theme::TEXT_ACCENT)
                .with_debug_name("branch_current_label"));
        }

        // Tracking as ↑/↓ icons with counts (the font has no arrows, so
        // chrome_icons draws them).
        if (!branch.tracking.empty()) {
            const auto [ahead, behind] = git::parse_ahead_behind(branch.tracking);
            auto tracking = div(ctx, mk(rowResult.ent(), 4), ComponentConfig{}
                .with_size(ComponentSize{children(), pixels(ROW_H)}).with_flex_direction(FlexDirection::Row)
                .with_align_items(AlignItems::Center).with_debug_name("branch_tracking"));
            auto count = [&](int id, const std::string& text) {
                div(ctx, mk(tracking.ent(), id), ComponentConfig{}.with_label(text)
                    .with_size(ComponentSize{children(), pixels(ROW_H)})
                    .with_custom_text_color(theme::TEXT_SECONDARY).with_font("mono", pixels(12))
                    .with_debug_name("branch_tracking_count"));
            };
            if (ahead > 0) {
                ui::chrome_icon(ctx, mk(tracking.ent(), 10), ui::ChromeIcon::ArrowUp, theme::TEXT_SECONDARY, "branch_ahead_icon");
                count(11, std::to_string(ahead));
            }
            if (behind > 0) {
                ui::chrome_icon(ctx, mk(tracking.ent(), 12), ui::ChromeIcon::ArrowDown, theme::TEXT_SECONDARY, "branch_behind_icon");
                count(13, std::to_string(behind));
            }
            if (ahead == 0 && behind == 0) count(14, "gone");
            ui::set_tooltip(tracking.ent(), branch.name + " vs " + branch.upstream + " " + branch.tracking);
        }

        // Delete is a hover / context-menu action, never a red × on every row.
        if (branch.isLocal && !isCurrent && !repo.reviewWorkspace) {
            auto askDelete = [this, &branch] {
                auto* bd = find_singleton<BranchDialogState, ActiveTab>();
                auto* layout = find_singleton<LayoutComponent>();
                if (bd && layout) {
                    bd->deleteBranchName = branch.name;
                    layout->open_modal(reading::focus::Popup::DeleteBranch);
                }
            };
            if (afterhours::ui::is_mouse_inside(ctx.mouse.pos, ui::visible_rect(rowResult.ent()))) {
                auto deleteBtn = button(ctx, mk(rowResult.ent(), 5),
                    preset::Button("x")
                        .with_size(ComponentSize{pixels(20), pixels(20)})
                        .with_custom_background(theme::BUTTON_SECONDARY)
                        .with_custom_text_color(theme::STATUS_DELETED)
                        .with_debug_name("delete_branch_btn"));
                if (deleteBtn) askDelete();
            }
            if (ctx.is_right_click(rowResult.ent().id)) {
                ui::show_context_menu(ctx.mouse.pos.x, ctx.mouse.pos.y, {
                    ui::ContextMenuItem::item("Delete branch...", [name = branch.name] {
                        auto* bd = find_singleton<BranchDialogState, ActiveTab>();
                        auto* layout = find_singleton<LayoutComponent>();
                        if (bd && layout) {
                            bd->deleteBranchName = name;
                            layout->open_modal(reading::focus::Popup::DeleteBranch);
                        }
                    })});
            }
        }

    }

    // ---- New Branch dialog (T031) ----
    void render_new_branch_dialog(UIContext<InputAction>& ctx,
                                   Entity& uiRoot,
                                   RepoComponent& repo,
                                   BranchDialogState& bd, LayoutComponent& layout) {
        if (!layout.modal_is(reading::focus::Popup::NewBranch)) return;

        using namespace afterhours;

        constexpr int MODAL_ID = 8100;
        constexpr int CONTENT_LAYER = 1001;

        bool dialogOpen = true;
        auto modalResult = afterhours::modal::detail::modal_impl(
            ctx, mk(uiRoot, MODAL_ID), dialogOpen,
            ModalConfig{}
                .with_size(pixels(380), pixels(180))
                .with_title("New Branch")
                .with_show_close_button(false));
        if (!dialogOpen) layout.close_modal(reading::focus::Popup::NewBranch);

        if (!modalResult) return;
        auto& modalEnt = modalResult.ent();

        // Label
        div(ctx, mk(modalEnt, 1),
            ComponentConfig{}
                .with_label("Branch name:")
                .with_size(ComponentSize{percent(1.0f), pixels(20)})
                .with_padding(Padding{
                    .top = pixels(8), .right = pixels(16),
                    .bottom = pixels(4), .left = pixels(16)})
                .with_custom_text_color(theme::TEXT_PRIMARY)
                .with_alignment(TextAlignment::Left)
                .with_render_layer(CONTENT_LAYER)
                .with_debug_name("new_branch_label"));

        // Text input
        afterhours::text_input::text_input(ctx, mk(modalEnt, 2),
            bd.newBranchName,
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(32)})
                .with_padding(Padding{
                    .top = pixels(0), .right = pixels(16),
                    .bottom = pixels(0), .left = pixels(16)})
                .with_background(afterhours::ui::Theme::Usage::Surface)
                .with_render_layer(CONTENT_LAYER)
                .with_debug_name("new_branch_input"));

        // Button row
        auto btnRow = div(ctx, mk(modalEnt, 3),
            preset::DialogButtonRow()
                .with_render_layer(CONTENT_LAYER)
                .with_debug_name("new_branch_buttons"));

        // Cancel
        if (button(ctx, mk(btnRow.ent(), 1),
            preset::Button("Cancel")
                .with_margin(Margin{
                    .top = {}, .bottom = {},
                    .left = {}, .right = pixels(8)})
                .with_custom_background(theme::BUTTON_SECONDARY)
                .with_custom_text_color(theme::TEXT_PRIMARY)
                .with_render_layer(CONTENT_LAYER)
                .with_debug_name("cancel_new_branch"))) {
            layout.close_modal(reading::focus::Popup::NewBranch);
            bd.newBranchName.clear();
        }

        // Create
        bool canCreate = !bd.newBranchName.empty();
        if (button(ctx, mk(btnRow.ent(), 2),
            preset::Button("Create", canCreate)
                .with_render_layer(CONTENT_LAYER)
                .with_debug_name("create_branch_btn"))) {
            if (canCreate) {
                auto result = git::create_branch(repo.repoPath, bd.newBranchName);
                toast_on_git_failure(result, "Create Branch");
                if (result.success()) {
                    repo.request_refresh();
                }
                layout.close_modal(reading::focus::Popup::NewBranch);
                bd.newBranchName.clear();
            }
        }
    }

    // ---- Stash dialog ----
    void render_stash_dialog(UIContext<InputAction>& ctx, Entity& uiRoot,
                             RepoComponent& repo, BranchDialogState& bd, LayoutComponent& layout) {
        if (!layout.modal_is(reading::focus::Popup::Stash)) return;
        using namespace afterhours;
        constexpr int MODAL_ID = 8400;
        constexpr int CONTENT_LAYER = 1001;
        bool dialogOpen = true;
        auto modalResult = afterhours::modal::detail::modal_impl(
            ctx, mk(uiRoot, MODAL_ID), dialogOpen,
            ModalConfig{}.with_size(pixels(400), pixels(210)).with_title("Stash Changes").with_show_close_button(false));
        if (!dialogOpen) layout.close_modal(reading::focus::Popup::Stash);
        if (!modalResult) return;
        auto& modalEnt = modalResult.ent();
        div(ctx, mk(modalEnt, 1), ComponentConfig{}.with_label("Message (optional):")
            .with_size(ComponentSize{percent(1.0f), pixels(20)})
            .with_padding(Padding{.top = pixels(8), .right = pixels(16), .bottom = pixels(4), .left = pixels(16)})
            .with_custom_text_color(theme::TEXT_PRIMARY).with_alignment(TextAlignment::Left)
            .with_render_layer(CONTENT_LAYER).with_debug_name("stash_label"));
        afterhours::text_input::text_input(ctx, mk(modalEnt, 2), bd.stashMessage,
            ComponentConfig{}.with_size(ComponentSize{percent(1.0f), pixels(32)})
                .with_padding(Padding{.right = pixels(16), .left = pixels(16)})
                .with_background(afterhours::ui::Theme::Usage::Surface)
                .with_render_layer(CONTENT_LAYER).with_debug_name("stash_message_input"));
        if (button(ctx, mk(modalEnt, 3), preset::Button(bd.stashIncludeUntracked ? "Include untracked: On" : "Include untracked: Off")
                .with_size(ComponentSize{children(), pixels(26)}).with_font_size(pixels(12))
                .with_render_layer(CONTENT_LAYER).with_debug_name("stash_untracked_toggle")))
            bd.stashIncludeUntracked = !bd.stashIncludeUntracked;
        auto btnRow = div(ctx, mk(modalEnt, 4), preset::DialogButtonRow()
            .with_render_layer(CONTENT_LAYER).with_debug_name("stash_buttons"));
        if (button(ctx, mk(btnRow.ent(), 1), preset::Button("Cancel")
                .with_margin(Margin{.right = pixels(8)})
                .with_custom_background(theme::BUTTON_SECONDARY).with_custom_text_color(theme::TEXT_PRIMARY)
                .with_render_layer(CONTENT_LAYER).with_debug_name("cancel_stash"))) {
            layout.close_modal(reading::focus::Popup::Stash);
            bd.stashMessage.clear();
        }
        if (button(ctx, mk(btnRow.ent(), 2), preset::Button("Stash")
                .with_render_layer(CONTENT_LAYER).with_debug_name("confirm_stash"))) {
            std::vector<std::string> args{"stash", "push", "-m",
                bd.stashMessage.empty() ? std::string("Stashed via floatinghotel") : bd.stashMessage};
            if (bd.stashIncludeUntracked) args.push_back("--include-untracked");
            enqueue_network_op("Stash", git::git_run_async(repo.repoPath, std::move(args)));
            layout.close_modal(reading::focus::Popup::Stash);
            bd.stashMessage.clear();
            bd.stashIncludeUntracked = false;
        }
    }

    // ---- Delete Branch confirmation dialog (T031) ----
    void render_delete_branch_dialog(UIContext<InputAction>& ctx,
                                      Entity& uiRoot,
                                      RepoComponent& repo,
                                      BranchDialogState& bd, LayoutComponent& layout) {
        if (!layout.modal_is(reading::focus::Popup::DeleteBranch)) return;

        using namespace afterhours;

        constexpr int MODAL_ID = 8200;
        constexpr int CONTENT_LAYER = 1001;

        std::string message = "Delete branch '" + bd.deleteBranchName + "'?\n"
            "This will delete the local branch. If it has unmerged\n"
            "changes, you will be prompted to force delete.";

        bool dialogOpen = true;
        auto modalResult = afterhours::modal::detail::modal_impl(
            ctx, mk(uiRoot, MODAL_ID), dialogOpen,
            ModalConfig{}
                .with_size(pixels(420), pixels(180))
                .with_title("Delete Branch")
                .with_show_close_button(false));
        if (!dialogOpen) layout.close_modal(reading::focus::Popup::DeleteBranch);

        if (!modalResult) return;
        auto& modalEnt = modalResult.ent();

        // Message
        div(ctx, mk(modalEnt, 1),
            preset::DialogMessage(message)
                .with_render_layer(CONTENT_LAYER)
                .with_debug_name("delete_msg"));

        // Button row
        auto btnRow = div(ctx, mk(modalEnt, 2),
            preset::DialogButtonRow()
                .with_render_layer(CONTENT_LAYER)
                .with_debug_name("delete_buttons"));

        // Cancel
        if (button(ctx, mk(btnRow.ent(), 1),
            preset::Button("Cancel")
                .with_margin(Margin{
                    .top = {}, .bottom = {},
                    .left = {}, .right = pixels(8)})
                .with_custom_background(theme::BUTTON_SECONDARY)
                .with_custom_text_color(theme::TEXT_PRIMARY)
                .with_render_layer(CONTENT_LAYER)
                .with_debug_name("cancel_delete"))) {
            layout.close_modal(reading::focus::Popup::DeleteBranch);
            bd.deleteBranchName.clear();
        }

        // Delete (red)
        if (button(ctx, mk(btnRow.ent(), 2),
            preset::Button("Delete")
                .with_custom_background(theme::STATUS_DELETED)
                .with_render_layer(CONTENT_LAYER)
                .with_debug_name("confirm_delete"))) {
            auto result = git::delete_branch(repo.repoPath,
                                              bd.deleteBranchName, false);
            if (result.success()) {
                repo.request_refresh();
                layout.close_modal(reading::focus::Popup::DeleteBranch);
                bd.deleteBranchName.clear();
            } else {
                layout.open_modal(reading::focus::Popup::ForceDeleteBranch);
            }
        }
    }

    // ---- Force Delete Branch dialog (T031) ----
    void render_force_delete_dialog(UIContext<InputAction>& ctx,
                                     Entity& uiRoot,
                                     RepoComponent& repo,
                                     BranchDialogState& bd, LayoutComponent& layout) {
        if (!layout.modal_is(reading::focus::Popup::ForceDeleteBranch)) return;

        using namespace afterhours;

        constexpr int MODAL_ID = 8300;
        constexpr int CONTENT_LAYER = 1001;

        std::string message = "Branch '" + bd.deleteBranchName +
            "' has unmerged changes.\n\n"
            "Force delete will permanently lose these changes.\n"
            "Are you sure?";

        bool dialogOpen = true;
        auto modalResult = afterhours::modal::detail::modal_impl(
            ctx, mk(uiRoot, MODAL_ID), dialogOpen,
            ModalConfig{}
                .with_size(pixels(420), pixels(200))
                .with_title("Force Delete Branch")
                .with_show_close_button(false));
        if (!dialogOpen) layout.close_modal(reading::focus::Popup::ForceDeleteBranch);

        if (!modalResult) return;
        auto& modalEnt = modalResult.ent();

        // Warning message
        div(ctx, mk(modalEnt, 1),
            preset::DialogMessage(message)
                .with_custom_text_color(theme::STATUS_CONFLICT)
                .with_render_layer(CONTENT_LAYER)
                .with_debug_name("force_delete_msg"));

        // Button row
        auto btnRow = div(ctx, mk(modalEnt, 2),
            preset::DialogButtonRow()
                .with_render_layer(CONTENT_LAYER)
                .with_debug_name("force_delete_buttons"));

        // Cancel
        if (button(ctx, mk(btnRow.ent(), 1),
            preset::Button("Cancel")
                .with_margin(Margin{
                    .top = {}, .bottom = {},
                    .left = {}, .right = pixels(8)})
                .with_custom_background(theme::BUTTON_SECONDARY)
                .with_custom_text_color(theme::TEXT_PRIMARY)
                .with_render_layer(CONTENT_LAYER)
                .with_debug_name("cancel_force_delete"))) {
            layout.close_modal(reading::focus::Popup::ForceDeleteBranch);
            bd.deleteBranchName.clear();
        }

        // Force Delete (red)
        if (button(ctx, mk(btnRow.ent(), 2),
            preset::Button("Force Delete")
                .with_custom_background(theme::STATUS_DELETED)
                .with_render_layer(CONTENT_LAYER)
                .with_debug_name("force_delete_btn"))) {
            auto result = git::delete_branch(repo.repoPath,
                                              bd.deleteBranchName, true);
            if (result.success()) {
                repo.request_refresh();
            }
            layout.close_modal(reading::focus::Popup::ForceDeleteBranch);
            bd.deleteBranchName.clear();
        }
    }


    // Render the file list with Staged, Changes, and Untracked sections
    // parentWidth: explicit pixel width to avoid percent resolution bug
    float sidebarPixelWidth_ = 0; // Set before rendering
    // Which review tab the sidebar is showing, and that tab's rows. The
    // windowed list above asks for a count and a row builder; render_file_list
    // below keeps the empty states.
    // One combined Changes list (staged, then unstaged, then untracked);
    // ReviewTab survives only as stored layout state.
    static LayoutComponent::ReviewTab active_review_tab() {
        return LayoutComponent::ReviewTab::ToReview;
    }

    std::vector<file_tree::Row> treeRows_;
    std::vector<std::string> treePaths_;
    std::set<std::string> treeCollapsed_;
    bool treeMode_ = false;
    bool allFilesMode_ = false;
    std::set<std::string> explorerCollapsed_;
    std::vector<size_t> fileIndices_;
    std::vector<char> treeKinds_;
    struct FileRowsKey {
        std::string repo;
        unsigned generation;
        unsigned patches;
        unsigned version;
        unsigned paths;
        LayoutComponent::ReviewTab tab;
        LayoutComponent::FileViewMode mode;
        bool operator==(const FileRowsKey&) const = default;
    };
    std::optional<FileRowsKey> fileRowsKey_;
    // Built file rows retained per (repo, generations, mode) so switching
    // back doesn't reflatten; capped at 4 entries.
    struct FileTreeCache {
        std::vector<file_tree::Row> rows;
        std::vector<std::string> paths;
        std::vector<char> kinds;
        std::vector<size_t> indices;
    };
    std::map<std::string, FileTreeCache> fileTreeCache_;
    std::vector<std::string> fileTreeCacheOrder_;
    static std::string file_tree_cache_key(const FileRowsKey& key, bool treeMode) {
        return key.repo + "\n" + std::to_string(key.generation) + ":" + std::to_string(key.patches) + ":" +
               std::to_string(key.version) + ":" + std::to_string(key.paths) + ":" +
               std::to_string(static_cast<int>(key.tab)) + ":" + std::to_string(static_cast<int>(key.mode)) +
               (treeMode ? ":tree" : ":flat");
    }
    void store_file_tree_cache(const std::string& cacheKey) {
        fileTreeCache_[cacheKey] = {treeRows_, treePaths_, treeKinds_, fileIndices_};
        std::erase(fileTreeCacheOrder_, cacheKey);
        fileTreeCacheOrder_.insert(fileTreeCacheOrder_.begin(), cacheKey);
        while (fileTreeCacheOrder_.size() > 4) {
            fileTreeCache_.erase(fileTreeCacheOrder_.back());
            fileTreeCacheOrder_.pop_back();
        }
    }

    void update_file_tree(UIContext<InputAction>& ctx, afterhours::ui::imm::EntityParent list,
                          RepoComponent& repo, const LayoutComponent& layout, const std::string& context) {
        auto publish = [&](std::vector<file_tree::Row> rows) {
            ui::replace_tree_rows(ctx, list, repo, repo.filesTreeNavigation, context, treeRows_, std::move(rows));
        };
        allFilesMode_ = layout.fileViewMode == LayoutComponent::FileViewMode::All;
        treeMode_ = layout.fileViewMode == LayoutComponent::FileViewMode::Tree;
        // ls-files is lazy now (full refreshes skip it): the All-files
        // tree is one of its two readers, so ask once per generation.
        if (allFilesMode_ && repo.allFilePaths.empty() && !repo.refresh_busy() &&
            repo.allFilePathsRequestedGeneration != repo.dataGeneration) {
            repo.allFilePathsRequestedGeneration = repo.dataGeneration;
            repo.request_refresh(refresh_scope::Scope::Worktree);
        }
        auto tab = active_review_tab();
        FileRowsKey key{repo.repoPath, repo.dataGeneration, repo.patchGeneration, repo.repoVersion, repo.allFilePathsGeneration, tab, layout.fileViewMode};
        if (allFilesMode_) {
            // Explorer: every file in the repo (tracked + untracked,
            // gitignore-respecting) as a collapsible directory tree, so the
            // whole codebase can be browsed like a local file explorer.
            auto collapsedIt = layout.collapsedDirectories.find(repo.repoPath);
            const std::set<std::string> noCollapsed;
            const auto& collapsed = collapsedIt == layout.collapsedDirectories.end() ? noCollapsed : collapsedIt->second;
            if (!fileRowsKey_ || *fileRowsKey_ != key || collapsed != explorerCollapsed_) {
                explorerCollapsed_ = collapsed;
                std::string collapsedKey;
                for (const auto& dir : collapsed) collapsedKey += dir + "\n";
                const auto explorerKey = file_tree_cache_key(key, true) + ":explorer:" + collapsedKey;
                if (auto cached = fileTreeCache_.find(explorerKey); cached != fileTreeCache_.end()) {
                    publish(cached->second.rows);
                } else {
                    publish(file_tree::flatten(repo.allFilePaths, explorerCollapsed_));
                    fileTreeCache_[explorerKey] = {treeRows_, {}, {}, {}};
                    std::erase(fileTreeCacheOrder_, explorerKey);
                    fileTreeCacheOrder_.insert(fileTreeCacheOrder_.begin(), explorerKey);
                    while (fileTreeCacheOrder_.size() > 4) {
                        fileTreeCache_.erase(fileTreeCacheOrder_.back());
                        fileTreeCacheOrder_.pop_back();
                    }
                }
                fileRowsKey_ = std::move(key);
            }
            return;
        }
        auto it = layout.collapsedDirectories.find(repo.repoPath);
        const std::set<std::string> noCollapsedDirectories;
        const auto& collapsed = it == layout.collapsedDirectories.end() ? noCollapsedDirectories : it->second;
        if (fileRowsKey_ && *fileRowsKey_ == key) {
            if (treeMode_ && collapsed != treeCollapsed_) {
                treeCollapsed_ = collapsed;
                publish(file_tree::flatten(treePaths_, treeCollapsed_));
            }
            return;
        }
        const auto cacheKey = file_tree_cache_key(key, treeMode_);
        if (auto cached = fileTreeCache_.find(cacheKey); cached != fileTreeCache_.end()) {
            treePaths_ = cached->second.paths;
            treeKinds_ = cached->second.kinds;
            fileIndices_ = cached->second.indices;
            treeCollapsed_ = collapsed;
            fileRowsKey_ = std::move(key);
            publish(cached->second.rows);
            return;
        }
        fileRowsKey_ = key;
        std::vector<std::string> paths;
        fileIndices_.clear();
        // The working-changes list always shows every changed file. The
        // review filters (repo.fileFilter) belong to the diff pane's
        // review views; they never cut this list down.
        auto append = [&](const std::string& path, size_t index, char /*change*/, const std::string& /*oldPath*/ = "") {
            paths.push_back(path);
            fileIndices_.push_back(index);
        };
        // Combined: staged first, then unstaged, then untracked (a path can
        // appear twice, once per section, when partially staged).
        treeKinds_.clear();
        auto appendKind = [&](const std::string& path, size_t i, char change, char kind, const std::string& oldPath = "") {
            size_t before = paths.size();
            append(path, i, change, oldPath);
            if (paths.size() > before) treeKinds_.push_back(kind);
        };
        for (size_t i = 0; i < repo.stagedFiles.size(); ++i) appendKind(repo.stagedFiles[i].path, i, repo.stagedFiles[i].indexStatus, 'S', repo.stagedFiles[i].origPath);
        for (size_t i = 0; i < repo.unstagedFiles.size(); ++i) appendKind(repo.unstagedFiles[i].path, i, repo.unstagedFiles[i].workTreeStatus, 'U', repo.unstagedFiles[i].origPath);
        for (size_t i = 0; i < repo.untrackedFiles.size(); ++i) appendKind(repo.untrackedFiles[i], i, 'A', '?');
        std::vector<size_t> order(paths.size());
        std::iota(order.begin(), order.end(), size_t{0});
        std::vector<size_t> indices;
        std::vector<char> kinds;
        treePaths_.clear();
        for (size_t index : order) {
            indices.push_back(fileIndices_[index]);
            kinds.push_back(treeKinds_[index]);
            treePaths_.push_back(std::move(paths[index]));
        }
        fileIndices_ = std::move(indices);
        treeKinds_ = std::move(kinds);
        treeCollapsed_ = collapsed;
        // Grouped sections replace the old Unstaged/Staged segment: one
        // list, Staged / Changes / Untracked headers, staged files marked
        // by their section instead of a mode switch.
        std::vector<file_tree::Row> rows;
        auto header = [&](const std::string& label, const std::string& scope) {
            file_tree::Row row;
            row.section = true;
            row.label = label;
            row.scope = scope;
            rows.push_back(std::move(row));
        };
        auto groupPaths = [&](char kind) {
            std::vector<std::string> group;
            for (size_t i = 0; i < treePaths_.size(); ++i)
                if (treeKinds_[i] == kind) group.push_back(treePaths_[i]);
            return group;
        };
        auto emit = [&](char kind, const std::string& label) {
            const auto group = groupPaths(kind);
            if (group.empty()) return;
            header(label + "  " + std::to_string(group.size()), kind == 'S' ? "index" : "wt");
            if (treeMode_) {
                auto sub = file_tree::flatten(group, treeCollapsed_);
                // flatten indexes into `group`; remap to treePaths_ indexes.
                for (auto& row : sub) {
                    if (!row.directory) {
                        size_t at = 0;
                        while (at < treePaths_.size() && !(treePaths_[at] == group[row.sourceIndex] && treeKinds_[at] == kind)) ++at;
                        row.sourceIndex = at;
                    }
                    rows.push_back(std::move(row));
                }
            } else {
                for (size_t i = 0; i < treePaths_.size(); ++i)
                    if (treeKinds_[i] == kind) rows.push_back({treePaths_[i], i, 0, false});
            }
        };
        emit('S', "Staged");
        emit('U', "Changes");
        emit('?', "Untracked");
        publish(std::move(rows));
        store_file_tree_cache(cacheKey);
    }

    size_t active_file_count(const RepoComponent&) const {
        // Published rows include section headers; the virtual list must
        // count them or the last files never render.
        return treeRows_.size();
    }

    void render_active_file_row(UIContext<InputAction>& ctx, Entity& row,
                                size_t i, RepoComponent& repo) {
        if (allFilesMode_) {
            const auto& node = treeRows_[i];
            if (node.directory) {
                if (ui::file_tree_style::directory(ctx, row, node, sidebarPixelWidth_,
                        file_tree::directory_collapsed(node, explorerCollapsed_), "explorer_directory:" + node.path, repo, repo.filesTreeNavigation)) {
                    auto& collapsed = find_singleton<LayoutComponent>()->collapsedDirectories[repo.repoPath];
                    file_tree::toggle_directory(node, collapsed);
                }
                return;
            }
            const auto& path = repo.allFilePaths[node.sourceIndex];
            ensure_status_maps(repo);
            char status = ' ';
            if (auto it = allFilesStatusMap_.find(path); it != allFilesStatusMap_.end()) status = it->second;
            render_file_row_impl(ctx, row, 0, path, status, repo, false, false, {}, node.depth);
            return;
        }
        if (treeRows_[i].section) {
            // Clicking a section header opens that scope's aggregate
            // working-changes document (what the old segment did).
            auto headerBtn = button(ctx, mk(row, 0), preset::Button(treeRows_[i].label)
                .with_size(ComponentSize{percent(1.0f), pixels(26)}).with_alignment(TextAlignment::Left)
                .with_text_inset(12.f, 0.f)
                .with_font_size(pixels(12)).with_custom_text_color(theme::TEXT_SECONDARY)
                .with_debug_name(treeRows_[i].scope == "index" ? "review_staged_changes" : "review_unstaged_changes"));
            if (headerBtn) navigation::open(repo, reading::review(treeRows_[i].scope), true);
            return;
        }
        size_t depth = 0;
        {
            const auto& node = treeRows_[i];
            depth = node.depth;
            if (treeMode_ && node.directory) {
                if (ui::file_tree_style::directory(ctx, row, node, sidebarPixelWidth_,
                        file_tree::directory_collapsed(node, treeCollapsed_), "tree_directory:" + node.path, repo, repo.filesTreeNavigation)) {
                    auto& collapsed = find_singleton<LayoutComponent>()->collapsedDirectories[repo.repoPath];
                    file_tree::toggle_directory(node, collapsed);
                }
                return;
            }
            // Rows carry their treePaths_ index (headers offset the list).
            i = node.sourceIndex;
        }
        char kind = i < treeKinds_.size() ? treeKinds_[i] : 'U';
        i = fileIndices_[i];
        if (kind == 'S') render_file_row(ctx, row, 0, repo.stagedFiles[i], repo, true, depth);
        else if (kind == '?') render_untracked_row(ctx, row, 0, repo.untrackedFiles[i], repo, depth);
        else render_file_row(ctx, row, 0, repo.unstagedFiles[i], repo, false, depth);
    }

    void render_file_list(UIContext<InputAction>& ctx,
                          Entity& scrollParent,
                          RepoComponent& repo) {
        if (repo.repoPath.empty()) {
            div(ctx, mk(scrollParent, 2500), preset::EmptyStateText("No repository open")
                .with_size(ComponentSize{percent(1.0f), pixels(28)})
                .with_debug_name("no_repository"));
            return;
        }
        if (!repo.filesError.empty()) {
            if (button(ctx, mk(scrollParent, 2500), preset::Button("Retry repository read")
                    .with_size(ComponentSize{percent(1.0f), pixels(28)})
                    .with_debug_name("retry_repository_read")))
                repo.request_refresh();
            div(ctx, mk(scrollParent, 2501), preset::EmptyStateText(
                    repo.filesError.substr(0, repo.filesError.find('\n')))
                .with_size(ComponentSize{percent(1.0f), pixels(28)})
                .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
                .with_debug_name("repository_read_error"));
            return;
        }
        if (allFilesMode_) {
            // Only reached when the explorer tree is empty (the windowed
            // list handles the non-empty case).
            div(ctx, mk(scrollParent, 2500), preset::EmptyStateText(repo.has_loaded_once() ? "No files in this repository" : "Loading files...")
                .with_size(ComponentSize{percent(1.0f), pixels(28)})
                .with_debug_name("explorer_empty"));
            return;
        }
        bool empty = repo.stagedFiles.empty() &&
                     repo.unstagedFiles.empty() &&
                     repo.untrackedFiles.empty();

        if (empty) {
            if (!repo.has_loaded_once()) {
                // Initial load in progress — show spinner
                static int spinIdx = 0;
                static int frameCounter = 0;
                constexpr const char* spinFrames[] = {
                    "\xe2\xa0\x8b", "\xe2\xa0\x99", "\xe2\xa0\xb9",
                    "\xe2\xa0\xb8", "\xe2\xa0\xbc", "\xe2\xa0\xb4",
                    "\xe2\xa0\xa6", "\xe2\xa0\xa7", "\xe2\xa0\x87",
                    "\xe2\xa0\x8f"  // braille spinner: ⠋⠙⠹⠸⠼⠴⠦⠧⠇⠏
                };
                if (++frameCounter >= 6) { frameCounter = 0; spinIdx = (spinIdx + 1) % 10; }
                std::string label = std::string(spinFrames[spinIdx]) + "  Loading\xe2\x80\xa6";

                div(ctx, mk(scrollParent, 2500),
                    ComponentConfig{}
                        .with_label(label)
                        .with_size(ComponentSize{percent(1.0f), pixels(28)})
                        .with_padding(Padding{
                            .top = pixels(20), .right = pixels(8),
                            .bottom = pixels(4), .left = pixels(8)})
                        .with_custom_text_color(theme::TEXT_SECONDARY)
                        .with_alignment(TextAlignment::Center)
                        .with_roundness(0.0f)
                        .with_debug_name("loading_spinner"));
            } else {
                div(ctx, mk(scrollParent, 2500),
                    preset::EmptyStateText("\xe2\x9c\x93 No changes")
                        .with_size(ComponentSize{percent(1.0f), pixels(28)})
                        .with_padding(Padding{
                            .top = pixels(20), .right = pixels(8),
                            .bottom = pixels(4), .left = pixels(8)})
                        .with_debug_name("empty_changes"));

                div(ctx, mk(scrollParent, 2501),
                    ComponentConfig{}
                        .with_label("Working tree clean")
                        .with_size(ComponentSize{percent(1.0f), pixels(22)})
                        .with_padding(Padding{
                            .top = pixels(0), .right = pixels(8),
                            .bottom = pixels(8), .left = pixels(8)})
                        .with_custom_text_color(theme::TEXT_TERTIARY)
                        .with_alignment(TextAlignment::Center)
                        .with_roundness(0.0f)
                        .with_debug_name("empty_clean"));
            }
            return;
        }

        int nextId = 2600;
        size_t shown = 0;
        const char* emptyMsg = "Nothing to review";
        for (int i = 0; i < static_cast<int>(repo.stagedFiles.size()); ++i)
            render_file_row(ctx, scrollParent, nextId++, repo.stagedFiles[i], repo, true);
        shown += repo.stagedFiles.size();
        for (int i = 0; i < static_cast<int>(repo.unstagedFiles.size()); ++i)
            render_file_row(ctx, scrollParent, nextId++, repo.unstagedFiles[i], repo, false);
        shown += repo.unstagedFiles.size();
        for (int i = 0; i < static_cast<int>(repo.untrackedFiles.size()); ++i)
            render_untracked_row(ctx, scrollParent, nextId++, repo.untrackedFiles[i], repo);
        shown += repo.untrackedFiles.size();
        if (shown == 0) {
            div(ctx, mk(scrollParent, 2599),
                ComponentConfig{}
                    .with_label(emptyMsg)
                    .with_size(ComponentSize{percent(1.0f), pixels(24)})
                    .with_padding(Padding{
                        .top = pixels(12), .right = pixels(8),
                        .bottom = pixels(4), .left = pixels(8)})
                    .with_custom_text_color(theme::pick({110, 110, 110, 255}, {140, 140, 144, 255}))
                    .with_alignment(TextAlignment::Center)
                    .with_roundness(0.0f)
                    .with_debug_name("tab_empty"));
        }
    }

    // Render a file row: [filename] [dir (gray)] [status badge]
    void render_file_row(UIContext<InputAction>& ctx,
                         Entity& parent, int id,
                         const FileStatus& file,
                         RepoComponent& repo, bool staged, size_t depth = 0) {
        char statusChar = staged ? file.indexStatus : file.workTreeStatus;
        if (statusChar == ' ' || statusChar == '\0') {
            statusChar = staged ? 'A' : 'M';
        }
        render_file_row_impl(ctx, parent, id, file.path, statusChar, repo,
                             file.isSubmodule, staged, file.origPath, depth);
    }

    void render_untracked_row(UIContext<InputAction>& ctx,
                               Entity& parent, int id,
                               const std::string& path,
                               RepoComponent& repo, size_t depth = 0) {
        render_file_row_impl(ctx, parent, id, path, 'U', repo, false, false, {}, depth);
    }

    void render_file_row_impl(UIContext<InputAction>& ctx,
                               Entity& parent, int id,
                               const std::string& path, char statusChar,
                               RepoComponent& repo, bool isSubmodule,
                               bool staged, const std::string& oldPath = "", size_t depth = 0) {
        bool selected = path == (source_tab_active(repo) ? repo.fullFilePath() : repo.selectedFilePath());

        std::string fname = sidebar_detail::basename_from_path(path);
        std::string dir = sidebar_detail::dir_from_path(path);
        if (treeMode_ || allFilesMode_) {
            dir.clear();
        }
        // Submodules show "S" (gitlink pointer change) rather than the raw M/A.
        std::string statusStr(1, isSubmodule ? 'S' : statusChar);

        auto row = div(ctx, mk(parent, id),
            ui::file_tree_style::row_config(sidebarPixelWidth_,
                depth, selected)
                .with_debug_name("file_row"));
        ui::bind_tree_row(ctx, row.ent(), repo, repo.filesTreeNavigation, path);

        row.ent().addComponentIfMissing<HasClickListener>([](Entity&){});

        const auto textCol = theme::TEXT_PRIMARY;
        constexpr float STATUS_W = 20.0f;
        constexpr float PAD_L = 8.0f;
        constexpr float PAD_R = 4.0f;
        // The filename column is expand(), so autolayout hands it whatever the
        // status letter and dir column leave. This used to be arithmetic here
        // because expand() in a Row took the full parent width instead of the
        // remainder; that is fixed, so only the dir column needs a size. Keep
        // it compact and right-aligned so the name gets the rest rather than
        // both columns floating in the middle with big gaps.
        float totalW = std::max(sidebarPixelWidth_ - PAD_L - PAD_R, 40.0f);
        float dirW = dir.empty() ? 0.0f : std::min(totalW * 0.4f, 90.0f);

        // Leading status glyph (left column) — matches the commit-detail file
        // rows and the mock, and gives every filename a consistent start x.
        auto statusCol = isSubmodule ? theme::STATUS_SUBMODULE
                                     : theme::statusColor(statusChar);
        div(ctx, mk(row.ent(), 3),
            preset::MetaText(statusStr)
                .with_size(ComponentSize{pixels(STATUS_W), pixels(28)})
                .with_font("mono", pixels(12))
                .with_custom_text_color(statusCol)
                .with_alignment(TextAlignment::Center)
                .with_debug_name("file_status"));

        auto fileLabel = div(ctx, mk(row.ent(), 1),
            preset::BodyText(fname)
                .with_size(ComponentSize{afterhours::ui::expand(), pixels(28)})
                .with_font_size(pixels(14))
                .with_custom_text_color(textCol)
                .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
                .with_debug_name("file_name"));
        ui::set_truncated_tooltip(row.ent(), path, fileLabel.ent());

        // Directory hint sits right after the filename (not floated far-right).
        if (!dir.empty()) {
            auto dirCol = theme::TEXT_SECONDARY;
            auto directoryLabel = div(ctx, mk(row.ent(), 2),
                preset::MetaText(dir)
                    .with_size(ComponentSize{pixels(dirW), children()})
                    .with_custom_text_color(dirCol)
                    .with_font_size(pixels(12))
                    .with_alignment(TextAlignment::Left)
                    .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
                    .with_debug_name("file_dir"));
            row.ent().get<ui::TruncatedTooltip>().labels.push_back(directoryLabel.ent().id);
        }

        if (auto* rowReview = find_singleton<ReviewComponent, ActiveTab>(); rowReview && !allFilesMode_) {
            ensure_diff_maps(repo);
            const auto& diffMap = staged ? stagedDiffMap_ : currentDiffMap_;
            const auto foundDiff = diffMap.find(path);
            const FileDiff* rowDiffPtr = foundDiff != diffMap.end() ? foundDiff->second : nullptr;
            bool changed = false;
            if (!staged && rowDiffPtr) {
                auto seen = rowReview->seenSig.find(path);
                changed = seen != rowReview->seenSig.end() && ui::diff_metrics().signature(*rowDiffPtr) != seen->second;
            }
            ui::file_tree_style::review_indicator(ctx, row.ent(), rowDiffPtr != nullptr &&
                ui::reviewed_file(*rowReview, staged ? "index" : "wt", *rowDiffPtr),
                unresolved_file_count(*rowReview, staged ? "index" : "wt", path, oldPath), changed);
        }

        if (row.ent().get<HasClickListener>().down) {
            auto* r = find_singleton<RepoComponent, ActiveTab>();
            if (r) {
                if (allFilesMode_) navigation::click(*r, reading::source(path), afterhours::input::is_key_pressed(afterhours::keys::ENTER));
                else navigation::click(*r, reading::review(staged ? "index" : "wt", path), afterhours::input::is_key_pressed(afterhours::keys::ENTER));
            }
        }

        if (ctx.is_right_click(row.ent().id)) {
            ui::remember_focus_origin(ctx, row.ent());
            open_file_context_menu(ctx, path, repo, staged);
        }
    }

    // Whichever of stage/unstage applies, plus the path. No Discard: there is
    // no discard_file command yet, and a destructive one wants a confirm.
    void open_file_context_menu(UIContext<InputAction>& ctx,
                                const std::string& path,
                                RepoComponent& repo, bool staged) {
        const std::string repoPath = repo.repoPath;
        auto* owner = find_singleton_entity<RepoComponent, ActiveTab>();
        auto canMutate = [ownerId = owner ? std::optional(owner->id) : std::nullopt, repoPath] {
            auto* active = find_singleton_entity<RepoComponent, ActiveTab>();
            return ownerId && active && active->id == *ownerId && !active->cleanup &&
                active->get<RepoComponent>().repoPath == repoPath && !active->get<RepoComponent>().reviewWorkspace;
        };

        std::vector<ui::ContextMenuItem> items;
        if (staged && !repo.reviewWorkspace) {
            items.push_back(ui::ContextMenuItem::item(
                "Unstage", [repoPath, path, canMutate] {
                    if (!canMutate()) return;
                    enqueue_network_op("Unstage file", git::git_run_async(repoPath, {"restore", "--staged", "--", path}));
                }));
        } else if (!repo.reviewWorkspace) {
            items.push_back(ui::ContextMenuItem::item(
                "Stage", [repoPath, path, canMutate] {
                    if (!canMutate()) return;
                    enqueue_network_op("Stage file", git::git_run_async(repoPath, {"add", "--", path}));
                }));
        }
        items.push_back(ui::ContextMenuItem::separator());
        items.push_back(ui::ContextMenuItem::item("File History", [repoPath, path] {
            auto* active = find_singleton<RepoComponent, ActiveTab>();
            if (active && active->repoPath == repoPath) open_file_history(*active, path);
        }));
        items.push_back(ui::ContextMenuItem::item("Copy Path", [path] {
            afterhours::clipboard::set_text(path);
        }));

        ui::show_context_menu(ctx.mouse.pos.x, ctx.mouse.pos.y,
                              std::move(items));
    }

    // Toast the failure, then refresh either way -- a partial failure still
    // moved the index.
    // ---- Commit log rendering (T021) ----

    // Render all commit log entries in a scrollable list
    // TODO(local-first): a time-travel scrubber over this commit stack — step
    // the diff/repo view back through history with recently-arrived changes
    // highlighted (Ink & Switch "visualizing document history"). See
    // docs/afterhours-persistence-proposal.md.
    void render_commit_log_entries(UIContext<InputAction>& ctx,
                                   Entity& scrollParent,
                                   RepoComponent& repo) {
        if (repo.commitLog.empty()) {
            // On a loaded machine the log can trail the diff by seconds;
            // "No commits yet" during that window reads as a broken sidebar.
            div(ctx, mk(scrollParent, 0),
                preset::EmptyStateText(repo.commitLogPage.busy() ? "Loading commits..." : !repo.historyError.empty() ? repo.historyError : !repo.commitLogPage.error.empty() && repo.commitLogPage.phase == LogPhase::Error ? repo.commitLogPage.error : "No commits yet")
                    .with_size(ComponentSize{percent(1.0f), pixels(32)})
                    .with_padding(Padding{
                        .top = pixels(16), .right = pixels(8),
                        .bottom = pixels(8), .left = pixels(8)})
                    .with_debug_name("empty_log"));
            return;
        }

        int count = static_cast<int>(repo.commitLog.size());

        for (int i = 0; i < count; ++i) {
            render_commit_row(ctx, scrollParent, i, repo.commitLog[i], repo);
        }

        if (repo.commitLogPage.hasMore) render_lazy_load_row(ctx, scrollParent, repo);
    }

    // Lazy load indicator at the bottom of the log.
    void render_lazy_load_row(UIContext<InputAction>& ctx, Entity& parent, RepoComponent& repo) {
        auto& page = repo.commitLogPage;
        if (page.error.empty() && !page.future.valid() && !page.busy()) page.request_page();
        const bool loading = page.busy() || page.future.valid();
        auto row = button(ctx, mk(parent, 9990),
            ComponentConfig{}
                .with_label(loading ? "Loading older commits..." : "Load failed · Retry")
                .with_disabled(loading)
                .with_consumes_directional_input()
                .with_size(ComponentSize{percent(1.0f), pixels(24)})
                .with_padding(Padding{
                    .top = pixels(3), .right = pixels(8),
                    .bottom = pixels(3), .left = pixels(8)})
                .with_custom_text_color(loading ? theme::TEXT_SECONDARY : theme::TEXT_PRIMARY)
                .with_font_size(pixels(12))
                .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
                .with_alignment(TextAlignment::Center)
                .with_roundness(0.0f)
                .with_debug_name("lazy_load"));
        ui::bind_focus(row.ent(), repo, reading::focus::Region::History);
        if (!page.error.empty()) ui::set_tooltip(row.ent(), page.error);
        if (row && !loading) page.request_page();
    }

    commit_graph::Graph graph_;
    std::string graphKey_;

    // Cached per-repo FETCH_HEAD lookup (avoids 3 VFS stat() syscalls/frame).
    std::string fetchStampRepo_;
    uint64_t fetchStampVersion_ = 0;
    std::chrono::steady_clock::time_point fetchStampCheckedAt_{};
    std::string fetchStampText_;

    // O(1) lookup maps for visible sidebar file rows (replaces O(N) linear
    // scans over repo.currentDiff / stagedFiles / unstagedFiles per row/frame).
    std::string diffMapKey_;
    std::unordered_map<std::string, const FileDiff*> currentDiffMap_;
    std::unordered_map<std::string, const FileDiff*> stagedDiffMap_;
    std::string statusMapKey_;
    std::unordered_map<std::string, char> allFilesStatusMap_;

    void ensure_diff_maps(const RepoComponent& repo) {
        std::string key = repo.repoPath + "\n" + std::to_string(repo.patchGeneration) + ":" +
            std::to_string(repo.currentDiff.size()) + ":" + std::to_string(repo.stagedDiff.size());
        if (key == diffMapKey_) return;
        diffMapKey_ = std::move(key);
        currentDiffMap_.clear();
        currentDiffMap_.reserve(repo.currentDiff.size());
        for (const auto& f : repo.currentDiff) currentDiffMap_.emplace(f.filePath, &f);
        stagedDiffMap_.clear();
        stagedDiffMap_.reserve(repo.stagedDiff.size());
        for (const auto& f : repo.stagedDiff) stagedDiffMap_.emplace(f.filePath, &f);
    }

    void ensure_status_maps(const RepoComponent& repo) {
        std::string key = repo.repoPath + "\n" + std::to_string(repo.dataGeneration) + ":" +
            std::to_string(repo.repoVersion);
        if (key == statusMapKey_) return;
        statusMapKey_ = std::move(key);
        allFilesStatusMap_.clear();
        allFilesStatusMap_.reserve(repo.stagedFiles.size() + repo.unstagedFiles.size() + repo.untrackedFiles.size());
        for (const auto& f : repo.stagedFiles) allFilesStatusMap_[f.path] = f.indexStatus;
        for (const auto& f : repo.unstagedFiles) allFilesStatusMap_[f.path] = f.workTreeStatus;
        for (const auto& u : repo.untrackedFiles) allFilesStatusMap_[u] = 'U';
    }

    std::optional<size_t> history_keys(UIContext<InputAction>& ctx, RepoComponent& repo,
            const LayoutComponent& layout, afterhours::ui::imm::EntityParent parent) {
        const auto owner = ui::shortcut_owner(ctx, repo);
        if (owner.region != reading::focus::Region::History || owner.text || repo.commitLog.empty() ||
            ui::shortcuts_blocked(layout) || layout.picker.is_open()) return {};
        if (any_modifier_down()) return {};
        const bool up = afterhours::input::is_key_pressed(afterhours::keys::UP);
        const bool down = afterhours::input::is_key_pressed(afterhours::keys::DOWN);
        const bool enter = afterhours::input::is_key_pressed(afterhours::keys::ENTER);
        if (!up && !down && !enter) return {};
        auto focused = afterhours::ui::UICollectionHolder::getEntityForID(ctx.focus_id);
        const auto target = focused.valid() ? ui::focus_target(**focused) : std::nullopt;
        const bool retry = target && target->control == "lazy_load";
        if (retry && enter) return {};
        if (up) (void)ctx.pressed(InputAction::WidgetUp);
        if (down) (void)ctx.pressed(InputAction::WidgetDown);
        if (enter) (void)ctx.pressed(InputAction::WidgetPress);
        if (retry && down) return {};
        const auto hash = target && !target->item.empty() ? target->item : repo.selectedCommitHash();
        const auto row = std::find_if(repo.commitLog.begin(), repo.commitLog.end(), [&](const auto& commit) { return commit.hash == hash; });
        size_t index = row == repo.commitLog.end() ? 0 : static_cast<size_t>(row - repo.commitLog.begin());
        if (retry) index = repo.commitLog.size() - 1;
        else if (row != repo.commitLog.end()) {
            if (up && index > 0) --index;
            if (down && index + 1 < repo.commitLog.size()) ++index;
        }
        auto [entity, listOwner] = afterhours::ui::imm::deref(parent);
        if (entity.has<afterhours::ui::HasScrollView>()) {
            auto& scroll = entity.get<afterhours::ui::HasScrollView>();
            const float height = theme::layout::COMMIT_ROW_HEIGHT * ui::zoom::get();
            const float top = static_cast<float>(index) * height;
            const float viewport = scroll.viewport_or_zero().y;
            float offset = scroll.scroll_offset.y;
            if (top < offset) offset = top;
            else if (top + height > offset + viewport) offset = top + height - viewport;
            offset = std::max(0.f, offset);
            scroll.scroll_offset.y = scroll.scroll_target.y = scroll.last_eased_offset.y = offset;
            scroll.anchor_child = -1;
        }
        select_history(repo, repo.commitLog[index].hash);
        return index;
    }

    void render_commit_row(UIContext<InputAction>& ctx,
                           Entity& parent, int index,
                           const CommitEntry& commit,
                           RepoComponent& repo, bool focus = false, bool acceptClick = true) {
        bool selected = repo.historySelection.hashes.empty() ? commit.hash == repo.selectedCommitHash() : repo.historySelection.hashes.contains(commit.hash);
        constexpr float ROW_H = theme::layout::COMMIT_ROW_HEIGHT;

        int baseId = index * 2 + 10;
        float sidebarW = sidebarPixelWidth_ > 0 ? sidebarPixelWidth_ : 300.0f;

        const auto& rowText = commit_row_text(commit, repo);
        const auto& badges = rowText.badges;

        constexpr float DOT_SIZE = 8.0f;
        constexpr float LINE_W = 1.0f;
        float GRAPH_COL_W = std::min(4.f + static_cast<float>(graph_.columns) * 12.f, std::max(16.f, sidebarW - 160.f));
        float laneWidth = (GRAPH_COL_W - 4.f) / static_cast<float>(graph_.columns);
        const auto& graphRow = graph_.rows.at(commit.hash);
        auto laneX = [&](size_t lane) { return 2.f + (static_cast<float>(lane) + 0.5f) * laneWidth; };
        constexpr afterhours::Color laneColors[] = {{92, 104, 122, 255}, {91, 112, 122, 255}, {125, 116, 98, 255}, {99, 120, 107, 255}, {122, 102, 115, 255}};
        auto laneColor = [&](size_t lane) { return laneColors[lane % 5]; };
        // Small left inset so the graph line/dots/HEAD ring aren't flush against
        // the window edge (#26).
        constexpr float ROW_INSET_L = 4.0f;

        auto row = div(ctx, mk(parent, baseId),
            preset::SelectableRow(selected)
                .with_consumes_directional_input()
                .with_size(ComponentSize{pixels(std::max(0.f, sidebarW - 22.f)), pixels(ROW_H)})
                .with_margin(Margin{.left = pixels(8), .right = pixels(14)})
                .with_rounded_corners(theme::layout::ROUNDED_CORNERS).with_corner_radius(theme::layout::RADIUS_CONTROL)
                .with_border(selected ? theme::SELECTED_ACCENT : afterhours::Color{0, 0, 0, 0}, pixels(1))
                .with_padding(Padding{
                    .top = pixels(0), .right = pixels(8),
                    .bottom = pixels(0), .left = pixels(ROW_INSET_L)})
                .with_gap(pixels(2))
                .with_debug_name("commit_row"));
        ui::bind_focus(row.ent(), repo, reading::focus::Region::History, commit.hash);
        ui::set_tooltip(row.ent(), rowText.tooltip);

        row.ent().addComponentIfMissing<HasClickListener>([](Entity&){});
        if (focus) ctx.set_focus(row.ent().id);
        if (ctx.is_input_allowed(row.ent().id)) {
            if (afterhours::ui::is_mouse_inside(ctx.mouse.pos, ui::visible_rect(row.ent()))) hoveredCommit_ = commit.hash;
            if (ctx.focus_id == row.ent().id) focusedCommit_ = commit.hash;
        }

        constexpr float rowPx = ROW_H;

        auto graphWrap = div(ctx, mk(row.ent(), 1),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(GRAPH_COL_W), pixels(rowPx)})
                .with_roundness(0.0f)
                .with_debug_name("graph_wrap"));

        int edgeId = 10;
        auto vertical = [&](size_t lane, float from, float to) {
            div(ctx, mk(graphWrap.ent(), edgeId++), ComponentConfig{}
                .with_size(ComponentSize{pixels(LINE_W), pixels((to - from) * rowPx)})
                .with_absolute_position(laneX(lane) - LINE_W * 0.5f, from * rowPx)
                .with_custom_background(laneColor(lane)).with_roundness(0.f)
                .with_debug_name("graph_lane:" + std::to_string(lane)));
        };
        if (graphRow.incoming) vertical(graphRow.lane, 0.f, 0.5f);
        for (auto lane : graphRow.continuing) vertical(lane, 0.f, 1.f);
        for (auto lane : graphRow.parents) {
            if (lane != graphRow.lane) {
                div(ctx, mk(graphWrap.ent(), edgeId++), ComponentConfig{}
                    .with_size(ComponentSize{pixels(std::fabs(laneX(lane) - laneX(graphRow.lane))), pixels(LINE_W)})
                    .with_absolute_position(std::min(laneX(lane), laneX(graphRow.lane)), rowPx * 0.5f - LINE_W * 0.5f)
                    .with_custom_background(laneColor(lane)).with_roundness(0.f)
                    .with_debug_name("graph_branch_edge"));
            }
            vertical(lane, 0.5f, 1.f);
        }

        bool isHead = commit.decorations.find("HEAD") != std::string::npos;
        float dotX = laneX(graphRow.lane) - DOT_SIZE * 0.5f;
        float dotY = (rowPx - DOT_SIZE) / 2.0f;
        auto dotCfg = ComponentConfig{}
            .with_size(ComponentSize{pixels(DOT_SIZE), pixels(DOT_SIZE)})
            .with_absolute_position(dotX, dotY)
            .with_rounded_corners(theme::layout::ROUNDED_CORNERS)
            .with_roundness(1.0f)
            .with_render_layer(1)
            .with_debug_name("commit_dot");
        if (selected || isHead) {
            dotCfg = dotCfg.with_custom_background(theme::TEXT_ACCENT);
        } else {
            dotCfg = dotCfg.with_custom_background(theme::SIDEBAR_BG)
                           .with_border(theme::TEXT_SECONDARY, pixels(1.f));
        }
        // The dot is the commit's node in the history graph (the vertical
        // line is its branch lane) — filled accent = HEAD or the selected
        // commit, hollow = any other commit. Nothing on screen said that,
        // so say it on hover.
        auto dot = div(ctx, mk(graphWrap.ent(), 2), dotCfg);
        ui::set_tooltip(dot.ent(), isHead ? "HEAD \xe2\x80\x94 the commit your working tree is on"
                                          : selected ? "Selected commit"
                                                     : "Commit in the history graph");

        auto text = div(ctx, mk(row.ent(), 2), ComponentConfig{}
            .with_size(ComponentSize{expand(), pixels(ROW_H)})
            .with_flex_direction(FlexDirection::Row).with_no_wrap()
            .with_align_items(AlignItems::Center).with_gap(pixels(8))
            .with_padding(Padding{}));
        auto subjectLabel = div(ctx, mk(text.ent(), 0), preset::BodyText(commit.subject)
            .with_size(ComponentSize{expand(), pixels(22)})
            .with_custom_text_color(selected ? theme::TEXT_PRIMARY : theme::TEXT_SECONDARY)
            .with_font_size(pixels(14)).with_text_inset(0.f).with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
            .with_debug_name("commit_subject"));
        ui::set_truncated_tooltip(row.ent(), commit.subject, subjectLabel.ent());
        auto metadata = div(ctx, mk(text.ent(), 1), ComponentConfig{}
            .with_size(ComponentSize{children(), pixels(20)})
            .with_flex_direction(FlexDirection::Row).with_align_items(AlignItems::Center)
            .with_gap(pixels(8)));
        if (!badges.empty()) {
            const auto* badge = &badges.front();
            for (const auto& value : badges)
                if (value.type == commit_log_detail::DecorationType::Head) badge = &value;
            const auto badgeBg = badge->type == commit_log_detail::DecorationType::Head ? theme::BADGE_HEAD_BG
                : badge->type == commit_log_detail::DecorationType::LocalBranch ? theme::BADGE_BRANCH_BG
                : badge->type == commit_log_detail::DecorationType::Tag ? theme::BADGE_TAG_BG
                : theme::BADGE_REMOTE_BG;
            const auto badgeFg = badge->type == commit_log_detail::DecorationType::Head ? theme::DIFF_ADD_TEXT
                : badge->type == commit_log_detail::DecorationType::Tag ? theme::BADGE_TAG_TEXT
                : theme::TEXT_ACCENT;
            div(ctx, mk(metadata.ent(), 2), preset::Badge(badge->label, badgeBg, badgeFg)
                .with_size(ComponentSize{pixels(54), pixels(18)}).with_font_size(pixels(12))
                .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis).with_debug_name("commit_badge"));
        }
        int commentCount = 0;
        if (auto* review = find_singleton<ReviewComponent, ActiveTab>())
            for (const auto& comment : review->comments)
                if (comment.scope == commit.hash && !comment.resolved) ++commentCount;
        if (commentCount > 0)
            div(ctx, mk(metadata.ent(), 3), preset::Badge(std::to_string(commentCount), theme::BUTTON_SECONDARY, theme::STATUS_MODIFIED)
                .with_size(ComponentSize{pixels(22), pixels(18)}).with_font_size(pixels(12))
                .with_debug_name("commit_comment_badge"));
        auto ageLabel = div(ctx, mk(metadata.ent(), 1), preset::MetaText(rowText.age)
            .with_size(ComponentSize{pixels(34), pixels(20)}).with_font("mono", pixels(12))
            .with_alignment(TextAlignment::Right).with_text_inset(0.f).with_debug_name("commit_age"));
        ui::set_tooltip(ageLabel.ent(), commit.authorDate.empty() ? rowText.age : commit.authorDate);

        // Arm drag range-select from the frame-level press state, not from
        // HasClickListener.down: that flag lives for one frame between two
        // systems, and a tight press->move->release sequence (e2e drag_to,
        // fast real drags) can slip past the render that would read it.
        // just_pressed + hit test is visible identically to every system.
        if (acceptClick && ctx.mouse.just_pressed &&
            afterhours::ui::is_mouse_inside(ctx.mouse.pos, ui::visible_rect(row.ent()))) {
            histDragAnchor_ = any_modifier_down() ? std::string{} : commit.hash;
            histDragMoved_ = false;
            if (!any_modifier_down()) {
                if (auto* r = find_singleton<RepoComponent, ActiveTab>()) select_history(*r, commit.hash);
            }
        }
        // Click -> select this commit. Skipped for the pressed row when the
        // press turned into a drag: the click latch can land on the release
        // frame (after the range already formed), and a plain select here
        // would collapse the range back to the single pressed commit.
        if (acceptClick && row.ent().get<HasClickListener>().down &&
            !(histDragMoved_ && commit.hash == histDragAnchor_)) {
            auto* r = find_singleton<RepoComponent, ActiveTab>();
            if (r) {
                select_history(*r, commit.hash);
                navigation::click(*r, reading::review(commit.hash), afterhours::input::is_key_pressed(afterhours::keys::ENTER), reading::ClickRegion::History);
            }
        }
        // Drag range-select: holding the button and moving onto other rows
        // extends the selection from the pressed row (whose hash the plain
        // select above left in HistorySelection::anchor).
        if (!histDragAnchor_.empty()) {
            // press_moved is reset on the release frame, so latch it, and on
            // release also derive it from the press->release distance: a fast
            // drag can land and let go with no held frame ever rendering.
            const float pressDx = ctx.mouse.pos.x - ctx.mouse.press_pos.x;
            const float pressDy = ctx.mouse.pos.y - ctx.mouse.press_pos.y;
            const float threshold = afterhours::ui::MousePointerState::press_drag_threshold_px;
            if (ctx.mouse.press_moved ||
                (pressDx * pressDx + pressDy * pressDy) > threshold * threshold)
                histDragMoved_ = true;
            const bool overRow = histDragMoved_ && commit.hash != histDragAnchor_ &&
                afterhours::ui::is_mouse_inside(ctx.mouse.pos, ui::visible_rect(row.ent()));
            if (overRow && (ctx.mouse.left_down || ctx.mouse.just_released)) {
                std::vector<std::string> visible;
                for (const CommitEntry& entry : repo.commitLog) visible.push_back(entry.hash);
                repo.historySelection.select(visible, commit.hash, true, false);
            }
            if (!ctx.mouse.left_down) { histDragAnchor_.clear(); histDragMoved_ = false; }
        }
        if (ctx.is_right_click(row.ent().id)) {
            ui::remember_focus_origin(ctx, row.ent());
            if (!repo.historySelection.hashes.contains(commit.hash)) {
                repo.historySelection.hashes = {commit.hash};
                repo.historySelection.anchor = commit.hash;
            }
            const auto selectedCommits = repo.historySelection.visible(repo.commitLog);
            std::string hashes, subjects;
            for (const auto& selectedCommit : selectedCommits) {
                if (!hashes.empty()) { hashes += "\n"; subjects += "\n"; }
                hashes += selectedCommit.hash;
                subjects += selectedCommit.subject;
            }
            std::vector<ui::ContextMenuItem> items;
            items.push_back(ui::ContextMenuItem::item("Copy selected hashes", [hashes] { afterhours::clipboard::set_text(hashes); }));
            items.push_back(ui::ContextMenuItem::item("Copy selected subjects", [subjects] { afterhours::clipboard::set_text(subjects); }));
            const bool contiguous = repo.historySelection.contiguous(repo.commitLog);
            const auto target = selectedCommits.empty() ? "" : selectedCommits.front().hash;
            const auto parents = selectedCommits.empty() ? "" : selectedCommits.back().parentHashes;
            const auto base = parents.empty() ? "empty" : parents.substr(0, parents.find(' '));
            items.push_back(ui::ContextMenuItem::item("Review selected range", [base, target] {
                if (auto* active = find_singleton<RepoComponent, ActiveTab>())
                    navigation::open(*active, reading::review("compare:" + base + ":" + target), true, reading::OpenMode::Keep);
            }, contiguous));
            items.push_back(ui::ContextMenuItem::item("Open commit", [hash = commit.hash] {
                if (auto* active = find_singleton<RepoComponent, ActiveTab>())
                    navigation::open(*active, reading::review(hash), {}, reading::OpenMode::Keep);
            }));
            ui::show_context_menu(ctx.mouse.pos.x, ctx.mouse.pos.y, std::move(items));
        }
    }

    static void change_history_scope(RepoComponent& repo, git::HistoryScope scope) {
        if (scope == repo.historyScope) return;
        repo.historyPositions[repo.historyScope.key()].selection = repo.historySelection;
        if (repo.historyPositions.size() > 64) repo.historyPositions.erase(repo.historyPositions.begin());
        repo.historyScope = std::move(scope);
        repo.historySelection = repo.historyPositions[repo.historyScope.key()].selection;
        repo.historyScopeRequested = true;
        repo.historyRestorePosition = true;
    }

    // Modifier state by name — never raw key codes at call sites.
    static bool shift_down() {
        return afterhours::input::is_key_down(afterhours::keys::LEFT_SHIFT) ||
               afterhours::input::is_key_down(afterhours::keys::RIGHT_SHIFT);
    }
    static bool cmd_down() {
        return afterhours::input::is_key_down(afterhours::keys::LEFT_SUPER) ||
               afterhours::input::is_key_down(afterhours::keys::RIGHT_SUPER) ||
               afterhours::input::is_key_down(afterhours::keys::LEFT_CONTROL) ||
               afterhours::input::is_key_down(afterhours::keys::RIGHT_CONTROL);
    }
    static bool alt_down() {
        return afterhours::input::is_key_down(afterhours::keys::LEFT_ALT) ||
               afterhours::input::is_key_down(afterhours::keys::RIGHT_ALT);
    }
    static bool any_modifier_down() { return shift_down() || cmd_down() || alt_down(); }

    static void select_history(RepoComponent& repo, const std::string& hash) {
        std::vector<std::string> visible;
        for (const auto& entry : repo.commitLog) visible.push_back(entry.hash);
        repo.historySelection.select(visible, hash, shift_down(), cmd_down());
    }

    // ---- Unstaged Changes Dialog (T030) ----
    // Custom modal dialog showing staged/unstaged file lists with three
    // action buttons and a "Remember this choice" checkbox.
    void render_unstaged_dialog(UIContext<InputAction>& ctx,
                                Entity& uiRoot,
                                RepoComponent& repo,
                                CommitEditorComponent& editor) {
        using namespace afterhours;

        constexpr int DIALOG_ID = 8000;
        constexpr int CONTENT_LAYER = 1001;

        // Build summary text
        int stagedCount = static_cast<int>(repo.stagedFiles.size());
        int unstagedCount = static_cast<int>(repo.unstagedFiles.size() +
                                              repo.untrackedFiles.size());
        std::string summary = "You have " + std::to_string(stagedCount) +
            " staged and " + std::to_string(unstagedCount) +
            " unstaged changes.\nHow would you like to proceed?";

        // Cancel + "Commit Staged Only" + "Stage All & Commit" need ~535px of
        // row; 480 left the last one outside the dialog. Sized and centred in
        // the zoomed viewport (see "Modal centering" in docs/afterhours-gaps.md).
        const float zoom = ::ui::zoom::get();
        const float width = std::min(640.f, ctx.screen_width / zoom - 32.f);
        const float height = std::min(560.f, ctx.screen_height / zoom - 32.f);
        // The dialog is open exactly in AwaitingPolicy; modal_impl
        // mutates this local (Esc/pending close), which dismisses back
        // to Editing — no stored dialog bool remains.
        bool dialogOpen = editor.flow == CommitFlow::AwaitingPolicy;
        auto modalResult = afterhours::modal::detail::modal_impl(
            ctx, mk(uiRoot, DIALOG_ID), dialogOpen,
            ModalConfig{}
                .with_size(pixels(width), pixels(height))
                .with_title("Unstaged Changes")
                .with_show_close_button(false));
        if (!dialogOpen) editor.dismiss_policy();

        if (!modalResult) return;

        auto& modalEnt = modalResult.ent();

        // -- Summary text --
        div(ctx, mk(modalEnt, 1),
            preset::DialogMessage(summary)
                .with_render_layer(CONTENT_LAYER)
                .with_debug_name("unstaged_summary"));

        // -- Staged files section --
        if (!repo.stagedFiles.empty()) {
            div(ctx, mk(modalEnt, 10),
                ComponentConfig{}
                    .with_label("Staged files:")
                    .with_size(ComponentSize{percent(1.0f), pixels(16)})
                    .with_padding(Padding{
                        .top = pixels(4), .right = pixels(16),
                        .bottom = pixels(2), .left = pixels(16)})
                    .with_custom_text_color(theme::TEXT_SECONDARY)
                    .with_alignment(TextAlignment::Left)
                    .with_render_layer(CONTENT_LAYER)
                    .with_debug_name("staged_label"));

            // Staged file list container with green left border
            auto stagedList = div(ctx, mk(modalEnt, 11),
                ComponentConfig{}
                    .with_size(ComponentSize{percent(1.0f), children()})
                    .with_flex_direction(FlexDirection::Column)
                    .with_padding(Padding{
                        .top = pixels(2), .right = pixels(16),
                        .bottom = pixels(4), .left = pixels(19)})
                    .with_render_layer(CONTENT_LAYER)
                    .with_debug_name("staged_list"));

            int maxShow = std::min(static_cast<int>(repo.stagedFiles.size()), 5);
            for (int i = 0; i < maxShow; ++i) {
                auto& f = repo.stagedFiles[i];
                char sc = f.indexStatus;
                if (sc == ' ' || sc == '\0') sc = 'A';
                std::string label = std::string(1, sc) + "  " + f.path;

                div(ctx, mk(stagedList.ent(), 100 + i),
                    ComponentConfig{}
                        .with_label(label)
                        .with_size(ComponentSize{percent(1.0f), pixels(20)})
                        .with_custom_text_color(theme::TEXT_PRIMARY)
                        .with_alignment(TextAlignment::Left)
                        .with_render_layer(CONTENT_LAYER)
                        .with_debug_name("staged_file"));
            }
            if (static_cast<int>(repo.stagedFiles.size()) > maxShow) {
                std::string moreLabel = "... and " +
                    std::to_string(repo.stagedFiles.size() - maxShow) + " more";
                div(ctx, mk(stagedList.ent(), 199),
                    ComponentConfig{}
                        .with_label(moreLabel)
                        .with_size(ComponentSize{percent(1.0f), pixels(18)})
                        .with_custom_text_color(theme::TEXT_SECONDARY)
                        .with_alignment(TextAlignment::Left)
                        .with_render_layer(CONTENT_LAYER)
                        .with_debug_name("staged_more"));
            }
        }

        // -- Unstaged files section --
        if (!repo.unstagedFiles.empty() || !repo.untrackedFiles.empty()) {
            div(ctx, mk(modalEnt, 20),
                ComponentConfig{}
                    .with_label("Unstaged files:")
                    .with_size(ComponentSize{percent(1.0f), pixels(16)})
                    .with_padding(Padding{
                        .top = pixels(8), .right = pixels(16),
                        .bottom = pixels(2), .left = pixels(16)})
                    .with_custom_text_color(theme::TEXT_SECONDARY)
                    .with_alignment(TextAlignment::Left)
                    .with_render_layer(CONTENT_LAYER)
                    .with_debug_name("unstaged_label"));

            auto unstagedList = div(ctx, mk(modalEnt, 21),
                ComponentConfig{}
                    .with_size(ComponentSize{percent(1.0f), children()})
                    .with_flex_direction(FlexDirection::Column)
                    .with_padding(Padding{
                        .top = pixels(2), .right = pixels(16),
                        .bottom = pixels(4), .left = pixels(19)})
                    .with_render_layer(CONTENT_LAYER)
                    .with_debug_name("unstaged_list"));

            // Combine unstaged + untracked for display
            std::vector<std::pair<char, std::string>> allUnstaged;
            for (auto& f : repo.unstagedFiles) {
                char wc = f.workTreeStatus;
                if (wc == ' ' || wc == '\0') wc = 'M';
                allUnstaged.push_back({wc, f.path});
            }
            for (auto& path : repo.untrackedFiles) {
                allUnstaged.push_back({'?', path});
            }

            int maxShow = std::min(static_cast<int>(allUnstaged.size()), 5);
            for (int i = 0; i < maxShow; ++i) {
                std::string label = std::string(1, allUnstaged[i].first) +
                    "  " + allUnstaged[i].second;

                div(ctx, mk(unstagedList.ent(), 200 + i),
                    ComponentConfig{}
                        .with_label(label)
                        .with_size(ComponentSize{percent(1.0f), pixels(20)})
                        .with_custom_text_color(theme::TEXT_PRIMARY)
                        .with_alignment(TextAlignment::Left)
                        .with_render_layer(CONTENT_LAYER)
                        .with_debug_name("unstaged_file"));
            }
            if (static_cast<int>(allUnstaged.size()) > maxShow) {
                std::string moreLabel = "... and " +
                    std::to_string(allUnstaged.size() - maxShow) + " more";
                div(ctx, mk(unstagedList.ent(), 299),
                    ComponentConfig{}
                        .with_label(moreLabel)
                        .with_size(ComponentSize{percent(1.0f), pixels(18)})
                        .with_custom_text_color(theme::TEXT_SECONDARY)
                        .with_alignment(TextAlignment::Left)
                        .with_render_layer(CONTENT_LAYER)
                        .with_debug_name("unstaged_more"));
            }
        }

        // -- "Remember this choice" checkbox --
        auto checkboxRow = div(ctx, mk(modalEnt, 30),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(28)})
                .with_flex_direction(FlexDirection::Row)
                .with_align_items(AlignItems::Center)
                .with_padding(Padding{
                    .top = pixels(8), .right = pixels(16),
                    .bottom = pixels(4), .left = pixels(16)})
                .with_render_layer(CONTENT_LAYER)
                .with_debug_name("remember_row"));

        checkbox(ctx, mk(checkboxRow.ent(), 1),
            editor.rememberChoice,
            ComponentConfig{}
                .with_label("Remember this choice")
                .with_size(ComponentSize{children(), pixels(20)})
                .with_custom_text_color(theme::TEXT_SECONDARY)
                .with_render_layer(CONTENT_LAYER)
                .with_debug_name("remember_checkbox"));

        // -- Button row: [Cancel] [Commit Staged Only] [Stage All & Commit] --
        auto btnRow = div(ctx, mk(modalEnt, 40),
            preset::DialogButtonRow()
                .with_render_layer(CONTENT_LAYER)
                .with_debug_name("dialog_buttons"));

        // Cancel button
        auto cancelBtn = button(ctx, mk(btnRow.ent(), 1),
            preset::Button("Cancel")
                .with_margin(Margin{
                    .top = {}, .bottom = {},
                    .left = {}, .right = pixels(8)})
                .with_custom_background(theme::BUTTON_SECONDARY)
                .with_custom_text_color(theme::TEXT_PRIMARY)
                .with_render_layer(CONTENT_LAYER)
                .with_debug_name("cancel_btn"));

        if (cancelBtn) {
            editor.dismiss_policy();
        }

        // "Commit Staged Only" button (primary blue)
        auto stagedOnlyBtn = button(ctx, mk(btnRow.ent(), 2),
            preset::Button("Commit Staged Only")
                .with_margin(Margin{
                    .top = {}, .bottom = {},
                    .left = {}, .right = pixels(8)})
                .with_render_layer(CONTENT_LAYER)
                .with_debug_name("staged_only_btn"));

        if (stagedOnlyBtn) {
            if (editor.rememberChoice) {
                editor.unstagedPolicy =
                    CommitEditorComponent::UnstagedPolicy::CommitStagedOnly;
                commit_workflow::save_policy(editor.unstagedPolicy);
            }
            commit_workflow::begin_commit(repo, editor, false);
        }

        // "Stage All & Commit" button (green)
        auto stageAllBtn = button(ctx, mk(btnRow.ent(), 3),
            preset::Button("Stage All & Commit")
                .with_custom_background(theme::STATUS_ADDED)
                .with_render_layer(CONTENT_LAYER)
                .with_debug_name("stage_all_btn"));

        if (stageAllBtn) {
            if (editor.rememberChoice) {
                editor.unstagedPolicy =
                    CommitEditorComponent::UnstagedPolicy::StageAll;
                commit_workflow::save_policy(editor.unstagedPolicy);
            }
            commit_workflow::begin_commit(repo, editor, true);
        }
    }
};

} // namespace ecs
