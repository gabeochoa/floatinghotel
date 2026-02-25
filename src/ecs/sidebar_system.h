#pragma once

#include <algorithm>
#include <ctime>
#include <string>
#include <vector>

#include "../git/git_commands.h"
#include "../git/git_runner.h"
#include "../settings.h"
#include "../util/git_helpers.h"
#include "ui_imports.h"

#include "../../vendor/afterhours/src/plugins/modal.h"
#include "../../vendor/afterhours/src/plugins/ui/text_input/text_input.h"

namespace ecs {

namespace sidebar_detail {

// Extract just the filename (basename) from a path
inline std::string basename_from_path(const std::string& path) {
    auto slashPos = path.find_last_of('/');
    return (slashPos == std::string::npos) ? path : path.substr(slashPos + 1);
}

// Extract short directory context: just the immediate parent dir
inline std::string dir_from_path(const std::string& path) {
    auto slashPos = path.find_last_of('/');
    if (slashPos == std::string::npos) return "";
    std::string dir = path.substr(0, slashPos);
    // Remove leading "./" or "../" prefixes for cleaner display
    while (dir.size() >= 2 && dir[0] == '.' && (dir[1] == '/' || (dir[1] == '.' && dir.size() >= 3 && dir[2] == '/'))) {
        dir = (dir[1] == '/') ? dir.substr(2) : dir.substr(3);
    }
    return dir;
}

// Legacy: combined "dir/file" format
inline std::string filename_from_path(const std::string& path, size_t maxChars = 28) {
    auto slashPos = path.find_last_of('/');
    std::string name = (slashPos == std::string::npos) ? path : path.substr(slashPos + 1);
    if (slashPos != std::string::npos && slashPos > 0) {
        auto prevSlash = path.find_last_of('/', slashPos - 1);
        std::string parent = (prevSlash == std::string::npos)
            ? path.substr(0, slashPos)
            : path.substr(prevSlash + 1, slashPos - prevSlash - 1);
        std::string full = parent + "/" + name;
        if (full.size() <= maxChars) return full;
    }
    if (name.size() > maxChars) {
        return name.substr(0, maxChars - 1) + "\xe2\x80\xa6";
    }
    return name;
}

// Format change stats string: "+N -N", "+N", or "-N"
inline std::string format_stats(int additions, int deletions) {
    std::string s;
    if (additions > 0) {
        s += "+" + std::to_string(additions);
    }
    if (deletions > 0) {
        if (!s.empty()) s += " ";
        s += "-" + std::to_string(deletions);
    }
    return s;
}

// Color for a status character — uses theme colors for consistency
inline afterhours::Color status_color(char status) {
    return theme::statusColor(status);
}

} // namespace sidebar_detail

// Commit log helpers now live in src/util/git_helpers.h
namespace commit_log_detail = git_helpers;

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
        if (!stageResult.success()) return false;
    }

    std::string message = build_message(editor.subject, editor.body);
    if (message.empty()) {
        message = "Update"; // Fallback if no message provided
    }

    auto result = git::git_commit(repo.repoPath, message);
    if (result.success()) {
        editor.subject.clear();
        editor.body.clear();
        editor.isVisible = false;
        repo.refreshRequested = true;
        return true;
    }
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
        auto layoutEntities = afterhours::EntityQuery({.force_merge = true})
                                  .whereHasComponent<LayoutComponent>()
                                  .gen();
        if (layoutEntities.empty()) return;
        auto& layout = layoutEntities[0].get().get<LayoutComponent>();

        if (!layout.sidebarVisible) return;

        auto repoEntities = afterhours::EntityQuery({.force_merge = true})
                                .whereHasComponent<RepoComponent>()
                                .whereHasComponent<ActiveTab>()
                                .gen();

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

        float sidebarW = layout.sidebar.width;
        sidebarPixelWidth_ = sidebarW;  // Set early for all child rendering

        // === Changes / Refs mode toggle tabs ===
        render_sidebar_mode_tabs(ctx, sidebarRoot.ent(), layout);

        // === Changed Files / Refs section (flow child of sidebar, NOT absolute) ===
        // NOTE: Use explicit pixels for width (not percent) to avoid framework bug
        // where percent(1.0f) resolves to screen width in absolute-positioned parents.
        float sh_for_tab = static_cast<float>(afterhours::graphics::get_screen_height());
        float tabH = resolve_to_pixels(h720(28.0f), sh_for_tab);
        float filesH = layout.sidebarFiles.height - tabH;
        if (filesH < 20.0f) filesH = 20.0f;
        auto filesBg = div(ctx, mk(sidebarRoot.ent(), 2100),
            preset::ScrollPanel()
                .with_size(ComponentSize{pixels(sidebarW), pixels(filesH)})
                .with_debug_name("sidebar_files"));

        if (layout.sidebarMode == LayoutComponent::SidebarMode::Changes) {
            // Render file list directly into filesBg (no intermediate container)
            // to avoid framework bug where nested container children render wrong
            if (!repoEntities.empty()) {
                auto& repo = repoEntities[0].get().get<RepoComponent>();
                render_file_list(ctx, filesBg.ent(), repo);
            } else {
                div(ctx, mk(filesBg.ent(), 2150),
                    ComponentConfig{}
                        .with_label("No repository open")
                        .with_size(ComponentSize{percent(1.0f), h720(32)})
                        .with_padding(Padding{
                            .top = h720(16), .right = w1280(8),
                            .bottom = h720(8), .left = w1280(8)})
                        .with_custom_text_color(afterhours::Color{90, 90, 90, 255})
                        .with_alignment(TextAlignment::Center)
                        .with_roundness(0.0f)
                        .with_debug_name("no_repo"));
            }
        } else {
            // === Refs view (T031) ===
            if (!repoEntities.empty()) {
                auto& repo = repoEntities[0].get().get<RepoComponent>();
                render_refs_view(ctx, filesBg.ent(), repo, layout);
            } else {
                div(ctx, mk(filesBg.ent(), 2150),
                    ComponentConfig{}
                        .with_label("No repository open")
                        .with_size(ComponentSize{percent(1.0f), h720(32)})
                        .with_padding(Padding{
                            .top = h720(16), .right = w1280(8),
                            .bottom = h720(8), .left = w1280(8)})
                        .with_custom_text_color(afterhours::Color{90, 90, 90, 255})
                        .with_alignment(TextAlignment::Center)
                        .with_roundness(0.0f)
                        .with_debug_name("no_repo_refs"));
            }
        }

        // === Horizontal divider between files and commit log (flow child) ===
        auto hDivider = div(ctx, mk(sidebarRoot.ent(), 2200),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(sidebarW), pixels(1)})
                .with_custom_background(theme::SIDEBAR_DIVIDER)
                .with_cursor(afterhours::ui::CursorType::ResizeV)
                .with_roundness(0.0f)
                .with_debug_name("sidebar_h_divider"));

        // Make horizontal divider draggable (adjusts commit log ratio)
        hDivider.ent().addComponentIfMissing<HasDragListener>(
            [](Entity& /*e*/) {});
        auto& hDrag = hDivider.ent().get<HasDragListener>();
        if (hDrag.down) {
            auto mousePos = afterhours::graphics::get_mouse_position();
            float mouseY = static_cast<float>(mousePos.y);
            float contentTop = layout.sidebar.y;
            float contentH = layout.sidebar.height;
            float ratio = (mouseY - contentTop) / contentH;
            float newCommitRatio = 1.0f - ratio;
            newCommitRatio = std::clamp(newCommitRatio, 0.2f, 0.8f);

            auto lEntities = afterhours::EntityQuery({.force_merge = true})
                                 .whereHasComponent<LayoutComponent>()
                                 .gen();
            if (!lEntities.empty()) {
                lEntities[0].get().get<LayoutComponent>().commitLogRatio = newCommitRatio;
            }
        }

        // === Commit Log section (flow child of sidebar, fills remaining space) ===
        float commitsH = layout.sidebarLog.height;
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
            size_t commitCount = 0;
            if (!repoEntities.empty()) {
                commitCount = repoEntities[0].get().get<RepoComponent>().commitLog.size();
            }
            std::string logHeaderText = "\xe2\x96\xbe Commits  " + std::to_string(commitCount);
            div(ctx, mk(logBg.ent(), 2310),
                preset::SectionHeader(logHeaderText)
                    .with_size(ComponentSize{logW, children()})
                    .with_debug_name("log_header"));
        }

        // === Scrollable commit log entries ===
        float sh2 = static_cast<float>(afterhours::graphics::get_screen_height());
        float logHeaderConsumed = resolve_to_pixels(h720(28.0f), sh2);
        float logScrollH = layout.sidebarLog.height - logHeaderConsumed;
        if (logScrollH < 20.0f) logScrollH = 20.0f;

        auto logScroll = div(ctx, mk(logBg.ent(), 2320),
            preset::ScrollPanel()
                .with_size(ComponentSize{logW, pixels(logScrollH)})
                .with_debug_name("commit_log_scroll"));

        if (!repoEntities.empty()) {
            auto& repo = repoEntities[0].get().get<RepoComponent>();
            render_commit_log_entries(ctx, logScroll.ent(), repo);
        } else {
            div(ctx, mk(logScroll.ent(), 0),
                ComponentConfig{}
                    .with_label("No repository open")
                    .with_size(ComponentSize{percent(1.0f), h720(32)})
                    .with_padding(Padding{
                        .top = h720(16), .right = w1280(8),
                        .bottom = h720(8), .left = w1280(8)})
                    .with_custom_text_color(afterhours::Color{90, 90, 90, 255})
                    .with_alignment(TextAlignment::Center)
                    .with_roundness(0.0f)
                    .with_debug_name("no_repo_log"));
        }

        // === Commit workflow + Unstaged Changes Dialog (T030) ===
        if (!repoEntities.empty()) {
            auto& repo = repoEntities[0].get().get<RepoComponent>();

            auto editorEntities = afterhours::EntityQuery({.force_merge = true})
                                      .whereHasComponent<CommitEditorComponent>()
                                      .whereHasComponent<ActiveTab>()
                                      .gen();
            if (!editorEntities.empty()) {
                auto& editor = editorEntities[0].get().get<CommitEditorComponent>();

                // Process commit request
                if (editor.commitRequested) {
                    commit_workflow::handle_commit_request(repo, editor);
                }

                // Render unstaged changes dialog
                render_unstaged_dialog(ctx, uiRoot, repo, editor);
            }

            // === Branch dialogs (T031) ===
            render_new_branch_dialog(ctx, uiRoot, repo);
            render_delete_branch_dialog(ctx, uiRoot, repo);
            render_force_delete_dialog(ctx, uiRoot, repo);
        }
    }

private:
    // ---- Sidebar mode toggle (T031) ----
    // Render the Changes/Refs toggle tabs at the top of the sidebar
    void render_sidebar_mode_tabs(UIContext<InputAction>& ctx,
                                   Entity& parent,
                                   LayoutComponent& layout) {
        constexpr float TAB_HEIGHT = 28.0f;
        constexpr float TAB_HPAD = 14.0f;

        auto tabRowW = sidebarPixelWidth_ > 0 ? pixels(sidebarPixelWidth_) : percent(1.0f);
        auto tabRow = div(ctx, mk(parent, 2090),
            ComponentConfig{}
                .with_size(ComponentSize{tabRowW, h720(TAB_HEIGHT)})
                .with_flex_direction(FlexDirection::Row)
                .with_align_items(AlignItems::Center)
                .with_padding(Padding{
                    .top = h720(2), .right = w1280(8),
                    .bottom = h720(2), .left = w1280(8)})
                .with_custom_background(theme::SIDEBAR_BG)
                .with_roundness(0.0f)
                .with_debug_name("sidebar_mode_tabs"));

        auto makeTab = [&](int id, const std::string& label,
                           LayoutComponent::SidebarMode mode) {
            bool active = (layout.sidebarMode == mode);

            auto config = preset::Button(label)
                .with_size(ComponentSize{children(), h720(TAB_HEIGHT - 6)})
                .with_padding(Padding{
                    .top = h720(2), .right = w1280(TAB_HPAD),
                    .bottom = h720(2), .left = w1280(TAB_HPAD)})
                .with_margin(Margin{
                    .top = {}, .bottom = {},
                    .left = {}, .right = w1280(4)})
                .with_font_size(FontSize::Large)
                .with_debug_name("mode_" + label);
            if (!active) {
                config = config.with_custom_background(theme::BUTTON_SECONDARY)
                               .with_custom_text_color(theme::TEXT_PRIMARY);
            }

            auto result = button(ctx, mk(tabRow.ent(), id), config);

            if (result) {
                auto lEntities = afterhours::EntityQuery({.force_merge = true})
                                     .whereHasComponent<LayoutComponent>()
                                     .gen();
                if (!lEntities.empty()) {
                    lEntities[0].get().get<LayoutComponent>().sidebarMode = mode;
                }
            }
        };

        makeTab(2091, "Changes", LayoutComponent::SidebarMode::Changes);
        makeTab(2092, "Refs", LayoutComponent::SidebarMode::Refs);
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
                    .top = h720(4), .right = w1280(8),
                    .bottom = h720(4), .left = w1280(8)})
                .with_custom_background(theme::SIDEBAR_BG)
                .with_roundness(0.0f)
                .with_debug_name("refs_header"));

        std::string branchLabel = "\xe2\x96\xbe Branches  " +
            std::to_string(repo.branches.size());
        div(ctx, mk(headerRow.ent(), 1),
            preset::SectionHeader(branchLabel)
                .with_size(ComponentSize{percent(1.0f), children()})
                .with_debug_name("branches_label"));

        // "+ New" button
        auto newBranchBtn = button(ctx, mk(headerRow.ent(), 2),
            preset::Button("+ New")
                .with_size(ComponentSize{children(), h720(18)})
                .with_padding(Padding{
                    .top = h720(2), .right = w1280(8),
                    .bottom = h720(2), .left = w1280(8)})
                .with_font_size(h720(16))
                .with_debug_name("new_branch_btn"));

        if (newBranchBtn) {
            repo.showNewBranchDialog = true;
            repo.newBranchName.clear();
        }

        // Scrollable branch list
        float shR = static_cast<float>(afterhours::graphics::get_screen_height());
        float refsHeaderConsumed = resolve_to_pixels(h720(24.0f + 28.0f), shR);
        float refsScrollH = layout.sidebarFiles.height - refsHeaderConsumed;
        if (refsScrollH < 20.0f) refsScrollH = 20.0f;

        auto scrollArea = div(ctx, mk(parent, 2170),
            preset::ScrollPanel()
                .with_size(ComponentSize{percent(1.0f), pixels(refsScrollH)})
                .with_debug_name("refs_scroll"));

        if (repo.branches.empty()) {
            div(ctx, mk(scrollArea.ent(), 0),
                ComponentConfig{}
                    .with_label("No branches found")
                    .with_size(ComponentSize{percent(1.0f), h720(32)})
                    .with_padding(Padding{
                        .top = h720(16), .right = w1280(8),
                        .bottom = h720(8), .left = w1280(8)})
                    .with_custom_text_color(afterhours::Color{90, 90, 90, 255})
                    .with_alignment(TextAlignment::Center)
                    .with_roundness(0.0f)
                    .with_debug_name("no_branches"));
            return;
        }

        // Render each branch row
        for (int i = 0; i < static_cast<int>(repo.branches.size()); ++i) {
            render_branch_row(ctx, scrollArea.ent(), i, repo.branches[i], repo);
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
                    .top = h720(0), .right = w1280(8),
                    .bottom = h720(0), .left = w1280(0)})
                .with_roundness(0.0f)
                .with_debug_name("branch_row"));

        rowResult.ent().addComponentIfMissing<HasClickListener>([](Entity&){});

        // Click -> checkout this branch
        if (rowResult.ent().get<HasClickListener>().down && !isCurrent) {
            auto result = git::checkout_branch(repo.repoPath, branch.name);
            if (result.success()) {
                repo.refreshRequested = true;
            }
        }

        // Current branch indicator (green left border)
        if (isCurrent) {
            div(ctx, mk(rowResult.ent(), 1),
                ComponentConfig{}
                    .with_size(ComponentSize{w1280(3), h720(ROW_H)})
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
                .with_size(ComponentSize{w1280(20), h720(16)})
                .with_padding(Padding{
                    .top = h720(1), .right = w1280(3),
                    .bottom = h720(1), .left = w1280(3)})
                .with_margin(Margin{
                    .top = {}, .bottom = {},
                    .left = w1280(isCurrent ? 5.0f : 8.0f),
                    .right = w1280(6)})
                .with_debug_name("branch_badge"));

        // Branch name
        auto nameColor = isCurrent ? afterhours::Color{255, 255, 255, 255}
                                   : theme::TEXT_PRIMARY;
        div(ctx, mk(rowResult.ent(), 3),
            ComponentConfig{}
                .with_label(branch.name)
                .with_size(ComponentSize{percent(1.0f), h720(ROW_H)})
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
                        .top = h720(0), .right = w1280(4),
                        .bottom = h720(0), .left = w1280(4)})
                    .with_custom_text_color(theme::TEXT_SECONDARY)
                    .with_font_size(FontSize::Medium)
                    .with_alignment(TextAlignment::Right)
                    .with_roundness(0.0f)
                    .with_debug_name("branch_tracking"));
        }

        // Delete button (only for non-current branches)
        if (!isCurrent) {
            auto deleteBtn = button(ctx, mk(rowResult.ent(), 5),
                preset::Button("x")
                    .with_size(ComponentSize{w1280(20), h720(20)})
                    .with_custom_background(theme::BUTTON_SECONDARY)
                    .with_custom_text_color(theme::STATUS_DELETED)
                    .with_debug_name("delete_branch_btn"));

            if (deleteBtn) {
                repo.deleteBranchName = branch.name;
                repo.showDeleteBranchDialog = true;
            }
        }

        // Row separator
        div(ctx, mk(parent, 2200 + index * 10 + 1),
            preset::RowSeparator()
                .with_size(ComponentSize{percent(1.0f), h720(1)})
                .with_debug_name("branch_sep"));
    }

    // ---- New Branch dialog (T031) ----
    void render_new_branch_dialog(UIContext<InputAction>& ctx,
                                   Entity& uiRoot,
                                   RepoComponent& repo) {
        if (!repo.showNewBranchDialog) return;

        using namespace afterhours;
        using afterhours::ui::h720;

        constexpr int MODAL_ID = 8100;
        constexpr int CONTENT_LAYER = 1001;

        auto modalResult = afterhours::modal::detail::modal_impl(
            ctx, mk(uiRoot, MODAL_ID), repo.showNewBranchDialog,
            ModalConfig{}
                .with_size(w1280(380), h720(180))
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
                    .top = h720(8), .right = w1280(16),
                    .bottom = h720(4), .left = w1280(16)})
                .with_custom_text_color(theme::TEXT_PRIMARY)
                .with_alignment(TextAlignment::Left)
                .with_render_layer(CONTENT_LAYER)
                .with_debug_name("new_branch_label"));

        // Text input
        afterhours::text_input::text_input(ctx, mk(modalEnt, 2),
            repo.newBranchName,
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), h720(32)})
                .with_padding(Padding{
                    .top = h720(0), .right = w1280(16),
                    .bottom = h720(0), .left = w1280(16)})
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
                    .left = {}, .right = w1280(8)})
                .with_custom_background(theme::BUTTON_SECONDARY)
                .with_custom_text_color(theme::TEXT_PRIMARY)
                .with_render_layer(CONTENT_LAYER)
                .with_debug_name("cancel_new_branch"))) {
            repo.showNewBranchDialog = false;
            repo.newBranchName.clear();
        }

        // Create
        bool canCreate = !repo.newBranchName.empty();
        if (button(ctx, mk(btnRow.ent(), 2),
            preset::Button("Create", canCreate)
                .with_render_layer(CONTENT_LAYER)
                .with_debug_name("create_branch_btn"))) {
            if (canCreate) {
                auto result = git::create_branch(repo.repoPath, repo.newBranchName);
                if (result.success()) {
                    repo.refreshRequested = true;
                }
                repo.showNewBranchDialog = false;
                repo.newBranchName.clear();
            }
        }
    }

    // ---- Delete Branch confirmation dialog (T031) ----
    void render_delete_branch_dialog(UIContext<InputAction>& ctx,
                                      Entity& uiRoot,
                                      RepoComponent& repo) {
        if (!repo.showDeleteBranchDialog) return;

        using namespace afterhours;
        using afterhours::ui::h720;

        constexpr int MODAL_ID = 8200;
        constexpr int CONTENT_LAYER = 1001;

        std::string message = "Delete branch '" + repo.deleteBranchName + "'?\n"
            "This will delete the local branch. If it has unmerged\n"
            "changes, you will be prompted to force delete.";

        auto modalResult = afterhours::modal::detail::modal_impl(
            ctx, mk(uiRoot, MODAL_ID), repo.showDeleteBranchDialog,
            ModalConfig{}
                .with_size(w1280(420), h720(180))
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
                    .left = {}, .right = w1280(8)})
                .with_custom_background(theme::BUTTON_SECONDARY)
                .with_custom_text_color(theme::TEXT_PRIMARY)
                .with_render_layer(CONTENT_LAYER)
                .with_debug_name("cancel_delete"))) {
            repo.showDeleteBranchDialog = false;
            repo.deleteBranchName.clear();
        }

        // Delete (red)
        if (button(ctx, mk(btnRow.ent(), 2),
            preset::Button("Delete")
                .with_custom_background(theme::STATUS_DELETED)
                .with_render_layer(CONTENT_LAYER)
                .with_debug_name("confirm_delete"))) {
            auto result = git::delete_branch(repo.repoPath,
                                              repo.deleteBranchName, false);
            if (result.success()) {
                repo.refreshRequested = true;
                repo.showDeleteBranchDialog = false;
                repo.deleteBranchName.clear();
            } else {
                // Safe delete failed (unmerged) — prompt for force delete
                repo.showDeleteBranchDialog = false;
                repo.showForceDeleteDialog = true;
            }
        }
    }

    // ---- Force Delete Branch dialog (T031) ----
    void render_force_delete_dialog(UIContext<InputAction>& ctx,
                                     Entity& uiRoot,
                                     RepoComponent& repo) {
        if (!repo.showForceDeleteDialog) return;

        using namespace afterhours;
        using afterhours::ui::h720;

        constexpr int MODAL_ID = 8300;
        constexpr int CONTENT_LAYER = 1001;

        std::string message = "Branch '" + repo.deleteBranchName +
            "' has unmerged changes.\n\n"
            "Force delete will permanently lose these changes.\n"
            "Are you sure?";

        auto modalResult = afterhours::modal::detail::modal_impl(
            ctx, mk(uiRoot, MODAL_ID), repo.showForceDeleteDialog,
            ModalConfig{}
                .with_size(w1280(420), h720(200))
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
                    .left = {}, .right = w1280(8)})
                .with_custom_background(theme::BUTTON_SECONDARY)
                .with_custom_text_color(theme::TEXT_PRIMARY)
                .with_render_layer(CONTENT_LAYER)
                .with_debug_name("cancel_force_delete"))) {
            repo.showForceDeleteDialog = false;
            repo.deleteBranchName.clear();
        }

        // Force Delete (red)
        if (button(ctx, mk(btnRow.ent(), 2),
            preset::Button("Force Delete")
                .with_custom_background(theme::STATUS_DELETED)
                .with_render_layer(CONTENT_LAYER)
                .with_debug_name("force_delete_btn"))) {
            auto result = git::delete_branch(repo.repoPath,
                                              repo.deleteBranchName, true);
            if (result.success()) {
                repo.refreshRequested = true;
            }
            repo.showForceDeleteDialog = false;
            repo.deleteBranchName.clear();
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
                    .top = h720(2), .right = w1280(10),
                    .bottom = h720(2), .left = w1280(10)})
                .with_custom_background(theme::SIDEBAR_BG)
                .with_roundness(0.0f)
                .with_debug_name("view_mode_tabs"));

        auto makeTab = [&](int id, const std::string& label,
                           LayoutComponent::FileViewMode mode) {
            bool active = (layout.fileViewMode == mode);

            auto config = preset::Button(label)
                .with_size(ComponentSize{children(), h720(TAB_HEIGHT - 6)})
                .with_padding(Padding{
                    .top = h720(2), .right = w1280(TAB_HPAD),
                    .bottom = h720(2), .left = w1280(TAB_HPAD)})
                .with_margin(Margin{
                    .top = {}, .bottom = {},
                    .left = {}, .right = w1280(4)})
                .with_debug_name("tab_" + label);
            if (!active) {
                config = config.with_custom_background(theme::BUTTON_SECONDARY)
                               .with_custom_text_color(theme::TEXT_PRIMARY);
            }

            auto result = button(ctx, mk(tabRow.ent(), id), config);

            if (result) {
                auto lEntities = afterhours::EntityQuery({.force_merge = true})
                                     .whereHasComponent<LayoutComponent>()
                                     .gen();
                if (!lEntities.empty()) {
                    lEntities[0].get().get<LayoutComponent>().fileViewMode = mode;
                }
            }
        };

        makeTab(2121, "Changed", LayoutComponent::FileViewMode::Flat);
        makeTab(2122, "Tree", LayoutComponent::FileViewMode::Tree);
        makeTab(2123, "All", LayoutComponent::FileViewMode::All);
    }

    // Render the file list with Staged, Changes, and Untracked sections
    // parentWidth: explicit pixel width to avoid percent resolution bug
    float sidebarPixelWidth_ = 0; // Set before rendering
    void render_file_list(UIContext<InputAction>& ctx,
                          Entity& scrollParent,
                          RepoComponent& repo) {
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
                            .top = h720(20), .right = w1280(8),
                            .bottom = h720(4), .left = w1280(8)})
                        .with_custom_text_color(theme::TEXT_SECONDARY)
                        .with_alignment(TextAlignment::Center)
                        .with_roundness(0.0f)
                        .with_debug_name("loading_spinner"));
            } else {
                div(ctx, mk(scrollParent, 2500),
                    preset::EmptyStateText("\xe2\x9c\x93 No changes")
                        .with_size(ComponentSize{percent(1.0f), h720(28)})
                        .with_padding(Padding{
                            .top = h720(20), .right = w1280(8),
                            .bottom = h720(4), .left = w1280(8)})
                        .with_debug_name("empty_changes"));

                div(ctx, mk(scrollParent, 2501),
                    ComponentConfig{}
                        .with_label("Working tree clean")
                        .with_size(ComponentSize{percent(1.0f), h720(22)})
                        .with_padding(Padding{
                            .top = h720(0), .right = w1280(8),
                            .bottom = h720(8), .left = w1280(8)})
                        .with_custom_text_color(afterhours::Color{90, 90, 90, 255})
                        .with_alignment(TextAlignment::Center)
                        .with_roundness(0.0f)
                        .with_debug_name("empty_clean"));
            }
            return;
        }

        int nextId = 2600;
        bool firstSection = true;

        // === Staged Changes section ===
        if (!repo.stagedFiles.empty()) {
            render_section_header(ctx, scrollParent, nextId++,
                "Staged Changes", repo.stagedFiles.size(), firstSection);
            firstSection = false;

            for (int i = 0; i < static_cast<int>(repo.stagedFiles.size()); ++i) {
                render_file_row(ctx, scrollParent, nextId++,
                    repo.stagedFiles[i], repo, true);
            }
        }

        // === Changes (unstaged) section ===
        if (!repo.unstagedFiles.empty()) {
            render_section_header(ctx, scrollParent, nextId++,
                "Unstaged Changes", repo.unstagedFiles.size(), firstSection);
            firstSection = false;

            for (int i = 0; i < static_cast<int>(repo.unstagedFiles.size()); ++i) {
                render_file_row(ctx, scrollParent, nextId++,
                    repo.unstagedFiles[i], repo, false);
            }
        }

        // === Untracked section ===
        if (!repo.untrackedFiles.empty()) {
            render_section_header(ctx, scrollParent, nextId++,
                "Untracked", repo.untrackedFiles.size(), firstSection);
            firstSection = false;

            for (int i = 0; i < static_cast<int>(repo.untrackedFiles.size()); ++i) {
                render_untracked_row(ctx, scrollParent, nextId++,
                    repo.untrackedFiles[i], repo);
            }
        }
    }

    // Render a section header: "▾ STAGED CHANGES  1"
    // isFirst: true for the very first section (no top margin needed)
    void render_section_header(UIContext<InputAction>& ctx,
                                Entity& parent, int id,
                                const std::string& label, size_t count,
                                bool isFirst = false) {
        auto secWidth = sidebarPixelWidth_ > 0 ? pixels(sidebarPixelWidth_) : percent(1.0f);

        std::string headerText = "\xe2\x96\xbe " + label +
                                 "  " + std::to_string(count);
        auto config = preset::SectionHeader(headerText)
                .with_size(ComponentSize{secWidth, children()})
                .with_debug_name("section_hdr");
        if (!isFirst) {
            config = config.with_margin(Margin{.top = pixels(6)});
        }
        div(ctx, mk(parent, id), config);
    }

    // Render a file row with Row flex: [filename (expand)] [dir (gray)] [status (colored)]
    void render_file_row(UIContext<InputAction>& ctx,
                         Entity& parent, int id,
                         const FileStatus& file,
                         RepoComponent& repo, bool staged) {
        bool selected = (file.path == repo.selectedFilePath);
        char statusChar = staged ? file.indexStatus : file.workTreeStatus;
        if (statusChar == ' ' || statusChar == '\0') {
            statusChar = staged ? 'A' : 'M';
        }

        constexpr float ROW_H = static_cast<float>(theme::layout::FILE_ROW_HEIGHT);

        std::string fname = sidebar_detail::basename_from_path(file.path);
        std::string dir = sidebar_detail::dir_from_path(file.path);
        std::string statusStr(1, statusChar);

        auto rowWidth = sidebarPixelWidth_ > 0 ? pixels(sidebarPixelWidth_) : percent(1.0f);

        // Row container
        auto row = div(ctx, mk(parent, id),
            preset::SelectableRow(selected)
                .with_size(ComponentSize{rowWidth, h720(ROW_H)})
                .with_debug_name("file_row"));

        row.ent().addComponentIfMissing<HasClickListener>([](Entity&){});

        auto textCol = selected ? afterhours::Color{255, 255, 255, 255}
                                : theme::TEXT_PRIMARY;

        // Filename: sidebarW - padL(10) - padR(4) - gap(4) - status(20)
        float nameW = std::max(sidebarPixelWidth_ - 38.0f, 30.0f);

        std::string label = fname;
        if (!dir.empty()) label += "  " + dir;

        div(ctx, mk(row.ent(), 1),
            preset::BodyText(label)
                .with_size(ComponentSize{pixels(nameW), children()})
                .with_custom_text_color(textCol)
                .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
                .with_debug_name("file_name"));

        // Status letter (right-aligned, vertically centered by AlignItems::Center)
        auto statusCol = sidebar_detail::status_color(statusChar);
        div(ctx, mk(row.ent(), 3),
            preset::MetaText(statusStr)
                .with_size(ComponentSize{pixels(20), children()})
                .with_custom_text_color(statusCol)
                .with_alignment(TextAlignment::Right)
                .with_debug_name("file_status"));

        // Click -> select this file
        if (row.ent().get<HasClickListener>().down) {
            auto rEntities = afterhours::EntityQuery({.force_merge = true})
                                 .whereHasComponent<RepoComponent>()
                                 .whereHasComponent<ActiveTab>()
                                 .gen();
            if (!rEntities.empty()) {
                auto& r = rEntities[0].get().get<RepoComponent>();
                r.selectedFilePath = file.path;
                r.selectedCommitHash.clear();
            }
        }
    }

    // Render a row for an untracked file with Row flex: [filename (expand)] [dir (gray)] [U (gray)]
    void render_untracked_row(UIContext<InputAction>& ctx,
                               Entity& parent, int id,
                               const std::string& path,
                               RepoComponent& repo) {
        bool selected = (path == repo.selectedFilePath);
        constexpr float ROW_H = static_cast<float>(theme::layout::FILE_ROW_HEIGHT);

        std::string fname = sidebar_detail::basename_from_path(path);
        std::string dir = sidebar_detail::dir_from_path(path);

        auto rowWidth = sidebarPixelWidth_ > 0 ? pixels(sidebarPixelWidth_) : percent(1.0f);

        // Row container
        auto row = div(ctx, mk(parent, id),
            preset::SelectableRow(selected)
                .with_size(ComponentSize{rowWidth, h720(ROW_H)})
                .with_debug_name("untracked_row"));

        row.ent().addComponentIfMissing<HasClickListener>([](Entity&){});

        auto textCol = selected ? afterhours::Color{255, 255, 255, 255}
                                : theme::TEXT_PRIMARY;

        float nameW = std::max(sidebarPixelWidth_ - 38.0f, 30.0f);

        std::string label = fname;
        if (!dir.empty()) label += "  " + dir;

        div(ctx, mk(row.ent(), 1),
            preset::BodyText(label)
                .with_size(ComponentSize{pixels(nameW), children()})
                .with_custom_text_color(textCol)
                .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
                .with_debug_name("file_name"));

        // Status letter "U" (right-aligned, vertically centered by AlignItems::Center)
        div(ctx, mk(row.ent(), 3),
            preset::MetaText("U")
                .with_size(ComponentSize{pixels(20), children()})
                .with_custom_text_color(sidebar_detail::status_color('U'))
                .with_alignment(TextAlignment::Right)
                .with_debug_name("file_status"));

        if (row.ent().get<HasClickListener>().down) {
            auto rEntities = afterhours::EntityQuery({.force_merge = true})
                                 .whereHasComponent<RepoComponent>()
                                 .whereHasComponent<ActiveTab>()
                                 .gen();
            if (!rEntities.empty()) {
                auto& r = rEntities[0].get().get<RepoComponent>();
                r.selectedFilePath = path;
                r.selectedCommitHash.clear();
            }
        }
    }

    // ---- Commit log rendering (T021) ----

    // Render all commit log entries in a scrollable list
    void render_commit_log_entries(UIContext<InputAction>& ctx,
                                   Entity& scrollParent,
                                   RepoComponent& repo) {
        if (repo.commitLog.empty()) {
            div(ctx, mk(scrollParent, 0),
                preset::EmptyStateText("No commits yet")
                    .with_size(ComponentSize{percent(1.0f), h720(32)})
                    .with_padding(Padding{
                        .top = h720(16), .right = w1280(8),
                        .bottom = h720(8), .left = w1280(8)})
                    .with_debug_name("empty_log"));
            return;
        }

        constexpr int MAX_VISIBLE = 500;
        int count = std::min(static_cast<int>(repo.commitLog.size()), MAX_VISIBLE);

        bool multipleCommits = (count > 1);
        for (int i = 0; i < count; ++i) {
            render_commit_row(ctx, scrollParent, i, repo.commitLog[i], repo,
                              multipleCommits);
        }

        // Lazy load indicator at bottom
        if (repo.commitLogHasMore) {
            div(ctx, mk(scrollParent, 9990),
                ComponentConfig{}
                    .with_label("\xe2\x97\x8b Loading more...")
                    .with_size(ComponentSize{percent(1.0f), h720(20)})
                    .with_padding(Padding{
                        .top = h720(3), .right = w1280(8),
                        .bottom = h720(3), .left = w1280(8)})
                    .with_custom_text_color(afterhours::Color{90, 90, 90, 255})
                    .with_font_size(FontSize::Medium)
                    .with_alignment(TextAlignment::Center)
                    .with_roundness(0.0f)
                    .with_debug_name("lazy_load"));
        }
    }

    // Render a single commit row: [graph_col] [subject] [badge pills] [hash]
    // showGraphLine controls whether the vertical connecting line is drawn
    void render_commit_row(UIContext<InputAction>& ctx,
                           Entity& parent, int index,
                           const CommitEntry& commit,
                           RepoComponent& repo,
                           bool showGraphLine = true) {
        bool selected = (commit.hash == repo.selectedCommitHash);
        constexpr float ROW_H = static_cast<float>(theme::layout::COMMIT_ROW_HEIGHT);

        int baseId = index * 2 + 10;
        float sidebarW = sidebarPixelWidth_ > 0 ? sidebarPixelWidth_ : 300.0f;

        auto badges = commit_log_detail::parse_decorations(commit.decorations);

        constexpr float DOT_SIZE = 8.0f;
        constexpr float LINE_W = 2.0f;
        constexpr float GRAPH_COL_W = 22.0f;

        auto row = div(ctx, mk(parent, baseId),
            preset::SelectableRow(selected)
                .with_size(ComponentSize{pixels(sidebarW), h720(ROW_H)})
                .with_padding(Padding{
                    .top = pixels(0), .right = pixels(4),
                    .bottom = pixels(0), .left = pixels(0)})
                .with_gap(pixels(4))
                .with_debug_name("commit_row"));

        row.ent().addComponentIfMissing<HasClickListener>([](Entity&){});

        float shG = static_cast<float>(
            afterhours::graphics::get_screen_height());
        float rowPx = resolve_to_pixels(h720(ROW_H), shG);
        if (rowPx < 1.0f) rowPx = 26.0f;

        // Graph wrapper: 22px wide container for line and dot.
        auto graphWrap = div(ctx, mk(row.ent(), 1),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(GRAPH_COL_W), pixels(rowPx)})
                .with_roundness(0.0f)
                .with_debug_name("graph_wrap"));

        // Line: 0-width div with border-left, absolutely positioned
        // so the border is centered on the dot center (GRAPH_COL_W/2).
        // Border draws LINE_W px right from element's left edge,
        // so left edge = center - LINE_W/2.
        if (showGraphLine) {
            float lineX = (GRAPH_COL_W - LINE_W) / 2.0f;
            div(ctx, mk(graphWrap.ent(), 1),
                ComponentConfig{}
                    .with_size(ComponentSize{pixels(0), pixels(rowPx)})
                    .with_absolute_position(lineX, 0.0f)
                    .with_border_left(theme::GRAPH_LINE, pixels(LINE_W))
                    .with_roundness(0.0f)
                    .with_debug_name("graph_line"));
        }

        // Dot: absolute, centered both ways
        float dotX = (GRAPH_COL_W - DOT_SIZE) / 2.0f;
        float dotY = (rowPx - DOT_SIZE) / 2.0f;
        div(ctx, mk(graphWrap.ent(), 2),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(DOT_SIZE), pixels(DOT_SIZE)})
                .with_absolute_position(dotX, dotY)
                .with_custom_background(theme::GRAPH_DOT)
                .with_roundness(1.0f)
                .with_render_layer(1)
                .with_debug_name("commit_dot"));

        constexpr float HASH_W = 46.0f;
        constexpr float HASH_AREA = 50.0f;
        constexpr float BADGE_EST_W = 46.0f;

        // Show at most one badge in the row to maximise subject readability.
        // Pick the most useful one: HEAD > local branch > tag > remote.
        const commit_log_detail::Decoration* bestBadge = nullptr;
        for (auto& b : badges) {
            if (!bestBadge) { bestBadge = &b; continue; }
            auto rank = [](commit_log_detail::DecorationType t) -> int {
                switch (t) {
                    case commit_log_detail::DecorationType::Head:         return 4;
                    case commit_log_detail::DecorationType::LocalBranch:  return 3;
                    case commit_log_detail::DecorationType::Tag:          return 2;
                    case commit_log_detail::DecorationType::RemoteBranch: return 1;
                    default: return 0;
                }
            };
            if (rank(b.type) > rank(bestBadge->type)) bestBadge = &b;
        }

        bool hasBadge = (bestBadge != nullptr);
        float fixedW = GRAPH_COL_W
                     + (hasBadge ? BADGE_EST_W + 4.0f : 0.0f)
                     + 4.0f
                     + HASH_AREA;
        float subjectW = sidebarW - 4.0f - fixedW;
        if (subjectW < 30.0f) subjectW = 30.0f;

        auto textCol = selected ? afterhours::Color{255, 255, 255, 255}
                                : theme::TEXT_PRIMARY;
        div(ctx, mk(row.ent(), 2),
            preset::BodyText(commit.subject)
                .with_size(ComponentSize{pixels(subjectW), children()})
                .with_custom_text_color(textCol)
                .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
                .with_debug_name("commit_subject"));

        if (bestBadge) {
            afterhours::Color bg, btxt;
            switch (bestBadge->type) {
                case commit_log_detail::DecorationType::Head:
                    bg = theme::BADGE_HEAD_BG;
                    btxt = afterhours::Color{255, 255, 255, 255};
                    break;
                case commit_log_detail::DecorationType::LocalBranch:
                    bg = theme::BADGE_BRANCH_BG;
                    btxt = afterhours::Color{255, 255, 255, 255};
                    break;
                case commit_log_detail::DecorationType::RemoteBranch:
                    bg = theme::BADGE_REMOTE_BG;
                    btxt = afterhours::Color{255, 255, 255, 255};
                    break;
                case commit_log_detail::DecorationType::Tag:
                    bg = theme::BADGE_TAG_BG;
                    btxt = theme::BADGE_TAG_TEXT;
                    break;
                default:
                    bg = theme::BADGE_TAG_BG;
                    btxt = theme::BADGE_TAG_TEXT;
                    break;
            }
            div(ctx, mk(row.ent(), 10),
                preset::Badge(bestBadge->label, bg, btxt)
                    .with_font_size(FontSize::Medium)
                    .with_debug_name("commit_badge"));
        }

        // Commit hash (absolute positioned at fixed right position)
        div(ctx, mk(row.ent(), 30),
            preset::MetaText(commit.hash.substr(0, 7))
                .with_size(ComponentSize{pixels(HASH_W), h720(ROW_H)})
                .with_font_size(FontSize::Medium)
                .with_alignment(TextAlignment::Right)
                .with_absolute_position()
                .with_translate(pixels(sidebarW - HASH_W - 4.0f), pixels(0))
                .with_debug_name("commit_hash"));

        // Click -> select this commit
        if (row.ent().get<HasClickListener>().down) {
            auto rEntities = afterhours::EntityQuery({.force_merge = true})
                                 .whereHasComponent<RepoComponent>()
                                 .whereHasComponent<ActiveTab>()
                                 .gen();
            if (!rEntities.empty()) {
                auto& r = rEntities[0].get().get<RepoComponent>();
                r.selectedCommitHash = commit.hash;
                r.selectedFilePath.clear();
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
                .with_size(w1280(480), h720(380))
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
                        .top = h720(4), .right = w1280(16),
                        .bottom = h720(2), .left = w1280(16)})
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
                        .top = h720(2), .right = w1280(16),
                        .bottom = h720(4), .left = w1280(19)})
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
                        .top = h720(8), .right = w1280(16),
                        .bottom = h720(2), .left = w1280(16)})
                    .with_custom_text_color(theme::TEXT_SECONDARY)
                    .with_alignment(TextAlignment::Left)
                    .with_render_layer(CONTENT_LAYER)
                    .with_debug_name("unstaged_label"));

            auto unstagedList = div(ctx, mk(modalEnt, 21),
                ComponentConfig{}
                    .with_size(ComponentSize{percent(1.0f), children()})
                    .with_flex_direction(FlexDirection::Column)
                    .with_padding(Padding{
                        .top = h720(2), .right = w1280(16),
                        .bottom = h720(4), .left = w1280(19)})
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
                    .top = h720(8), .right = w1280(16),
                    .bottom = h720(4), .left = w1280(16)})
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
                    .left = {}, .right = w1280(8)})
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
                    .left = {}, .right = w1280(8)})
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
