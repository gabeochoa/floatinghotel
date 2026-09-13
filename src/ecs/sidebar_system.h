#pragma once

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
#include "network_ops_system.h"
#include "ui_imports.h"
#include "../ui/context_menu.h"
#include "../ui/focus.h"
#include "../ui/file_history.h"
#include "../ui/review_snapshot.h"
#include "../ui/diff_metrics.h"
#include "../ui/virtual_list.h"
#include "../ui/text_area.h"
#include "../ui/zoom.h"
#include "../ui/file_tree_style.h"
#include "../ui/chrome_icons.h"

#include "../../vendor/afterhours/src/plugins/clipboard.h"
#include "../../vendor/afterhours/src/plugins/modal.h"
#include "../../vendor/afterhours/src/plugins/ui/text_input/text_input.h"

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

// Execute the commit, optionally staging all first
// Returns true on success
inline bool execute_commit(RepoComponent& repo,
                           CommitEditorComponent& editor,
                           bool stageAllFirst) {
    if (stageAllFirst) {
        auto stageResult = git::stage_all(repo.repoPath);
        if (!stageResult.success()) {
            toast_on_git_failure(stageResult, "Stage All");
            return false;
        }
    }

    std::string message = build_message(editor.subject, editor.body);
    if (message.empty()) {
        message = "Update";
    }

    auto result = git::git_commit(repo.repoPath, message);
    if (result.success()) {
        editor.subject.clear();
        editor.body.clear();
        editor.isVisible = false;
        repo.refreshRequested = true;
        return true;
    }
    toast_on_git_failure(result, "Commit");
    return false;
}

// Handle the commit request — checks unstaged policy and either
// commits directly or opens the dialog
inline void handle_commit_request(RepoComponent& repo,
                                   CommitEditorComponent& editor) {
    bool hasStaged = !repo.stagedFiles.empty();
    bool hasUnstaged = !repo.unstagedFiles.empty() ||
                       !repo.untrackedFiles.empty();

    if (!hasStaged) return; // Nothing to commit

    if (!hasUnstaged) {
        // No unstaged changes — commit directly
        execute_commit(repo, editor, false);
        editor.commitRequested = false;
        return;
    }

    // Both staged and unstaged exist — check policy
    switch (editor.unstagedPolicy) {
        case CommitEditorComponent::UnstagedPolicy::StageAll:
            execute_commit(repo, editor, true);
            editor.commitRequested = false;
            break;
        case CommitEditorComponent::UnstagedPolicy::CommitStagedOnly:
            execute_commit(repo, editor, false);
            editor.commitRequested = false;
            break;
        case CommitEditorComponent::UnstagedPolicy::Ask:
        default:
            // Open the dialog
            editor.showUnstagedDialog = true;
            editor.rememberChoice = false;
            editor.commitRequested = false;
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

        if (!layout.sidebarVisible || layout.sidebar.width <= 0.f) return;

        auto* repoPtr = find_singleton<RepoComponent, ActiveTab>();

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
                .with_border_right(afterhours::Color{58, 58, 58, 80})
                .with_flex_direction(FlexDirection::Column)
                .with_overflow(Overflow::Hidden, Axis::Y)
                .with_roundness(0.0f)
                .with_debug_name("sidebar_bg"));
        if (repoPtr) ui::bind_focus(sidebarRoot.ent(), *repoPtr, reading::focus::Region::Tree);

        float sidebarW = layout.sidebar.width;
        sidebarPixelWidth_ = sidebarW;  // Set early for all child rendering

        float sh_for_tab = static_cast<float>(afterhours::graphics::get_screen_height());
        const float zoom = ui::zoom::get();
        const bool filesNavigation = layout.sidebarNavigation == LayoutComponent::SidebarNavigation::Files;
        float filesH = 0.f;
        float commitsH = 0.f;
        float splitAvailable = std::max(0.f, layout.sidebar.height - LayoutComponent::kCommitSplitterHeight);

        render_repo_header(ctx, sidebarRoot.ent(), repoPtr);
        auto navigation = div(ctx, mk(sidebarRoot.ent(), 2080), ComponentConfig{}
            .with_size(ComponentSize{percent(1.f), pixels(42)})
            .with_padding(Padding{.top = pixels(4), .right = pixels(12), .bottom = pixels(6), .left = pixels(12)})
            .with_gap(pixels(4)).with_flex_direction(FlexDirection::Row)
            .with_debug_name("sidebar_navigation"));
        auto segments = div(ctx, mk(navigation.ent(), 10), ComponentConfig{}
            .with_size(ComponentSize{percent(1.f), pixels(32)})
            .with_flex_direction(FlexDirection::Row).with_no_wrap().with_gap(pixels(4))
            .with_padding(Padding{.top = pixels(3), .right = pixels(3), .bottom = pixels(3), .left = pixels(3)})
            .with_border(theme::BORDER, pixels(1)).with_rounded_corners(theme::layout::ROUNDED_CORNERS)
            .with_corner_radius(7.f).with_debug_name("sidebar_navigation_segments"));
        for (int index = 0; index < 2; ++index) {
            const bool selected = filesNavigation == (index == 1);
            auto segment = button(ctx, mk(segments.ent(), index), preset::Button("")
                    .with_size(ComponentSize{expand(), pixels(26)})
                    .with_padding(Padding{.left = pixels(0), .right = pixels(0)})
                    .with_flex_direction(FlexDirection::Row).with_justify_content(JustifyContent::Center)
                    .with_align_items(AlignItems::Center).with_gap(pixels(6)).with_no_wrap()
                    .with_custom_background(selected ? ui::segment_selected_color() : theme::SIDEBAR_BG)
                    .with_custom_text_color(selected ? theme::TEXT_PRIMARY : theme::TEXT_SECONDARY)
                    .with_debug_name(index == 0 ? "sidebar_review" : "sidebar_working_files"));
            ui::chrome_icon(ctx, mk(segment.ent(), 0), index == 0 ? ui::ChromeIcon::Commit : ui::ChromeIcon::Files,
                selected ? theme::TEXT_PRIMARY : theme::TEXT_SECONDARY, "navigation_icon");
            div(ctx, mk(segment.ent(), 1), ComponentConfig{}.with_label(index == 0 ? "Review" : "Files")
                .with_size(ComponentSize{children(), pixels(26)}).with_font_size(pixels(14))
                .with_custom_text_color(selected ? theme::TEXT_PRIMARY : theme::TEXT_SECONDARY));
            if (segment) {
                layout.sidebarNavigation = index == 0 ? LayoutComponent::SidebarNavigation::Review : LayoutComponent::SidebarNavigation::Files;
                if (index == 0 && repoPtr) navigation::activate(*repoPtr, reading::Slot::Review);
            }
        }
        if (!filesNavigation && repoPtr) {
            auto views = div(ctx, mk(sidebarRoot.ent(), 2081), ComponentConfig{}
                .with_size(ComponentSize{percent(1.f), pixels(36)})
                .with_padding(Padding{.left = pixels(12), .right = pixels(12), .bottom = pixels(4)})
                .with_flex_direction(FlexDirection::Row).with_gap(pixels(4)).with_no_wrap()
                .with_debug_name("working_review_views"));
            for (bool staged : {false, true}) {
                const auto count = staged ? repoPtr->stagedFiles.size() : repoPtr->unstagedFiles.size() + repoPtr->untrackedFiles.size();
                const auto label = std::string(staged ? "Staged" : "Unstaged") + " (" + std::to_string(count) + ")";
                const bool active = std::holds_alternative<reading::WorkingChanges>(repoPtr->workspace().review().destination) &&
                    repoPtr->selectedFileStaged() == staged;
                if (button(ctx, mk(views.ent(), staged ? 1 : 0), preset::Button(label)
                        .with_size(ComponentSize{expand(), pixels(30)}).with_font_size(pixels(13))
                        .with_custom_background(active ? ui::segment_selected_color() : theme::SIDEBAR_BG)
                        .with_debug_name(staged ? "review_staged_changes" : "review_unstaged_changes")))
                    navigation::open(*repoPtr, reading::review(staged ? "index" : "wt"), true);
            }
        }
        const float repoHeaderH = filesNavigation ? 104.f : 140.f;
        if (filesNavigation) {
            auto* editor = find_singleton<CommitEditorComponent, ActiveTab>();
            const bool showGit = repoPtr && !repoPtr->repoPath.empty() && !repoPtr->reviewWorkspace;
            const bool showCommit = showGit && !treeClean && editor &&
                layout.sidebarMode == LayoutComponent::SidebarMode::Changes;
            const bool showProgress = repoPtr && layout.sidebarMode == LayoutComponent::SidebarMode::Changes;
            const float viewportH = std::max(0.f, layout.sidebarFiles.height - repoHeaderH);
            const float minimumControlsH = resolve_to_pixels(h720(
                (repoPtr ? 28.f : 0.f) + (showGit ? 34.f : 0.f) +
                (showCommit ? 54.f + COMMIT_INPUT_H_720 : 0.f) + 28.f), sh_for_tab) / zoom +
                (showProgress ? 60.f : 0.f);
            const float bodyH = std::max(viewportH, minimumControlsH + 80.f);
            auto controlsScroll = div(ctx, mk(sidebarRoot.ent(), 2090), preset::ScrollPanel()
                .with_size(ComponentSize{pixels(sidebarW), pixels(viewportH)})
                .with_debug_name("files_controls_scroll"));
            auto controlsBody = div(ctx, mk(controlsScroll.ent(), 0), ComponentConfig{}
                .with_size(ComponentSize{pixels(sidebarW), pixels(bodyH)})
                .with_min_height(pixels(bodyH))
                .with_flex_direction(FlexDirection::Column).with_no_wrap()
                .with_debug_name("files_controls_body"));
            float controlsH = 0.f;
            if (repoPtr) {
                if (button(ctx, mk(controlsBody.ent(), 2099), preset::Button(repoPtr->reviewWorkspace ? "Git controls hidden · Show" : "Hide Git controls")
                        .with_size(ComponentSize{percent(1.f), h720(28)})
                        .with_font_size(FontSize::Small).with_debug_name("review_workspace_toggle"))) {
                    repoPtr->reviewWorkspace = !repoPtr->reviewWorkspace;
                }
                controlsH += resolve_to_pixels(h720(28.f), sh_for_tab) / zoom;
            }

            // === Sync row (Push / Pull / Stash), grouped under the repo header ===
            float syncRowH = 0.0f;
            if (repoPtr && !repoPtr->repoPath.empty() && !repoPtr->reviewWorkspace) {
                render_sync_row(ctx, controlsBody.ent(), repoPtr);
                syncRowH = resolve_to_pixels(h720(34.0f), sh_for_tab) / zoom;
            }

            // === Commit area (always-visible input + button, VS Code style) ===
            // hint(16) + input + button(24) + gaps(6) + padding(6) + slack, tracked
            // against the actual multi-line input height.
            const float COMMIT_AREA_H_720 = 54.0f + COMMIT_INPUT_H_720;
            float commitAreaH = 0.0f;
            // Hide the commit input + button when there is nothing to commit (#24).
            if (layout.sidebarMode == LayoutComponent::SidebarMode::Changes && repoPtr &&
                !treeClean && !repoPtr->reviewWorkspace) {
                if (editor) {
                    render_commit_area(ctx, controlsBody.ent(), *repoPtr, *editor);
                    commitAreaH = resolve_to_pixels(h720(COMMIT_AREA_H_720), sh_for_tab) / zoom;
                }
            }

            render_sidebar_mode_tabs(ctx, controlsBody.ent(), layout);
            float tabH = resolve_to_pixels(h720(28.0f), sh_for_tab) / zoom;

            // === Review-progress strip ("In the ballroom") ===
            float progressH = 0.0f;
            if (layout.sidebarMode == LayoutComponent::SidebarMode::Changes && repoPtr) {
                auto* rv = find_singleton<ReviewComponent, ActiveTab>();
                render_review_progress(ctx, controlsBody.ent(), *repoPtr, rv);
                progressH = 60.f;
            }

            // === Changed Files / Refs section (flow child of sidebar, NOT absolute) ===
            filesH = bodyH - tabH - commitAreaH - progressH -
                           controlsH - syncRowH;
            if (filesH < 20.0f) filesH = 20.0f;
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
                if (!repoPtr->isRefreshing && !repoPtr->refreshRequested && file_tree::reveal_navigation(state, context,
                        repoPtr->workspace().generation(), file_tree::destination_path(repoPtr->workspace(),
                            allFilesMode_ || scope == "wt" ? file_tree::Scope::Working : file_tree::Scope::Index),
                        allFilesMode_ ? repoPtr->allFilePaths : treePaths_, collapsed)) update_file_tree(ctx, fileList, *repoPtr, layout, context);
                if (auto move = ui::tree_keys(ctx, *repoPtr, layout, state, context, treeRows_, collapsed)) {
                    if (move->toggle) {
                        file_tree::toggle_directory(treeRows_, *move->toggle, collapsed);
                        update_file_tree(ctx, fileList, *repoPtr, layout, context);
                    }
                    if (move->open) {
                        reading::Location destination = allFilesMode_ ? reading::Location{reading::source(move->path)} :
                            reading::Location{reading::review(scope, move->path)};
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
                ? ui::virtual_list(
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

            commitsH = layout.sidebarLog.height;
        } else {
            splitAvailable = std::max(0.f, splitAvailable - repoHeaderH);
            commitsH = splitAvailable * layout.commitLogRatio;
            render_commit_files(ctx, sidebarRoot.ent(), repoPtr, splitAvailable - commitsH);
        }
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
                filesNavigation ? std::clamp(1.f - repoHeaderH / splitAvailable, 0.2f, 0.8f) : 0.8f);
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
            div(ctx, mk(heading.ent(), 0), preset::BodyText("COMMIT HISTORY")
                .with_size(ComponentSize{expand(), pixels(32)}).with_font("ui-bold", pixels(11))
                .with_custom_text_color(theme::TEXT_TERTIARY));
            div(ctx, mk(heading.ent(), 1), preset::BodyText(branch)
                .with_size(ComponentSize{pixels(72), pixels(32)}).with_font_size(pixels(11))
                .with_custom_text_color(theme::TEXT_SECONDARY).with_alignment(TextAlignment::Right)
                .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis).with_debug_name("history_branch"));
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
        const size_t logRows = repoPtr ? repoPtr->commitLog.size() : 0;
        const bool windowedLog = repoPtr && logRows > 0;
        const bool logHasMore = windowedLog && repoPtr->commitLogHasMore;
        constexpr float commitRowPx = theme::layout::COMMIT_ROW_HEIGHT;
        constexpr float lazyRowPx = 24.f;
        if (repoPtr) {
            auto key = repoPtr->repoPath + ":" + std::to_string(repoPtr->dataGeneration) + ":" + std::to_string(repoPtr->commitLog.size());
            if (!repoPtr->commitLog.empty()) key += repoPtr->commitLog.front().hash;
            if (key != graphKey_) { graphKey_ = key; graph_ = commit_graph::build(repoPtr->commitLog); }
        }
        const auto logList = mk(logBg.ent(), 2320);
        auto focusedHistory = afterhours::ui::UICollectionHolder::getEntityForID(ctx.focus_id);
        const auto historyTarget = focusedHistory.valid() ? ui::focus_target(**focusedHistory) : std::nullopt;
        const bool retryFinished = repoPtr && historyTarget && historyTarget->repository == repoPtr->repoPath &&
            historyTarget->control == "lazy_load" && !repoPtr->commitLogPage.future.valid() &&
            !repoPtr->commitLogPage.requested && repoPtr->commitLogPage.error.empty();
        if (retryFinished) {
            auto [listEntity, listOwner] = afterhours::ui::imm::deref(logList);
            ctx.set_focus(listEntity.id);
        }
        const auto historyMove = repoPtr ? history_keys(ctx, *repoPtr, layout, logList) : std::nullopt;
        auto logScroll = windowedLog
            ? ui::virtual_list(
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
            ui::bind_focus(logScroll.ent(), *repoPtr, reading::focus::Region::History);
            if (!windowedLog) render_commit_log_entries(ctx, logScroll.ent(), *repoPtr);
            if (historyMove) {
                navigation::preview(*repoPtr, reading::review(repoPtr->commitLog[*historyMove].hash));
                if (afterhours::input::is_key_pressed(257)) navigation::keep(*repoPtr, repoPtr->workspace().active_id());
            }
        } else {
            render_no_repo(ctx, logScroll.ent(), 0, "no_repo_log");
        }


        // === Commit workflow + Unstaged Changes Dialog (T030) ===
        if (repoPtr && repoPtr->reviewWorkspace) {
            if (auto* editor = find_singleton<CommitEditorComponent, ActiveTab>()) {
                editor->commitRequested = false;
                editor->showUnstagedDialog = false;
            }
            if (auto* branchDialog = find_singleton<BranchDialogState, ActiveTab>()) {
                branchDialog->showNewBranchDialog = false;
                branchDialog->showDeleteBranchDialog = false;
                branchDialog->showForceDeleteDialog = false;
            }
        }
        if (repoPtr && !repoPtr->reviewWorkspace) {
            auto& repo = *repoPtr;

            auto* editor = find_singleton<CommitEditorComponent, ActiveTab>();
            if (editor) {

                // Process commit request
                if (editor->commitRequested) {
                    commit_workflow::handle_commit_request(repo, *editor);
                }

                // Render unstaged changes dialog
                render_unstaged_dialog(ctx, uiRoot, repo, *editor);
            }

            // === Branch dialogs (T031) ===
            auto* branchDialog = find_singleton<BranchDialogState, ActiveTab>();
            if (branchDialog) {
                render_new_branch_dialog(ctx, uiRoot, repo, *branchDialog);
                render_delete_branch_dialog(ctx, uiRoot, repo, *branchDialog);
                render_force_delete_dialog(ctx, uiRoot, repo, *branchDialog);
            }
        }
    }

private:
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
            else empty = repo->comparisonError.empty() ? "Loading comparison files..." : "Unable to load comparison files";
        } else if (repo && !repo->selectedCommitHash().empty()) {
            scope = commit_review_scope(*repo);
            const bool matching = cache && cache->cachedRepoPath == repo->repoPath &&
                cache->cachedCommitHash == repo->selectedCommitHash() &&
                cache->cachedParentHash == selected_commit_parent(*repo) &&
                cache->cachedContext == repo->diffContext && cache->cachedIgnoreWhitespace == repo->ignoreWhitespace;
            if (matching && !cache->patchFuture.valid()) {
                if (cache->commitDetailError.empty()) files = &cache->commitDetailDiff;
                else empty = "Unable to load commit files";
            } else empty = "Loading commit files...";
        } else if (repo && repo->hasLoadedOnce) {
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
        div(ctx, mk(heading.ent(), 0), preset::BodyText("CHANGED FILES" +
            (files ? "  " + std::to_string(files->size()) : ""))
            .with_size(ComponentSize{expand(), pixels(32)}).with_font("ui-bold", pixels(11))
            .with_custom_text_color(theme::TEXT_TERTIARY));
        if (files && review) {
            const auto* origin = repo && source_tab_active(*repo) ? repo->workspace().retained_review() : nullptr;
            const auto progress = origin && origin->files ? review_progress(*review, scope, *origin->files) :
                review_progress(*review, scope, *files);
            div(ctx, mk(heading.ent(), 1), preset::BodyText(std::to_string(progress.reviewed) + " / " + std::to_string(progress.total))
                .with_size(ComponentSize{pixels(64), pixels(32)}).with_font("mono", pixels(11))
                .with_alignment(TextAlignment::Right).with_custom_text_color(theme::TEXT_SECONDARY)
                .with_debug_name("tree_review_progress"));
        }
        if (height <= 32.f) return;
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
        ui::virtual_list(ctx, fileList, commitTreeRows_.size(), 28.f,
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
                ui::set_tooltip(row.ent(), node.path);
                div(ctx, mk(row.ent(), 3), ComponentConfig{}.with_label(ui::file_tree_style::type_marker(node.path))
                    .with_size(ComponentSize{pixels(24), pixels(28)}).with_font("mono", pixels(11))
                    .with_custom_text_color(theme::TEXT_ACCENT).with_debug_name("tree_file_type"));
                div(ctx, mk(row.ent(), 0), ComponentConfig{}.with_label(sidebar_detail::basename_from_path(node.path))
                        .with_size(ComponentSize{expand(), pixels(28)}).with_transparent_bg()
                        .with_custom_text_color(theme::TEXT_PRIMARY).with_alignment(TextAlignment::Left)
                        .with_padding(Padding{.left = pixels(0)}).with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
                        .with_font_size(pixels(13)).with_debug_name("jump_to_diff:" + node.path));
                if (row) selectedPath = file.filePath;
                if (ctx.is_right_click(row.ent().id)) {
                    ui::remember_focus_origin(ctx, row.ent());
                    open_review_file_menu(ctx, *repo, file);
                }
                if (review) {
                    bool reviewed = file_reviewed(*review, scope, file);
                    if (source_tab_active(*repo)) {
                        const auto* origin = repo->workspace().retained_review();
                        if (origin && origin->files)
                            for (const auto& summary : *origin->files)
                                if (summary.path == file.filePath) { reviewed = file_reviewed(*review, scope, summary); break; }
                    }
                    bool changed = false;
                    if (scope == "wt") {
                        const auto seen = review->seenSig.find(file.filePath);
                        changed = seen != review->seenSig.end() && ui::diff_metrics().signature(file) != seen->second;
                    }
                    ui::file_tree_style::review_indicator(ctx, row.ent(), reviewed,
                        unresolved_file_count(*review, scope, file.filePath, file.oldPath), changed);
                }
                if (file.additions > 0) div(ctx, mk(row.ent(), 1), ComponentConfig{}
                    .with_label("+" + std::to_string(file.additions))
                    .with_size(ComponentSize{pixels(32), pixels(28)}).with_font("mono", pixels(11))
                    .with_custom_text_color(theme::DIFF_ADD_TEXT).with_alignment(TextAlignment::Right)
                    .with_debug_name("tree_additions"));
                if (file.deletions > 0) div(ctx, mk(row.ent(), 2), ComponentConfig{}
                    .with_label("-" + std::to_string(file.deletions))
                    .with_size(ComponentSize{pixels(32), pixels(28)}).with_font("mono", pixels(11))
                    .with_custom_text_color(theme::DIFF_DEL_TEXT).with_alignment(TextAlignment::Right)
                    .with_debug_name("tree_deletions"));
            }, config);
        if (move && move->open) {
            if (move->keep) navigation::click(*repo, reading::review(scope, move->path), true);
            else navigation::preview(*repo, reading::review(scope, move->path));
            treeState.pendingFocus = true;
        } else if (selectedPath) navigation::click(*repo, reading::review(scope, *selectedPath), afterhours::input::is_key_pressed(257));
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
                .with_size(ComponentSize{tabRowW, h720(TAB_HEIGHT)})
                .with_flex_direction(FlexDirection::Row)
                .with_align_items(AlignItems::Center)
                .with_padding(Padding{
                    .top = h720(2), .right = pixels(8),
                    .bottom = h720(2), .left = pixels(8)})
                .with_custom_background(theme::SIDEBAR_BG)
                .with_roundness(0.0f)
                .with_debug_name("sidebar_mode_tabs"));

        auto* countRepo = find_singleton<RepoComponent, ActiveTab>();
        int nReview = countRepo ? static_cast<int>(countRepo->unstagedFiles.size()) : 0;
        int nStaged = countRepo ? static_cast<int>(countRepo->stagedFiles.size()) : 0;
        int nUntracked = countRepo ? static_cast<int>(countRepo->untrackedFiles.size()) : 0;
        int nRefs = countRepo ? static_cast<int>(countRepo->branches.size()) : 0;

        // Emit one tab. `active` highlights it; on click, `apply` mutates layout.
        auto makeTab = [&](int id, const std::string& label, bool active,
                           void (*apply)(LayoutComponent&)) {
            // Mock: tabs are transparent text with an accent underline on the
            // active one (not filled pills).
            // expand(), not children(): four natural-width tabs do not fit a
            // 200px sidebar (the minimum) and NoWrap put Refs outside the strip
            // where it could not be clicked at all. Sharing the row keeps all
            // four reachable at every width, and at the default width there is
            // enough for each to render its label in full anyway.
            auto config = preset::Button(label)
                .with_size(ComponentSize{afterhours::ui::expand(),
                                         h720(TAB_HEIGHT - 4)})
                .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
                // Tight padding: four tabs share a sidebar that starts at
                // 200px, so every pixel here is one the labels do not get.
                .with_padding(Padding{
                    .top = h720(2), .right = pixels(5),
                    .bottom = h720(2), .left = pixels(5)})
                .with_margin(Margin{
                    .top = {}, .bottom = {},
                    .left = {}, .right = pixels(3)})
                .with_font_size(FontSize::Medium)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("tab_" + label);
            if (active) {
                config = config.with_custom_text_color(theme::TEXT_PRIMARY)
                               .with_border_bottom(theme::BUTTON_PRIMARY, h720(2.0f));
            } else {
                config = config.with_custom_text_color(theme::TEXT_SECONDARY);
            }
            if (button(ctx, mk(tabRow.ent(), id), config)) {
                auto* lc = find_singleton<LayoutComponent>();
                if (lc) apply(*lc);
            }
        };

        using SM = LayoutComponent::SidebarMode;
        using RT = LayoutComponent::ReviewTab;
        bool inChanges = (layout.sidebarMode == SM::Changes);
        makeTab(2091, "Changes " + std::to_string(nReview),
                inChanges && layout.reviewTab == RT::ToReview,
                [](LayoutComponent& l) { l.sidebarMode = SM::Changes; l.reviewTab = RT::ToReview; });
        makeTab(2092, "Staged " + std::to_string(nStaged),
                inChanges && layout.reviewTab == RT::Staged,
                [](LayoutComponent& l) { l.sidebarMode = SM::Changes; l.reviewTab = RT::Staged; });
        makeTab(2093, "Untracked " + std::to_string(nUntracked),
                inChanges && layout.reviewTab == RT::Untracked,
                [](LayoutComponent& l) { l.sidebarMode = SM::Changes; l.reviewTab = RT::Untracked; });
        makeTab(2094, "Refs " + std::to_string(nRefs), layout.sidebarMode == SM::Refs,
                [](LayoutComponent& l) { l.sidebarMode = SM::Refs; });
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
                .with_size(ComponentSize{w, pixels(62)})
                .with_flex_direction(FlexDirection::Row)
                .with_align_items(AlignItems::Center)
                .with_gap(pixels(8))
                .with_padding(Padding{
                    .top = pixels(12), .right = pixels(14),
                    .bottom = pixels(8), .left = pixels(14)})
                .with_custom_background(theme::SIDEBAR_BG)
                .with_roundness(0.0f)
                .with_debug_name("repo_header"));
        auto icon = div(ctx, mk(row.ent(), 1), ComponentConfig{}
            .with_size(ComponentSize{pixels(32), pixels(32)})
            .with_custom_background(theme::BUTTON_SECONDARY)
            .with_border(theme::BORDER, pixels(1))
            .with_rounded_corners(theme::layout::ROUNDED_CORNERS).with_corner_radius(7.f)
            .with_debug_name("repo_folder_icon"));
        div(ctx, mk(icon.ent(), 0), ComponentConfig{}
            .with_size(ComponentSize{pixels(17), pixels(12)})
            .with_absolute_position(7.f, 11.f).with_transparent_bg()
            .with_border(theme::TEXT_ACCENT, pixels(1.5f)));
        div(ctx, mk(icon.ent(), 1), ComponentConfig{}
            .with_size(ComponentSize{pixels(7), pixels(4)})
            .with_absolute_position(7.f, 8.f).with_custom_background(theme::BUTTON_SECONDARY)
            .with_border_top(theme::TEXT_ACCENT, pixels(1.5f))
            .with_border_left(theme::TEXT_ACCENT, pixels(1.5f))
            .with_border_right(theme::TEXT_ACCENT, pixels(1.5f)));
        auto text = div(ctx, mk(row.ent(), 2), ComponentConfig{}
            .with_size(ComponentSize{expand(), pixels(42)})
            .with_flex_direction(FlexDirection::Column));
        div(ctx, mk(text.ent(), 0),
            ComponentConfig{}
                .with_label(name)
                .with_size(ComponentSize{percent(1.f), pixels(23)})
                .with_custom_text_color(theme::TEXT_PRIMARY)
                .with_font("ui-bold", pixels(16))
                .with_alignment(TextAlignment::Left)
                .with_transparent_bg()
                .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
                .with_roundness(0.0f)
                .with_debug_name("repo_header_label"));
        div(ctx, mk(text.ent(), 1), ComponentConfig{}
            .with_label(branch.empty() ? "Open a repository" : branch + (dirty ? " *" : ""))
            .with_size(ComponentSize{percent(1.f), pixels(19)})
            .with_custom_text_color(theme::TEXT_SECONDARY).with_font_size(pixels(12))
            .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis).with_debug_name("repo_branch_label"));
    }

    // Sync actions (Push / Pull / Stash) grouped under the repo header so they
    // read as repo-scoped actions rather than floating chrome.
    void render_sync_row(UIContext<InputAction>& ctx, Entity& parent,
                         RepoComponent* repo) {
        bool hasRepo = repo && !repo->repoPath.empty();
        auto w = sidebarPixelWidth_ > 0 ? pixels(sidebarPixelWidth_) : percent(1.0f);
        auto row = div(ctx, mk(parent, 2085),
            ComponentConfig{}
                .with_size(ComponentSize{w, h720(34)})
                .with_flex_direction(FlexDirection::Row)
                .with_align_items(AlignItems::Center)
                .with_gap(pixels(6))
                .with_custom_background(theme::SIDEBAR_BG)
                .with_padding(Padding{
                    .top = h720(2), .right = pixels(10),
                    .bottom = h720(6), .left = pixels(10)})
                .with_roundness(0.0f)
                .with_debug_name("sync_row"));

        div(ctx, mk(row.ent(), 2086),
            ComponentConfig{}
                .with_label("Sync")
                // Match the button-row height so the caption text sits on the
                // same vertical center as the Push/Pull/Stash buttons (#23).
                .with_size(ComponentSize{children(), h720(22)})
                .with_custom_text_color(theme::TEXT_SECONDARY)
                .with_font_size(FontSize::Small)
                .with_transparent_bg()
                .with_roundness(0.0f)
                .with_debug_name("sync_caption"));

        auto syncBtn = [&](int id, const std::string& label, bool enabled) -> bool {
            // Compact, not expand(): stretching these across the row loses the
            // mock's look. Tighter side padding is what makes all three fit a
            // 200px sidebar, which is the minimum the divider can be dragged to.
            auto config = preset::Button(label, enabled)
                .with_size(ComponentSize{children(), children()})
                .with_padding(Padding{
                    .top = pixels(3), .right = pixels(6),
                    .bottom = pixels(3), .left = pixels(6)})
                .with_font_size(FontSize::Medium)
                .with_cursor(afterhours::ui::CursorType::Pointer)
                .with_debug_name("sync_btn");
            if (enabled)
                config = config.with_custom_background(theme::BUTTON_SECONDARY)
                               .with_custom_text_color(theme::TEXT_PRIMARY);
            return static_cast<bool>(button(ctx, mk(row.ent(), id), config));
        };

        // Plain ASCII labels (font atlas has no arrow glyphs); show ahead/behind
        // counts like the old toolbar did.
        std::string pushLabel = "Push";
        std::string pullLabel = "Pull";
        if (repo && repo->aheadCount > 0)
            pushLabel += " (" + std::to_string(repo->aheadCount) + ")";
        if (repo && repo->behindCount > 0)
            pullLabel += " (" + std::to_string(repo->behindCount) + ")";

        if (syncBtn(2087, pushLabel, hasRepo))
            enqueue_network_op("Push", git::git_run_async(repo->repoPath, {"push"}));
        if (syncBtn(2088, pullLabel, hasRepo))
            enqueue_network_op("Pull", git::git_run_async(repo->repoPath, {"pull"}));
        if (syncBtn(2089, "Stash", hasRepo)) {
            auto* menuComp = find_singleton<MenuComponent>();
            if (menuComp)
                menuComp->pendingToasts.push_back({"Stash is not yet implemented"});
        }
    }

    // Centered "No repository open" placeholder, shown in each sidebar pane
    // (files / refs / commit log) when no repo is loaded.
    void render_no_repo(UIContext<InputAction>& ctx, Entity& parent, int id,
                        const std::string& debugName) {
        div(ctx, mk(parent, id),
            ComponentConfig{}
                .with_label("No repository open")
                .with_size(ComponentSize{percent(1.0f), h720(32)})
                .with_padding(Padding{
                    .top = h720(16), .right = pixels(8),
                    .bottom = h720(8), .left = pixels(8)})
                .with_custom_text_color(theme::TEXT_TERTIARY)
                .with_alignment(TextAlignment::Center)
                .with_roundness(0.0f)
                .with_debug_name(debugName));
    }

    void render_review_progress(UIContext<InputAction>& ctx, Entity& parent,
                                RepoComponent& repo, ReviewComponent* review) {
        bool reviewing = review && review->reviewing;
        int toReview = static_cast<int>(repo.unstagedFiles.size());
        int approvedHunks = 0;
        int queued = review ? static_cast<int>(unresolved_comment_count(*review)) : 0;

        int remainingHunks = 0;
        for (const auto& file : repo.currentDiff)
            for (const auto& hunk : file.hunks) {
                if (review && review->approvedHunks.contains("wt\n" + ReviewComponent::hunk_key(file.filePath, hunk))) ++approvedHunks;
                else ++remainingHunks;
            }
        int totalHunks = approvedHunks + remainingHunks;
        float frac = (reviewing && totalHunks > 0)
                         ? static_cast<float>(approvedHunks) / totalHunks : 0.f;

        bool aside = reviewing && !repo.selectedCommitHash().empty();
        const std::string txt = std::to_string(approvedHunks) + "/" + std::to_string(totalHunks) +
            " approved · " + std::to_string(queued) + " comments";

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
                    review->dirty = true;
                    if (review->reviewing) {
                        if (review->baselineSnapshot.empty() && !review->snapshotFuture.valid())
                            start_review_snapshot(repo, *review, true);
                        review->baselineHead =
                            repo.commitLog.empty() ? "" : repo.commitLog[0].hash;
                        review->baselineDiffSig.clear();
                        for (auto& fd : repo.currentDiff) {
                            review->seenSig[fd.filePath] = diff_signature(fd);
                            review->baselineDiffSig += diff_signature(fd) + ";";
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
        if (reviewing || toReview > 0) {
            auto bar = div(ctx, mk(status.ent(), 0),
                ComponentConfig{}
                    .with_size(ComponentSize{pixels(32), pixels(4)})
                    .with_custom_background(afterhours::Color{51, 51, 51, 255})
                    .with_corner_radius(2.0f)
                    .with_debug_name("prog_bar"));
            div(ctx, mk(bar.ent(), 0),
                ComponentConfig{}
                    .with_size(ComponentSize{percent(frac), percent(1.0f)})
                    .with_custom_background(afterhours::Color{63, 185, 80, 255})
                    .with_corner_radius(2.0f)
                    .with_debug_name("prog_fill"));
        }
        div(ctx, mk(status.ent(), 1),
            ComponentConfig{}
                .with_label(txt)
                .with_size(ComponentSize{expand(), pixels(20)})
                .with_custom_text_color(theme::TEXT_SECONDARY)
                .with_font_size(pixels(11))
                .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
                .with_debug_name("prog_text"));
    }

    // Height of the multi-line commit message box (720-space). Kept in sync
    // with COMMIT_AREA_H_720, which reserves the whole commit area's space.
    static constexpr float COMMIT_INPUT_H_720 = 60.0f;

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
                    .top = h720(4), .right = pixels(8),
                    .bottom = h720(2), .left = pixels(8)})
                .with_gap(h720(3))
                .with_custom_background(theme::SIDEBAR_BG)
                .with_roundness(0.0f)
                .with_debug_name("commit_area"));

        // Commit message hint (shows branch name). Stays a label above the
        // field rather than the field's own placeholder: with_placeholder is
        // read by text_field only, and text_area ignores it without a word.
        {
            std::string branch = repo.currentBranch.empty() ? "main" : repo.currentBranch;
            std::string hint = "Message (Enter to commit on \""
                               + branch + "\")";
            div(ctx, mk(commitArea.ent(), 0),
                ComponentConfig{}
                    .with_label(hint)
                    .with_size(ComponentSize{childW, h720(16)})
                    .with_custom_text_color(theme::TEXT_SECONDARY)
                    .with_font_size(FontSize::Small)
                    .with_alignment(TextAlignment::Left)
                    .with_roundness(0.0f)
                    .with_debug_name("commit_hint"));
        }

        // Multi-line message: Enter commits (submit_on_enter), Shift+Enter adds
        // a body line. The whole string is the commit message — build_message
        // passes a multi-line subject through as subject + body.
        auto inputResult = ui::text_area(
            ctx, mk(commitArea.ent(), 1),
            editor.subject,
            ComponentConfig{}
                .with_size(ComponentSize{childW, h720(COMMIT_INPUT_H_720)})
                .with_custom_background(theme::INPUT_BG)
                .with_border(theme::BORDER, h720(1.0f))
                .with_corner_radius(4.0f)
                .with_line_height(pixels(18.0f))
                .with_submit_on_enter()
                .with_debug_name("commit_msg_input"));

        // Enter fires on_submit -> request a commit.
        inputResult.ent().addComponentIfMissing<afterhours::text_input::HasTextInputListener>();
        inputResult.ent()
            .get<afterhours::text_input::HasTextInputListener>()
            .on_submit = [](Entity&) {
                auto* ed = find_singleton<CommitEditorComponent, ActiveTab>();
                if (ed) ed->commitRequested = true;
            };

        // Full-width blue Commit button
        bool hasStaged = !repo.stagedFiles.empty();
        auto commitBtn = button(ctx, mk(commitArea.ent(), 2),
            preset::Button("Commit", hasStaged)
                .with_size(ComponentSize{childW, h720(24)})
                .with_font_size(FontSize::Medium)
                .with_debug_name("commit_btn_inline"));

        if (commitBtn && hasStaged) {
            editor.commitRequested = true;
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
                .with_size(ComponentSize{percent(1.0f), h720(28)})
                .with_flex_direction(FlexDirection::Row)
                .with_align_items(AlignItems::Center)
                .with_padding(Padding{
                    .top = h720(4), .right = pixels(8),
                    .bottom = h720(4), .left = pixels(8)})
                .with_custom_background(theme::SIDEBAR_BG)
                .with_roundness(0.0f)
                .with_debug_name("refs_header"));

        std::string branchLabel = "BRANCHES  " +
            std::to_string(repo.branches.size());
        div(ctx, mk(headerRow.ent(), 1),
            preset::SectionHeader(branchLabel)
                // expand(), not percent(1): "+ New" shares this row, and a
                // full-width label pushed it outside the header.
                .with_size(ComponentSize{afterhours::ui::expand(), children()})
                .with_debug_name("branches_label"));

        // "+ New" button
        if (!repo.reviewWorkspace) {
            auto newBranchBtn = button(ctx, mk(headerRow.ent(), 2),
                preset::Button("+ New")
                    .with_size(ComponentSize{children(), h720(18)})
                    .with_padding(Padding{
                        .top = h720(2), .right = pixels(8),
                        .bottom = h720(2), .left = pixels(8)})
                    .with_font_size(FontSize::Medium)
                    .with_debug_name("new_branch_btn"));

            if (newBranchBtn) {
                auto* bd = find_singleton<BranchDialogState, ActiveTab>();
                if (bd) {
                    bd->showNewBranchDialog = true;
                    bd->newBranchName.clear();
                }
            }
        }

        // Branch list. Render rows directly into `parent` (the outer files
        // ScrollPanel), NOT into a nested ScrollPanel — see render_file_list:
        // a container nested inside filesBg fails to render its children after
        // the UI has been exercised (framework nested-container bug).
        (void)layout;
        if (repo.branches.empty()) {
            div(ctx, mk(parent, 2170),
                ComponentConfig{}
                    .with_label("No branches found")
                    .with_size(ComponentSize{percent(1.0f), h720(32)})
                    .with_padding(Padding{
                        .top = h720(16), .right = pixels(8),
                        .bottom = h720(8), .left = pixels(8)})
                    .with_custom_text_color(theme::TEXT_TERTIARY)
                    .with_alignment(TextAlignment::Center)
                    .with_roundness(0.0f)
                    .with_debug_name("no_branches"));
            return;
        }

        // Render each branch row
        for (int i = 0; i < static_cast<int>(repo.branches.size()); ++i) {
            render_branch_row(ctx, parent, i, repo.branches[i], repo);
        }
    }

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
                .with_size(ComponentSize{percent(1.0f), h720(ROW_H)})
                .with_custom_background(rowBg)
                .with_padding(Padding{
                    .top = h720(0), .right = pixels(8),
                    .bottom = h720(0), .left = pixels(0)})
                .with_roundness(0.0f)
                .with_debug_name("branch_row"));
        ui::set_tooltip(rowResult.ent(), branch.name);

        rowResult.ent().addComponentIfMissing<HasClickListener>([](Entity&){});

        // Click -> checkout this branch
        if (rowResult.ent().get<HasClickListener>().down && !isCurrent && !repo.reviewWorkspace) {
            auto result = git::checkout_branch(repo.repoPath, branch.name);
            toast_on_git_failure(result, "Checkout");
            if (result.success()) {
                repo.refreshRequested = true;
            }
        }

        // Current branch indicator (green left border)
        if (isCurrent) {
            div(ctx, mk(rowResult.ent(), 1),
                ComponentConfig{}
                    .with_size(ComponentSize{pixels(3), h720(ROW_H)})
                    .with_custom_background(theme::STATUS_ADDED)
                    .with_roundness(0.0f)
                    .with_debug_name("current_indicator"));
        }

        // Branch type badge
        auto badgeBg = branch.isLocal ? theme::BADGE_BRANCH_BG
                                      : theme::BADGE_REMOTE_BG;
        div(ctx, mk(rowResult.ent(), 2),
            preset::Badge(branch.isLocal ? "L" : "R", badgeBg,
                          afterhours::Color{255, 255, 255, 255})
                .with_size(ComponentSize{pixels(20), h720(16)})
                .with_padding(Padding{
                    .top = h720(1), .right = pixels(3),
                    .bottom = h720(1), .left = pixels(3)})
                .with_margin(Margin{
                    .top = {}, .bottom = {},
                    .left = pixels(isCurrent ? 5.0f : 8.0f),
                    .right = pixels(6)})
                .with_debug_name("branch_badge"));

        // Branch name takes whatever the badge, tracking and delete button
        // leave. This used to subtract each sibling by hand because percent(1)
        // resolved to the whole row and overflowed them; expand() does it now.
        auto nameColor = isCurrent ? afterhours::Color{255, 255, 255, 255}
                                   : theme::TEXT_PRIMARY;
        div(ctx, mk(rowResult.ent(), 3),
            ComponentConfig{}
                .with_label(branch.name)
                .with_size(ComponentSize{afterhours::ui::expand(), h720(ROW_H)})
                .with_custom_text_color(nameColor)
                .with_font_size(FontSize::Medium)
                .with_alignment(TextAlignment::Left)
                .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
                .with_roundness(0.0f)
                .with_debug_name("branch_name"));

        // Tracking info (ahead/behind)
        if (!branch.tracking.empty()) {
            div(ctx, mk(rowResult.ent(), 4),
                ComponentConfig{}
                    .with_label(branch.tracking)
                    .with_size(ComponentSize{children(), h720(ROW_H)})
                    .with_padding(Padding{
                        .top = h720(0), .right = pixels(4),
                        .bottom = h720(0), .left = pixels(4)})
                    .with_custom_text_color(theme::TEXT_SECONDARY)
                    .with_font_size(FontSize::Medium)
                    .with_alignment(TextAlignment::Right)
                    .with_roundness(0.0f)
                    .with_debug_name("branch_tracking"));
        }

        // Delete button (only for non-current branches)
        if (!isCurrent && !repo.reviewWorkspace) {
            auto deleteBtn = button(ctx, mk(rowResult.ent(), 5),
                preset::Button("x")
                    .with_size(ComponentSize{pixels(20), h720(20)})
                    .with_custom_background(theme::BUTTON_SECONDARY)
                    .with_custom_text_color(theme::STATUS_DELETED)
                    .with_debug_name("delete_branch_btn"));

            if (deleteBtn) {
                auto* bd = find_singleton<BranchDialogState, ActiveTab>();
                if (bd) {
                    bd->deleteBranchName = branch.name;
                    bd->showDeleteBranchDialog = true;
                }
            }
        }

    }

    // ---- New Branch dialog (T031) ----
    void render_new_branch_dialog(UIContext<InputAction>& ctx,
                                   Entity& uiRoot,
                                   RepoComponent& repo,
                                   BranchDialogState& bd) {
        if (!bd.showNewBranchDialog) return;

        using namespace afterhours;
        using afterhours::ui::h720;

        constexpr int MODAL_ID = 8100;
        constexpr int CONTENT_LAYER = 1001;

        auto modalResult = afterhours::modal::detail::modal_impl(
            ctx, mk(uiRoot, MODAL_ID), bd.showNewBranchDialog,
            ModalConfig{}
                .with_size(pixels(380), h720(180))
                .with_title("New Branch")
                .with_show_close_button(false));

        if (!modalResult) return;
        auto& modalEnt = modalResult.ent();

        // Label
        div(ctx, mk(modalEnt, 1),
            ComponentConfig{}
                .with_label("Branch name:")
                .with_size(ComponentSize{percent(1.0f), h720(20)})
                .with_padding(Padding{
                    .top = h720(8), .right = pixels(16),
                    .bottom = h720(4), .left = pixels(16)})
                .with_custom_text_color(theme::TEXT_PRIMARY)
                .with_alignment(TextAlignment::Left)
                .with_render_layer(CONTENT_LAYER)
                .with_debug_name("new_branch_label"));

        // Text input
        afterhours::text_input::text_input(ctx, mk(modalEnt, 2),
            bd.newBranchName,
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), h720(32)})
                .with_padding(Padding{
                    .top = h720(0), .right = pixels(16),
                    .bottom = h720(0), .left = pixels(16)})
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
            bd.showNewBranchDialog = false;
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
                    repo.refreshRequested = true;
                }
                bd.showNewBranchDialog = false;
                bd.newBranchName.clear();
            }
        }
    }

    // ---- Delete Branch confirmation dialog (T031) ----
    void render_delete_branch_dialog(UIContext<InputAction>& ctx,
                                      Entity& uiRoot,
                                      RepoComponent& repo,
                                      BranchDialogState& bd) {
        if (!bd.showDeleteBranchDialog) return;

        using namespace afterhours;
        using afterhours::ui::h720;

        constexpr int MODAL_ID = 8200;
        constexpr int CONTENT_LAYER = 1001;

        std::string message = "Delete branch '" + bd.deleteBranchName + "'?\n"
            "This will delete the local branch. If it has unmerged\n"
            "changes, you will be prompted to force delete.";

        auto modalResult = afterhours::modal::detail::modal_impl(
            ctx, mk(uiRoot, MODAL_ID), bd.showDeleteBranchDialog,
            ModalConfig{}
                .with_size(pixels(420), h720(180))
                .with_title("Delete Branch")
                .with_show_close_button(false));

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
            bd.showDeleteBranchDialog = false;
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
                repo.refreshRequested = true;
                bd.showDeleteBranchDialog = false;
                bd.deleteBranchName.clear();
            } else {
                bd.showDeleteBranchDialog = false;
                bd.showForceDeleteDialog = true;
            }
        }
    }

    // ---- Force Delete Branch dialog (T031) ----
    void render_force_delete_dialog(UIContext<InputAction>& ctx,
                                     Entity& uiRoot,
                                     RepoComponent& repo,
                                     BranchDialogState& bd) {
        if (!bd.showForceDeleteDialog) return;

        using namespace afterhours;
        using afterhours::ui::h720;

        constexpr int MODAL_ID = 8300;
        constexpr int CONTENT_LAYER = 1001;

        std::string message = "Branch '" + bd.deleteBranchName +
            "' has unmerged changes.\n\n"
            "Force delete will permanently lose these changes.\n"
            "Are you sure?";

        auto modalResult = afterhours::modal::detail::modal_impl(
            ctx, mk(uiRoot, MODAL_ID), bd.showForceDeleteDialog,
            ModalConfig{}
                .with_size(pixels(420), h720(200))
                .with_title("Force Delete Branch")
                .with_show_close_button(false));

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
            bd.showForceDeleteDialog = false;
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
                repo.refreshRequested = true;
            }
            bd.showForceDeleteDialog = false;
            bd.deleteBranchName.clear();
        }
    }

    // Render the view mode tabs: [Changed] [Tree] [All]
    void render_view_mode_tabs(UIContext<InputAction>& ctx,
                                Entity& parent,
                                LayoutComponent& layout) {
        constexpr float TAB_HEIGHT = 26.0f;
        constexpr float TAB_HPAD = 10.0f;

        auto vmTabW = sidebarPixelWidth_ > 0 ? pixels(sidebarPixelWidth_) : percent(1.0f);
        auto tabRow = div(ctx, mk(parent, 2120),
            ComponentConfig{}
                .with_size(ComponentSize{vmTabW, h720(TAB_HEIGHT)})
                .with_flex_direction(FlexDirection::Row)
                .with_align_items(AlignItems::Center)
                .with_padding(Padding{
                    .top = h720(2), .right = pixels(10),
                    .bottom = h720(2), .left = pixels(10)})
                .with_custom_background(theme::SIDEBAR_BG)
                .with_roundness(0.0f)
                .with_debug_name("view_mode_tabs"));

        auto makeTab = [&](int id, const std::string& label,
                           LayoutComponent::FileViewMode mode) {
            bool active = (layout.fileViewMode == mode);

            auto config = preset::Button(label)
                .with_size(ComponentSize{children(), h720(TAB_HEIGHT - 6)})
                .with_padding(Padding{
                    .top = h720(2), .right = pixels(TAB_HPAD),
                    .bottom = h720(2), .left = pixels(TAB_HPAD)})
                .with_margin(Margin{
                    .top = {}, .bottom = {},
                    .left = {}, .right = pixels(4)})
                .with_debug_name("tab_" + label);
            if (!active) {
                config = config.with_custom_background(theme::BUTTON_SECONDARY)
                               .with_custom_text_color(theme::TEXT_PRIMARY);
            }

            auto result = button(ctx, mk(tabRow.ent(), id), config);

            if (result) {
                auto* lc = find_singleton<LayoutComponent>();
                if (lc) lc->fileViewMode = mode;
            }
        };

        makeTab(2121, "Changed", LayoutComponent::FileViewMode::Flat);
        makeTab(2122, "Tree", LayoutComponent::FileViewMode::Tree);
        makeTab(2123, "All", LayoutComponent::FileViewMode::All);
    }

    // Render the file list with Staged, Changes, and Untracked sections
    // parentWidth: explicit pixel width to avoid percent resolution bug
    float sidebarPixelWidth_ = 0; // Set before rendering
    // Which review tab the sidebar is showing, and that tab's rows. The
    // windowed list above asks for a count and a row builder; render_file_list
    // below keeps the empty states.
    static LayoutComponent::ReviewTab active_review_tab() {
        auto* lc = find_singleton<LayoutComponent>();
        return lc ? lc->reviewTab : LayoutComponent::ReviewTab::ToReview;
    }

    std::vector<file_tree::Row> treeRows_;
    std::vector<std::string> treePaths_;
    std::set<std::string> treeCollapsed_;
    bool treeMode_ = false;
    bool allFilesMode_ = false;
    std::vector<size_t> fileIndices_;
    struct FileRowsKey {
        std::string repo;
        unsigned generation;
        unsigned patches;
        unsigned version;
        unsigned paths;
        LayoutComponent::ReviewTab tab;
        LayoutComponent::FileViewMode mode;
        review_files::Filter filter;
        std::set<std::pair<std::string, bool>> unresolved;
        bool operator==(const FileRowsKey&) const = default;
    };
    std::optional<FileRowsKey> fileRowsKey_;

    void update_file_tree(UIContext<InputAction>& ctx, afterhours::ui::imm::EntityParent list,
                          RepoComponent& repo, const LayoutComponent& layout, const std::string& context) {
        auto publish = [&](std::vector<file_tree::Row> rows) {
            ui::replace_tree_rows(ctx, list, repo, repo.filesTreeNavigation, context, treeRows_, std::move(rows));
        };
        allFilesMode_ = layout.fileViewMode == LayoutComponent::FileViewMode::All;
        treeMode_ = layout.fileViewMode == LayoutComponent::FileViewMode::Tree;
        auto tab = active_review_tab();
        FileRowsKey key{repo.repoPath, repo.dataGeneration, repo.patchGeneration, repo.repoVersion, repo.allFilePathsGeneration, tab, layout.fileViewMode, repo.fileFilter};
        if (allFilesMode_) {
            if (!fileRowsKey_ || *fileRowsKey_ != key) {
                std::vector<file_tree::Row> rows;
                rows.reserve(repo.allFilePaths.size());
                for (size_t i = 0; i < repo.allFilePaths.size(); ++i) rows.push_back({repo.allFilePaths[i], i, 0, false});
                publish(std::move(rows));
                fileRowsKey_ = std::move(key);
            }
            return;
        }
        auto* review = find_singleton<ReviewComponent, ActiveTab>();
        std::string scope = tab == LayoutComponent::ReviewTab::Staged ? "index" : "wt";
        if (repo.fileFilter.onlyUnresolved && review)
            for (const auto& comment : review->comments)
                if (!comment.resolved && comment.scope == scope) key.unresolved.emplace(comment.file, comment.oldSide);
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
        fileRowsKey_ = std::move(key);
        std::vector<std::string> paths;
        fileIndices_.clear();
        auto append = [&](const std::string& path, size_t index, char change, const std::string& oldPath = "") {
            if (review_files::matches(repo.fileFilter, path, change) &&
                (!repo.fileFilter.onlyUnresolved || (review && unresolved_file_count(*review, scope, path, oldPath) > 0))) {
                paths.push_back(path);
                fileIndices_.push_back(index);
            }
        };
        if (tab == LayoutComponent::ReviewTab::ToReview) {
            for (size_t i = 0; i < repo.unstagedFiles.size(); ++i) append(repo.unstagedFiles[i].path, i, repo.unstagedFiles[i].workTreeStatus, repo.unstagedFiles[i].origPath);
        } else if (tab == LayoutComponent::ReviewTab::Staged) {
            for (size_t i = 0; i < repo.stagedFiles.size(); ++i) append(repo.stagedFiles[i].path, i, repo.stagedFiles[i].indexStatus, repo.stagedFiles[i].origPath);
        } else for (size_t i = 0; i < repo.untrackedFiles.size(); ++i) append(repo.untrackedFiles[i], i, 'A');
        std::map<std::string, int> changes;
        for (const auto& file : tab == LayoutComponent::ReviewTab::Staged ? repo.stagedDiff : repo.currentDiff)
            changes[file.filePath] = file.additions + file.deletions;
        std::vector<size_t> order(paths.size());
        std::iota(order.begin(), order.end(), size_t{0});
        auto churn = [&](const std::string& path) { auto found = changes.find(path); return found == changes.end() ? 0 : found->second; };
        std::sort(order.begin(), order.end(), [&](size_t a, size_t b) {
            return review_files::precedes(repo.fileFilter.sort, paths[a], churn(paths[a]), paths[b], churn(paths[b]));
        });
        std::vector<size_t> indices;
        treePaths_.clear();
        for (size_t index : order) {
            indices.push_back(fileIndices_[index]);
            treePaths_.push_back(std::move(paths[index]));
        }
        fileIndices_ = std::move(indices);
        treeCollapsed_ = collapsed;
        std::vector<file_tree::Row> rows;
        if (treeMode_) rows = file_tree::flatten(treePaths_, treeCollapsed_);
        else for (size_t i = 0; i < treePaths_.size(); ++i) rows.push_back({treePaths_[i], i, 0, false});
        publish(std::move(rows));
    }

    size_t active_file_count(const RepoComponent& repo) const {
        if (allFilesMode_) return repo.allFilePaths.size();
        if (treeMode_) return treeRows_.size();
        return fileIndices_.size();
    }

    void render_active_file_row(UIContext<InputAction>& ctx, Entity& row,
                                size_t i, RepoComponent& repo) {
        if (allFilesMode_) {
            const auto& path = repo.allFilePaths[i];
            char status = ' ';
            for (const auto& file : repo.stagedFiles) if (file.path == path) status = file.indexStatus;
            for (const auto& file : repo.unstagedFiles) if (file.path == path) status = file.workTreeStatus;
            if (std::find(repo.untrackedFiles.begin(), repo.untrackedFiles.end(), path) != repo.untrackedFiles.end()) status = 'U';
            render_file_row_impl(ctx, row, 0, path, status, repo, false, false);
            return;
        }
        size_t depth = 0;
        if (treeMode_) {
            const auto& node = treeRows_[i];
            depth = node.depth;
            if (node.directory) {
                if (ui::file_tree_style::directory(ctx, row, node, sidebarPixelWidth_,
                        file_tree::directory_collapsed(node, treeCollapsed_), "tree_directory:" + node.path, repo, repo.filesTreeNavigation)) {
                    auto& collapsed = find_singleton<LayoutComponent>()->collapsedDirectories[repo.repoPath];
                    file_tree::toggle_directory(node, collapsed);
                }
                return;
            }
            i = node.sourceIndex;
        }
        i = fileIndices_[i];
        auto tab = active_review_tab();
        if (tab == LayoutComponent::ReviewTab::ToReview) {
            render_file_row(ctx, row, 0, repo.unstagedFiles[i], repo, false, depth);
        } else if (tab == LayoutComponent::ReviewTab::Staged) {
            render_file_row(ctx, row, 0, repo.stagedFiles[i], repo, true, depth);
        } else {
            render_untracked_row(ctx, row, 0, repo.untrackedFiles[i], repo, depth);
        }
    }

    void render_file_list(UIContext<InputAction>& ctx,
                          Entity& scrollParent,
                          RepoComponent& repo) {
        if (repo.repoPath.empty()) {
            div(ctx, mk(scrollParent, 2500), preset::EmptyStateText("No repository open")
                .with_size(ComponentSize{percent(1.0f), h720(28)})
                .with_debug_name("no_repository"));
            return;
        }
        if (!repo.filesError.empty()) {
            if (button(ctx, mk(scrollParent, 2500), preset::Button("Retry repository read")
                    .with_size(ComponentSize{percent(1.0f), h720(28)})
                    .with_debug_name("retry_repository_read")))
                repo.refreshRequested = true;
            div(ctx, mk(scrollParent, 2501), preset::EmptyStateText(
                    repo.filesError.substr(0, repo.filesError.find('\n')))
                .with_size(ComponentSize{percent(1.0f), h720(28)})
                .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
                .with_debug_name("repository_read_error"));
            return;
        }
        if (!allFilesMode_ && (!repo.stagedFiles.empty() || !repo.unstagedFiles.empty() || !repo.untrackedFiles.empty()) &&
            (repo.fileFilter.hideGenerated || repo.fileFilter.hideVendor || repo.fileFilter.hideLockfiles ||
             repo.fileFilter.onlyUnresolved || !repo.fileFilter.language.empty() || repo.fileFilter.change != ' ') && fileIndices_.empty()) {
            div(ctx, mk(scrollParent, 2598), preset::EmptyStateText("No files match the review filters")
                .with_size(ComponentSize{percent(1.f), h720(28)}).with_debug_name("filtered_files_empty"));
            return;
        }
        bool empty = repo.stagedFiles.empty() &&
                     repo.unstagedFiles.empty() &&
                     repo.untrackedFiles.empty();

        if (empty) {
            if (!repo.hasLoadedOnce) {
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
                        .with_size(ComponentSize{percent(1.0f), h720(28)})
                        .with_padding(Padding{
                            .top = h720(20), .right = pixels(8),
                            .bottom = h720(4), .left = pixels(8)})
                        .with_custom_text_color(theme::TEXT_SECONDARY)
                        .with_alignment(TextAlignment::Center)
                        .with_roundness(0.0f)
                        .with_debug_name("loading_spinner"));
            } else {
                div(ctx, mk(scrollParent, 2500),
                    preset::EmptyStateText("\xe2\x9c\x93 No changes")
                        .with_size(ComponentSize{percent(1.0f), h720(28)})
                        .with_padding(Padding{
                            .top = h720(20), .right = pixels(8),
                            .bottom = h720(4), .left = pixels(8)})
                        .with_debug_name("empty_changes"));

                div(ctx, mk(scrollParent, 2501),
                    ComponentConfig{}
                        .with_label("Working tree clean")
                        .with_size(ComponentSize{percent(1.0f), h720(22)})
                        .with_padding(Padding{
                            .top = h720(0), .right = pixels(8),
                            .bottom = h720(8), .left = pixels(8)})
                        .with_custom_text_color(theme::TEXT_TERTIARY)
                        .with_alignment(TextAlignment::Center)
                        .with_roundness(0.0f)
                        .with_debug_name("empty_clean"));
            }
            return;
        }

        auto* lc = find_singleton<LayoutComponent>();
        auto tab = lc ? lc->reviewTab : LayoutComponent::ReviewTab::ToReview;
        int nextId = 2600;
        size_t shown = 0;
        const char* emptyMsg = "";
        if (tab == LayoutComponent::ReviewTab::ToReview) {
            for (int i = 0; i < static_cast<int>(repo.unstagedFiles.size()); ++i)
                render_file_row(ctx, scrollParent, nextId++,
                                repo.unstagedFiles[i], repo, false);
            shown = repo.unstagedFiles.size();
            emptyMsg = "Nothing to review";
        } else if (tab == LayoutComponent::ReviewTab::Staged) {
            for (int i = 0; i < static_cast<int>(repo.stagedFiles.size()); ++i)
                render_file_row(ctx, scrollParent, nextId++,
                                repo.stagedFiles[i], repo, true);
            shown = repo.stagedFiles.size();
            emptyMsg = "Nothing staged yet";
        } else {
            for (int i = 0; i < static_cast<int>(repo.untrackedFiles.size()); ++i)
                render_untracked_row(ctx, scrollParent, nextId++,
                                     repo.untrackedFiles[i], repo);
            shown = repo.untrackedFiles.size();
            emptyMsg = "No untracked files";
        }
        if (shown == 0) {
            div(ctx, mk(scrollParent, 2599),
                ComponentConfig{}
                    .with_label(emptyMsg)
                    .with_size(ComponentSize{percent(1.0f), h720(24)})
                    .with_padding(Padding{
                        .top = h720(12), .right = pixels(8),
                        .bottom = h720(4), .left = pixels(8)})
                    .with_custom_text_color(afterhours::Color{110, 110, 110, 255})
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
        if (treeMode_) {
            dir.clear();
        }
        // Submodules show "S" (gitlink pointer change) rather than the raw M/A.
        std::string statusStr(1, isSubmodule ? 'S' : statusChar);

        auto row = div(ctx, mk(parent, id),
            ui::file_tree_style::row_config(sidebarPixelWidth_,
                depth, selected)
                .with_debug_name("file_row"));
        ui::bind_tree_row(ctx, row.ent(), repo, repo.filesTreeNavigation, path);
        ui::set_tooltip(row.ent(), path);

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
        auto statusCol = isSubmodule ? afterhours::Color{170, 140, 230, 255}
                                     : theme::statusColor(statusChar);
        div(ctx, mk(row.ent(), 3),
            preset::MetaText(statusStr)
                .with_size(ComponentSize{pixels(STATUS_W), pixels(28)})
                .with_font("mono", pixels(11))
                .with_custom_text_color(statusCol)
                .with_alignment(TextAlignment::Center)
                .with_debug_name("file_status"));

        div(ctx, mk(row.ent(), 1),
            preset::BodyText(fname)
                .with_size(ComponentSize{afterhours::ui::expand(), pixels(28)})
                .with_font_size(pixels(13))
                .with_custom_text_color(textCol)
                .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
                .with_debug_name("file_name"));

        // Directory hint sits right after the filename (not floated far-right).
        if (!dir.empty()) {
            auto dirCol = theme::TEXT_SECONDARY;
            div(ctx, mk(row.ent(), 2),
                preset::MetaText(dir)
                    .with_size(ComponentSize{pixels(dirW), children()})
                    .with_custom_text_color(dirCol)
                    .with_font_size(FontSize::Small)
                    .with_alignment(TextAlignment::Left)
                    .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
                    .with_debug_name("file_dir"));
        }

        if (auto* rowReview = find_singleton<ReviewComponent, ActiveTab>(); rowReview && !allFilesMode_) {
            const auto& rowDiffs = staged ? repo.stagedDiff : repo.currentDiff;
            const auto rowDiff = std::find_if(rowDiffs.begin(), rowDiffs.end(), [&](const auto& file) { return file.filePath == path; });
            bool changed = false;
            if (!staged && rowDiff != rowDiffs.end()) {
                auto seen = rowReview->seenSig.find(path);
                changed = seen != rowReview->seenSig.end() && ui::diff_metrics().signature(*rowDiff) != seen->second;
            }
            ui::file_tree_style::review_indicator(ctx, row.ent(), rowDiff != rowDiffs.end() &&
                file_reviewed(*rowReview, staged ? "index" : "wt", *rowDiff),
                unresolved_file_count(*rowReview, staged ? "index" : "wt", path, oldPath), changed);
        }

        if (row.ent().get<HasClickListener>().down) {
            auto* r = find_singleton<RepoComponent, ActiveTab>();
            if (r) {
                if (allFilesMode_) navigation::click(*r, reading::source(path), afterhours::input::is_key_pressed(257));
                else navigation::click(*r, reading::review(staged ? "index" : "wt", path), afterhours::input::is_key_pressed(257));
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
                    run_file_git_op(git::unstage_file(repoPath, path),
                                    "Unstage");
                }));
        } else if (!repo.reviewWorkspace) {
            items.push_back(ui::ContextMenuItem::item(
                "Stage", [repoPath, path, canMutate] {
                    if (!canMutate()) return;
                    run_file_git_op(git::stage_file(repoPath, path), "Stage");
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
    static void run_file_git_op(const git::GitResult& result,
                                const std::string& what) {
        if (!result.success()) toast_on_git_failure(result, what);
        auto* r = find_singleton<RepoComponent, ActiveTab>();
        if (r) r->refreshRequested = true;
    }

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
                preset::EmptyStateText(repo.commitLogLoading
                                           ? "Loading commits\xe2\x80\xa6"
                                           : "No commits yet")
                    .with_size(ComponentSize{percent(1.0f), h720(32)})
                    .with_padding(Padding{
                        .top = h720(16), .right = pixels(8),
                        .bottom = h720(8), .left = pixels(8)})
                    .with_debug_name("empty_log"));
            return;
        }

        int count = static_cast<int>(repo.commitLog.size());

        for (int i = 0; i < count; ++i) {
            render_commit_row(ctx, scrollParent, i, repo.commitLog[i], repo);
        }

        if (repo.commitLogHasMore) render_lazy_load_row(ctx, scrollParent, repo);
    }

    // Lazy load indicator at the bottom of the log.
    void render_lazy_load_row(UIContext<InputAction>& ctx, Entity& parent, RepoComponent& repo) {
        auto& page = repo.commitLogPage;
        if (page.error.empty() && !page.future.valid() && !repo.commitLogLoading) page.requested = true;
        const bool loading = page.requested || page.future.valid() || repo.commitLogLoading;
        auto row = button(ctx, mk(parent, 9990),
            ComponentConfig{}
                .with_label(loading ? "Loading older commits..." : "Load failed · Retry")
                .with_disabled(loading)
                .with_consumes_directional_input()
                .with_size(ComponentSize{percent(1.0f), pixels(24)})
                .with_padding(Padding{
                    .top = h720(3), .right = pixels(8),
                    .bottom = h720(3), .left = pixels(8)})
                .with_custom_text_color(loading ? theme::TEXT_SECONDARY : theme::TEXT_PRIMARY)
                .with_font_size(pixels(12))
                .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
                .with_alignment(TextAlignment::Center)
                .with_roundness(0.0f)
                .with_debug_name("lazy_load"));
        ui::bind_focus(row.ent(), repo, reading::focus::Region::History);
        if (!page.error.empty()) ui::set_tooltip(row.ent(), page.error);
        if (row && !loading) page.requested = true;
    }

    commit_graph::Graph graph_;
    std::string graphKey_;

    std::optional<size_t> history_keys(UIContext<InputAction>& ctx, RepoComponent& repo,
            const LayoutComponent& layout, afterhours::ui::imm::EntityParent parent) {
        const auto owner = ui::shortcut_owner(ctx, repo);
        if (owner.region != reading::focus::Region::History || owner.text || repo.commitLog.empty() ||
            ui::shortcuts_blocked(layout) || layout.filePickerOpen) return {};
        for (const int key : {340, 344, 341, 345, 342, 346, 343, 347})
            if (afterhours::input::is_key_down(key)) return {};
        const bool up = afterhours::input::is_key_pressed(265);
        const bool down = afterhours::input::is_key_pressed(264);
        const bool enter = afterhours::input::is_key_pressed(257);
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
        return index;
    }

    void render_commit_row(UIContext<InputAction>& ctx,
                           Entity& parent, int index,
                           const CommitEntry& commit,
                           RepoComponent& repo, bool focus = false, bool acceptClick = true) {
        bool selected = (commit.hash == repo.selectedCommitHash());
        constexpr float ROW_H = theme::layout::COMMIT_ROW_HEIGHT;

        int baseId = index * 2 + 10;
        float sidebarW = sidebarPixelWidth_ > 0 ? sidebarPixelWidth_ : 300.0f;

        auto badges = commit_log_detail::parse_decorations(commit.decorations);

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
                .with_size(ComponentSize{pixels(std::max(0.f, sidebarW - 8.f)), pixels(ROW_H)})
                .with_margin(Margin{.left = pixels(4), .right = pixels(4)})
                .with_rounded_corners(theme::layout::ROUNDED_CORNERS).with_corner_radius(6.f)
                .with_border(selected ? afterhours::Color{69, 83, 103, 255} : afterhours::Color{0, 0, 0, 0}, pixels(1))
                .with_padding(Padding{
                    .top = pixels(0), .right = pixels(4),
                    .bottom = pixels(0), .left = pixels(ROW_INSET_L)})
                .with_gap(pixels(2))
                .with_debug_name("commit_row"));
        ui::bind_focus(row.ent(), repo, reading::focus::Region::History, commit.hash);
        ui::set_tooltip(row.ent(), commit.subject + "\n" + commit.hash + "\n" + commit.decorations);

        row.ent().addComponentIfMissing<HasClickListener>([](Entity&){});
        if (focus) ctx.set_focus(row.ent().id);

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
        div(ctx, mk(graphWrap.ent(), 2), dotCfg);

        auto text = div(ctx, mk(row.ent(), 2), ComponentConfig{}
            .with_size(ComponentSize{expand(), pixels(ROW_H)})
            .with_flex_direction(FlexDirection::Row).with_no_wrap()
            .with_align_items(AlignItems::Center).with_gap(pixels(8))
            .with_padding(Padding{}));
        div(ctx, mk(text.ent(), 0), preset::BodyText(commit.subject)
            .with_size(ComponentSize{expand(), pixels(22)})
            .with_custom_text_color(selected ? theme::TEXT_PRIMARY : theme::TEXT_SECONDARY)
            .with_font_size(pixels(13)).with_text_inset(0.f).with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
            .with_debug_name("commit_subject"));
        auto metadata = div(ctx, mk(text.ent(), 1), ComponentConfig{}
            .with_size(ComponentSize{children(), pixels(20)})
            .with_flex_direction(FlexDirection::Row).with_align_items(AlignItems::Center)
            .with_gap(pixels(8)));
        if (!badges.empty()) {
            const auto* badge = &badges.front();
            for (const auto& value : badges)
                if (value.type == commit_log_detail::DecorationType::Head) badge = &value;
            div(ctx, mk(metadata.ent(), 2), preset::Badge(badge->label, theme::BUTTON_SECONDARY, theme::TEXT_SECONDARY)
                .with_size(ComponentSize{pixels(54), pixels(18)}).with_font_size(pixels(11))
                .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis).with_debug_name("commit_badge"));
        }
        int commentCount = 0;
        if (auto* review = find_singleton<ReviewComponent, ActiveTab>())
            for (const auto& comment : review->comments)
                if (comment.scope == commit.hash && !comment.resolved) ++commentCount;
        if (commentCount > 0)
            div(ctx, mk(metadata.ent(), 3), preset::Badge(std::to_string(commentCount), theme::BUTTON_SECONDARY, theme::STATUS_MODIFIED)
                .with_size(ComponentSize{pixels(22), pixels(18)}).with_font_size(pixels(11))
                .with_debug_name("commit_comment_badge"));
        div(ctx, mk(metadata.ent(), 1), preset::MetaText(relative_time(commit.authorDate))
            .with_size(ComponentSize{pixels(34), pixels(20)}).with_font_size(pixels(11))
            .with_alignment(TextAlignment::Right).with_text_inset(0.f).with_debug_name("commit_age"));

        // Click -> select this commit
        if (acceptClick && row.ent().get<HasClickListener>().down) {
            auto* r = find_singleton<RepoComponent, ActiveTab>();
            if (r) {
                navigation::click(*r, reading::review(commit.hash), afterhours::input::is_key_pressed(257), reading::ClickRegion::History);
            }
        }
    }

    // ---- Unstaged Changes Dialog (T030) ----
    // Custom modal dialog showing staged/unstaged file lists with three
    // action buttons and a "Remember this choice" checkbox.
    void render_unstaged_dialog(UIContext<InputAction>& ctx,
                                Entity& uiRoot,
                                RepoComponent& repo,
                                CommitEditorComponent& editor) {
        using namespace afterhours;
        using afterhours::ui::h720;

        constexpr int DIALOG_ID = 8000;
        constexpr int CONTENT_LAYER = 1001;

        // Build summary text
        int stagedCount = static_cast<int>(repo.stagedFiles.size());
        int unstagedCount = static_cast<int>(repo.unstagedFiles.size() +
                                              repo.untrackedFiles.size());
        std::string summary = "You have " + std::to_string(stagedCount) +
            " staged and " + std::to_string(unstagedCount) +
            " unstaged changes.\nHow would you like to proceed?";

        // Create modal using modal_impl
        auto modalResult = afterhours::modal::detail::modal_impl(
            ctx, mk(uiRoot, DIALOG_ID), editor.showUnstagedDialog,
            ModalConfig{}
                // Cancel + "Commit Staged Only" + "Stage All & Commit" need
                // ~535px of row; 480 left the last one outside the dialog.
                .with_size(pixels(640), h720(380))
                .with_title("Unstaged Changes")
                .with_show_close_button(false));

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
                    .with_size(ComponentSize{percent(1.0f), h720(16)})
                    .with_padding(Padding{
                        .top = h720(4), .right = pixels(16),
                        .bottom = h720(2), .left = pixels(16)})
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
                        .top = h720(2), .right = pixels(16),
                        .bottom = h720(4), .left = pixels(19)})
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
                        .with_size(ComponentSize{percent(1.0f), h720(20)})
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
                        .with_size(ComponentSize{percent(1.0f), h720(18)})
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
                    .with_size(ComponentSize{percent(1.0f), h720(16)})
                    .with_padding(Padding{
                        .top = h720(8), .right = pixels(16),
                        .bottom = h720(2), .left = pixels(16)})
                    .with_custom_text_color(theme::TEXT_SECONDARY)
                    .with_alignment(TextAlignment::Left)
                    .with_render_layer(CONTENT_LAYER)
                    .with_debug_name("unstaged_label"));

            auto unstagedList = div(ctx, mk(modalEnt, 21),
                ComponentConfig{}
                    .with_size(ComponentSize{percent(1.0f), children()})
                    .with_flex_direction(FlexDirection::Column)
                    .with_padding(Padding{
                        .top = h720(2), .right = pixels(16),
                        .bottom = h720(4), .left = pixels(19)})
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
                        .with_size(ComponentSize{percent(1.0f), h720(20)})
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
                        .with_size(ComponentSize{percent(1.0f), h720(18)})
                        .with_custom_text_color(theme::TEXT_SECONDARY)
                        .with_alignment(TextAlignment::Left)
                        .with_render_layer(CONTENT_LAYER)
                        .with_debug_name("unstaged_more"));
            }
        }

        // -- "Remember this choice" checkbox --
        auto checkboxRow = div(ctx, mk(modalEnt, 30),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), h720(28)})
                .with_flex_direction(FlexDirection::Row)
                .with_align_items(AlignItems::Center)
                .with_padding(Padding{
                    .top = h720(8), .right = pixels(16),
                    .bottom = h720(4), .left = pixels(16)})
                .with_render_layer(CONTENT_LAYER)
                .with_debug_name("remember_row"));

        checkbox(ctx, mk(checkboxRow.ent(), 1),
            editor.rememberChoice,
            ComponentConfig{}
                .with_label("Remember this choice")
                .with_size(ComponentSize{children(), h720(20)})
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
            editor.showUnstagedDialog = false;
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
            commit_workflow::execute_commit(repo, editor, false);
            editor.showUnstagedDialog = false;
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
            commit_workflow::execute_commit(repo, editor, true);
            editor.showUnstagedDialog = false;
        }
    }
};

} // namespace ecs
