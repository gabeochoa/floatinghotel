#pragma once

#include <algorithm>
#include <ctime>
#include <string>
#include <vector>

#include "../../vendor/afterhours/src/core/system.h"
#include "../input_mapping.h"
#include "../rl.h"
#include "../git/git_commands.h"
#include "../git/git_runner.h"
#include "../settings.h"
#include "../ui/theme.h"
#include "../ui_context.h"
#include "components.h"

#include "../../vendor/afterhours/src/plugins/modal.h"
#include "../../vendor/afterhours/src/plugins/ui/text_input/text_input.h"

namespace ecs {

using afterhours::Entity;
using afterhours::ui::UIContext;
using afterhours::ui::imm::ComponentConfig;
using afterhours::ui::imm::div;
using afterhours::ui::imm::button;
using afterhours::ui::imm::mk;
using afterhours::ui::pixels;
using afterhours::ui::h720;
using afterhours::ui::w1280;
using afterhours::ui::expand;
using afterhours::ui::percent;
using afterhours::ui::children;
using afterhours::ui::FlexDirection;
using afterhours::ui::AlignItems;
using afterhours::ui::JustifyContent;
using afterhours::ui::ComponentSize;
using afterhours::ui::Padding;
using afterhours::ui::Margin;
using afterhours::ui::TextAlignment;
using afterhours::ui::HasClickListener;
using afterhours::ui::HasDragListener;
using afterhours::ui::Overflow;
using afterhours::ui::Axis;
using afterhours::ui::JustifyContent;
using afterhours::ui::resolve_to_pixels;
using afterhours::ui::imm::checkbox;

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

// ---- Commit log helpers ----
namespace commit_log_detail {

// Parse ISO 8601 date string to seconds since epoch (UTC)
inline std::time_t parse_iso8601(const std::string& dateStr) {
    if (dateStr.size() < 19) return 0;
    struct std::tm tm = {};
    tm.tm_year = std::stoi(dateStr.substr(0, 4)) - 1900;
    tm.tm_mon  = std::stoi(dateStr.substr(5, 2)) - 1;
    tm.tm_mday = std::stoi(dateStr.substr(8, 2));
    tm.tm_hour = std::stoi(dateStr.substr(11, 2));
    tm.tm_min  = std::stoi(dateStr.substr(14, 2));
    tm.tm_sec  = std::stoi(dateStr.substr(17, 2));
    tm.tm_isdst = -1;
    std::time_t t = timegm(&tm);
    // Adjust for timezone offset if present (e.g. +05:00 or -03:00)
    if (dateStr.size() > 19) {
        char sign = dateStr[19];
        if (sign == '+' || sign == '-') {
            int tzH = std::stoi(dateStr.substr(20, 2));
            int tzM = (dateStr.size() >= 25) ? std::stoi(dateStr.substr(23, 2)) : 0;
            int offset = tzH * 3600 + tzM * 60;
            t += (sign == '+') ? -offset : offset;
        }
    }
    return t;
}

// Compute human-readable relative time from ISO 8601 date
inline std::string relative_time(const std::string& isoDate) {
    if (isoDate.empty()) return "";
    std::time_t commitTime = parse_iso8601(isoDate);
    if (commitTime == 0) return "";
    std::time_t now = std::time(nullptr);
    long diff = static_cast<long>(std::difftime(now, commitTime));
    if (diff < 0) return "now";
    if (diff < 60) return std::to_string(diff) + "s";
    if (diff < 3600) return std::to_string(diff / 60) + "m";
    if (diff < 86400) return std::to_string(diff / 3600) + "h";
    if (diff < 604800) return std::to_string(diff / 86400) + "d";
    if (diff < 2592000) return std::to_string(diff / 604800) + "w";
    if (diff < 31536000) return std::to_string(diff / 2592000) + "mo";
    return std::to_string(diff / 31536000) + "y";
}

enum class DecorationType { LocalBranch, Head, RemoteBranch, Tag };

struct Decoration {
    std::string label;
    DecorationType type;
};

// Parse git log %D decoration string into typed badges
inline std::vector<Decoration> parse_decorations(const std::string& decStr) {
    std::vector<Decoration> result;
    if (decStr.empty()) return result;
    std::string remaining = decStr;
    while (!remaining.empty()) {
        size_t pos = remaining.find(", ");
        std::string item;
        if (pos != std::string::npos) {
            item = remaining.substr(0, pos);
            remaining = remaining.substr(pos + 2);
        } else {
            item = remaining;
            remaining.clear();
        }
        // Trim whitespace
        while (!item.empty() && item.front() == ' ') item.erase(item.begin());
        while (!item.empty() && item.back() == ' ') item.pop_back();
        if (item.empty()) continue;
        // "HEAD -> branch" splits into HEAD + branch
        if (item.find("HEAD -> ") == 0) {
            result.push_back({"HEAD", DecorationType::Head});
            result.push_back({item.substr(8), DecorationType::LocalBranch});
        } else if (item == "HEAD") {
            result.push_back({"HEAD", DecorationType::Head});
        } else if (item.find("tag: ") == 0) {
            result.push_back({item.substr(5), DecorationType::Tag});
        } else if (item.find('/') != std::string::npos) {
            result.push_back({item, DecorationType::RemoteBranch});
        } else {
            result.push_back({item, DecorationType::LocalBranch});
        }
    }
    return result;
}

} // namespace commit_log_detail

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

        // === Changed Files / Refs section (flow child of sidebar, NOT absolute) ===
        // NOTE: Use explicit pixels for width (not percent) to avoid framework bug
        // where percent(1.0f) resolves to screen width in absolute-positioned parents.
        float sidebarW = layout.sidebar.width;
        sidebarPixelWidth_ = sidebarW;  // Set early for all child rendering
        float filesH = layout.sidebarFiles.height;
        auto filesBg = div(ctx, mk(sidebarRoot.ent(), 2100),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(sidebarW), pixels(filesH)})
                .with_custom_background(theme::SIDEBAR_BG)
                .with_flex_direction(FlexDirection::Column)
                .with_overflow(Overflow::Auto, Axis::Y)
                .with_roundness(0.0f)
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
            std::string logHeaderText = "\xe2\x96\xbe COMMITS  " + std::to_string(commitCount);
            div(ctx, mk(logBg.ent(), 2310),
                ComponentConfig{}
                    .with_label(logHeaderText)
                    .with_size(ComponentSize{logW, children()})
                    .with_padding(Padding{
                        .top = pixels(7), .right = pixels(10),
                        .bottom = pixels(5), .left = pixels(10)})
                    .with_custom_background(theme::SECTION_HEADER_BG)
                    .with_custom_text_color(afterhours::Color{160, 160, 160, 255})
                    .with_font_size(pixels(theme::layout::FONT_CAPTION))
                    .with_letter_spacing(0.5f)
                    .with_alignment(TextAlignment::Left)
                    .with_roundness(0.0f)
                    .with_debug_name("log_header"));
        }

        // === Scrollable commit log entries ===
        float sh2 = static_cast<float>(afterhours::graphics::get_screen_height());
        float logHeaderConsumed = resolve_to_pixels(h720(28.0f), sh2);
        float logScrollH = layout.sidebarLog.height - logHeaderConsumed;
        if (logScrollH < 20.0f) logScrollH = 20.0f;

        auto logScroll = div(ctx, mk(logBg.ent(), 2320),
            ComponentConfig{}
                .with_size(ComponentSize{logW, pixels(logScrollH)})
                .with_overflow(Overflow::Auto, Axis::Y)
                .with_flex_direction(FlexDirection::Column)
                .with_custom_background(theme::SIDEBAR_BG)
                .with_roundness(0.0f)
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
            auto tabBg = active ? theme::BUTTON_PRIMARY
                                : theme::BUTTON_SECONDARY;
            auto tabText = active ? afterhours::Color{255, 255, 255, 255}
                                  : theme::TEXT_PRIMARY;

            auto result = button(ctx, mk(tabRow.ent(), id),
                ComponentConfig{}
                    .with_label(label)
                    .with_size(ComponentSize{children(), h720(TAB_HEIGHT - 6)})
                    .with_padding(Padding{
                        .top = h720(2), .right = w1280(TAB_HPAD),
                        .bottom = h720(2), .left = w1280(TAB_HPAD)})
                    .with_margin(Margin{
                        .top = {}, .bottom = {},
                        .left = {}, .right = w1280(4)})
                    .with_custom_background(tabBg)
                    .with_custom_text_color(tabText)
                    .with_font_size(pixels(theme::layout::FONT_CHROME))
                    .with_roundness(0.04f)
                    .with_alignment(TextAlignment::Center)
                    .with_debug_name("mode_" + label));

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

        std::string branchLabel = "BRANCHES (" +
            std::to_string(repo.branches.size()) + ")";
        div(ctx, mk(headerRow.ent(), 1),
            ComponentConfig{}
                .with_label(branchLabel)
                .with_size(ComponentSize{percent(1.0f), h720(18)})
                .with_custom_text_color(theme::TEXT_SECONDARY)
                .with_font_size(pixels(theme::layout::FONT_CAPTION))
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("branches_label"));

        // "+ New" button
        auto newBranchBtn = button(ctx, mk(headerRow.ent(), 2),
            ComponentConfig{}
                .with_label("+ New")
                .with_size(ComponentSize{children(), h720(18)})
                .with_padding(Padding{
                    .top = h720(2), .right = w1280(8),
                    .bottom = h720(2), .left = w1280(8)})
                .with_custom_background(theme::BUTTON_PRIMARY)
                .with_custom_text_color(afterhours::Color{255, 255, 255, 255})
                .with_font_size(pixels(theme::layout::FONT_CAPTION))
                .with_roundness(0.04f)
                .with_alignment(TextAlignment::Center)
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
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), pixels(refsScrollH)})
                .with_overflow(Overflow::Auto, Axis::Y)
                .with_flex_direction(FlexDirection::Column)
                .with_custom_background(theme::SIDEBAR_BG)
                .with_roundness(0.0f)
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

        // Row container
        auto rowResult = button(ctx, mk(parent, 2200 + index * 10),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), h720(ROW_H)})
                .with_flex_direction(FlexDirection::Row)
                .with_align_items(AlignItems::Center)
                .with_custom_background(rowBg)
                .with_padding(Padding{
                    .top = h720(0), .right = w1280(8),
                    .bottom = h720(0), .left = w1280(0)})
                .with_roundness(0.0f)
                .with_debug_name("branch_row"));

        // Click -> checkout this branch
        if (rowResult && !isCurrent) {
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
            ComponentConfig{}
                .with_label(branch.isLocal ? "L" : "R")
                .with_size(ComponentSize{w1280(18), h720(14)})
                .with_padding(Padding{
                    .top = h720(1), .right = w1280(2),
                    .bottom = h720(1), .left = w1280(2)})
                .with_margin(Margin{
                    .top = {}, .bottom = {},
                    .left = w1280(isCurrent ? 5.0f : 8.0f),
                    .right = w1280(6)})
                .with_custom_background(badgeBg)
                .with_custom_text_color(afterhours::Color{255, 255, 255, 255})
                .with_font_size(pixels(theme::layout::FONT_CAPTION))
                .with_roundness(0.15f)
                .with_alignment(TextAlignment::Center)
                .with_debug_name("branch_badge"));

        // Branch name
        auto nameColor = isCurrent ? afterhours::Color{255, 255, 255, 255}
                                   : theme::TEXT_PRIMARY;
        div(ctx, mk(rowResult.ent(), 3),
            ComponentConfig{}
                .with_label(branch.name)
                .with_size(ComponentSize{percent(1.0f), h720(ROW_H)})
                .with_custom_text_color(nameColor)
                .with_font_size(pixels(theme::layout::FONT_BODY))
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
                    .with_font_size(pixels(theme::layout::FONT_META))
                    .with_alignment(TextAlignment::Right)
                    .with_roundness(0.0f)
                    .with_debug_name("branch_tracking"));
        }

        // Delete button (only for non-current branches)
        if (!isCurrent) {
            auto deleteBtn = button(ctx, mk(rowResult.ent(), 5),
                ComponentConfig{}
                    .with_label("x")
                    .with_size(ComponentSize{w1280(20), h720(20)})
                    .with_custom_background(theme::BUTTON_SECONDARY)
                    .with_custom_text_color(theme::STATUS_DELETED)
                    .with_roundness(0.04f)
                    .with_alignment(TextAlignment::Center)
                    .with_debug_name("delete_branch_btn"));

            if (deleteBtn) {
                repo.deleteBranchName = branch.name;
                repo.showDeleteBranchDialog = true;
            }
        }

        // Row separator
        div(ctx, mk(parent, 2200 + index * 10 + 1),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), h720(1)})
                .with_custom_background(theme::ROW_SEPARATOR)
                .with_roundness(0.0f)
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
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), h720(44)})
                .with_flex_direction(FlexDirection::Row)
                .with_justify_content(JustifyContent::FlexEnd)
                .with_align_items(AlignItems::Center)
                .with_padding(Padding{
                    .top = h720(8), .right = w1280(16),
                    .bottom = h720(8), .left = w1280(16)})
                .with_render_layer(CONTENT_LAYER)
                .with_debug_name("new_branch_buttons"));

        // Cancel
        if (button(ctx, mk(btnRow.ent(), 1),
            ComponentConfig{}
                .with_label("Cancel")
                .with_size(ComponentSize{children(), h720(32)})
                .with_padding(Padding{
                    .top = h720(0), .right = w1280(16),
                    .bottom = h720(0), .left = w1280(16)})
                .with_margin(Margin{
                    .top = {}, .bottom = {},
                    .left = {}, .right = w1280(8)})
                .with_custom_background(theme::BUTTON_SECONDARY)
                .with_custom_text_color(theme::TEXT_PRIMARY)
                .with_roundness(0.04f)
                .with_alignment(TextAlignment::Center)
                .with_render_layer(CONTENT_LAYER)
                .with_debug_name("cancel_new_branch"))) {
            repo.showNewBranchDialog = false;
            repo.newBranchName.clear();
        }

        // Create
        bool canCreate = !repo.newBranchName.empty();
        auto createBg = canCreate ? theme::BUTTON_PRIMARY
                                  : theme::TOOLBAR_BTN_DISABLED;
        if (button(ctx, mk(btnRow.ent(), 2),
            ComponentConfig{}
                .with_label("Create")
                .with_size(ComponentSize{children(), h720(32)})
                .with_padding(Padding{
                    .top = h720(0), .right = w1280(16),
                    .bottom = h720(0), .left = w1280(16)})
                .with_custom_background(createBg)
                .with_custom_text_color(Color{255, 255, 255, 255})
                .with_roundness(0.04f)
                .with_alignment(TextAlignment::Center)
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
            ComponentConfig{}
                .with_label(message)
                .with_size(ComponentSize{percent(1.0f), children()})
                .with_padding(Padding{
                    .top = h720(8), .right = w1280(16),
                    .bottom = h720(8), .left = w1280(16)})
                .with_custom_text_color(theme::TEXT_PRIMARY)
                .with_alignment(TextAlignment::Left)
                .with_render_layer(CONTENT_LAYER)
                .with_debug_name("delete_msg"));

        // Button row
        auto btnRow = div(ctx, mk(modalEnt, 2),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), h720(44)})
                .with_flex_direction(FlexDirection::Row)
                .with_justify_content(JustifyContent::FlexEnd)
                .with_align_items(AlignItems::Center)
                .with_padding(Padding{
                    .top = h720(8), .right = w1280(16),
                    .bottom = h720(8), .left = w1280(16)})
                .with_render_layer(CONTENT_LAYER)
                .with_debug_name("delete_buttons"));

        // Cancel
        if (button(ctx, mk(btnRow.ent(), 1),
            ComponentConfig{}
                .with_label("Cancel")
                .with_size(ComponentSize{children(), h720(32)})
                .with_padding(Padding{
                    .top = h720(0), .right = w1280(16),
                    .bottom = h720(0), .left = w1280(16)})
                .with_margin(Margin{
                    .top = {}, .bottom = {},
                    .left = {}, .right = w1280(8)})
                .with_custom_background(theme::BUTTON_SECONDARY)
                .with_custom_text_color(theme::TEXT_PRIMARY)
                .with_roundness(0.04f)
                .with_alignment(TextAlignment::Center)
                .with_render_layer(CONTENT_LAYER)
                .with_debug_name("cancel_delete"))) {
            repo.showDeleteBranchDialog = false;
            repo.deleteBranchName.clear();
        }

        // Delete (red)
        if (button(ctx, mk(btnRow.ent(), 2),
            ComponentConfig{}
                .with_label("Delete")
                .with_size(ComponentSize{children(), h720(32)})
                .with_padding(Padding{
                    .top = h720(0), .right = w1280(16),
                    .bottom = h720(0), .left = w1280(16)})
                .with_custom_background(theme::STATUS_DELETED)
                .with_custom_text_color(Color{255, 255, 255, 255})
                .with_roundness(0.04f)
                .with_alignment(TextAlignment::Center)
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
            ComponentConfig{}
                .with_label(message)
                .with_size(ComponentSize{percent(1.0f), children()})
                .with_padding(Padding{
                    .top = h720(8), .right = w1280(16),
                    .bottom = h720(8), .left = w1280(16)})
                .with_custom_text_color(theme::STATUS_CONFLICT)
                .with_alignment(TextAlignment::Left)
                .with_render_layer(CONTENT_LAYER)
                .with_debug_name("force_delete_msg"));

        // Button row
        auto btnRow = div(ctx, mk(modalEnt, 2),
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), h720(44)})
                .with_flex_direction(FlexDirection::Row)
                .with_justify_content(JustifyContent::FlexEnd)
                .with_align_items(AlignItems::Center)
                .with_padding(Padding{
                    .top = h720(8), .right = w1280(16),
                    .bottom = h720(8), .left = w1280(16)})
                .with_render_layer(CONTENT_LAYER)
                .with_debug_name("force_delete_buttons"));

        // Cancel
        if (button(ctx, mk(btnRow.ent(), 1),
            ComponentConfig{}
                .with_label("Cancel")
                .with_size(ComponentSize{children(), h720(32)})
                .with_padding(Padding{
                    .top = h720(0), .right = w1280(16),
                    .bottom = h720(0), .left = w1280(16)})
                .with_margin(Margin{
                    .top = {}, .bottom = {},
                    .left = {}, .right = w1280(8)})
                .with_custom_background(theme::BUTTON_SECONDARY)
                .with_custom_text_color(theme::TEXT_PRIMARY)
                .with_roundness(0.04f)
                .with_alignment(TextAlignment::Center)
                .with_render_layer(CONTENT_LAYER)
                .with_debug_name("cancel_force_delete"))) {
            repo.showForceDeleteDialog = false;
            repo.deleteBranchName.clear();
        }

        // Force Delete (red)
        if (button(ctx, mk(btnRow.ent(), 2),
            ComponentConfig{}
                .with_label("Force Delete")
                .with_size(ComponentSize{children(), h720(32)})
                .with_padding(Padding{
                    .top = h720(0), .right = w1280(16),
                    .bottom = h720(0), .left = w1280(16)})
                .with_custom_background(theme::STATUS_DELETED)
                .with_custom_text_color(Color{255, 255, 255, 255})
                .with_roundness(0.04f)
                .with_alignment(TextAlignment::Center)
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
            auto tabBg = active ? theme::BUTTON_PRIMARY
                                : theme::BUTTON_SECONDARY;
            auto tabText = active ? afterhours::Color{255, 255, 255, 255}
                                  : theme::TEXT_PRIMARY;

            auto result = button(ctx, mk(tabRow.ent(), id),
                ComponentConfig{}
                    .with_label(label)
                    .with_size(ComponentSize{children(), h720(TAB_HEIGHT - 6)})
                    .with_padding(Padding{
                        .top = h720(2), .right = w1280(TAB_HPAD),
                        .bottom = h720(2), .left = w1280(TAB_HPAD)})
                    .with_margin(Margin{
                        .top = {}, .bottom = {},
                        .left = {}, .right = w1280(4)})
                    .with_custom_background(tabBg)
                    .with_custom_text_color(tabText)
                    .with_roundness(0.04f)
                    .with_alignment(TextAlignment::Center)
                    .with_debug_name("tab_" + label));

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
            // Empty state
            div(ctx, mk(scrollParent, 2500),
                ComponentConfig{}
                    .with_label("\xe2\x9c\x93 No changes") // ✓ No changes
                    .with_size(ComponentSize{percent(1.0f), h720(28)})
                    .with_padding(Padding{
                        .top = h720(20), .right = w1280(8),
                        .bottom = h720(4), .left = w1280(8)})
                    .with_custom_text_color(theme::EMPTY_STATE_TEXT)
                    .with_alignment(TextAlignment::Center)
                    .with_roundness(0.0f)
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
            return;
        }

        int nextId = 2600;

        // === Staged Changes section ===
        if (!repo.stagedFiles.empty()) {
            render_section_header(ctx, scrollParent, nextId++,
                "STAGED CHANGES", repo.stagedFiles.size());

            for (int i = 0; i < static_cast<int>(repo.stagedFiles.size()); ++i) {
                render_file_row(ctx, scrollParent, nextId++,
                    repo.stagedFiles[i], repo, true);
            }
        }

        // === Changes (unstaged) section ===
        if (!repo.unstagedFiles.empty()) {
            render_section_header(ctx, scrollParent, nextId++,
                "UNSTAGED CHANGES", repo.unstagedFiles.size());

            for (int i = 0; i < static_cast<int>(repo.unstagedFiles.size()); ++i) {
                render_file_row(ctx, scrollParent, nextId++,
                    repo.unstagedFiles[i], repo, false);
            }
        }

        // === Untracked section ===
        if (!repo.untrackedFiles.empty()) {
            render_section_header(ctx, scrollParent, nextId++,
                "UNTRACKED", repo.untrackedFiles.size());

            for (int i = 0; i < static_cast<int>(repo.untrackedFiles.size()); ++i) {
                render_untracked_row(ctx, scrollParent, nextId++,
                    repo.untrackedFiles[i], repo);
            }
        }
    }

    // Render a section header: "▾ STAGED CHANGES  1"
    void render_section_header(UIContext<InputAction>& ctx,
                                Entity& parent, int id,
                                const std::string& label, size_t count) {
        auto secWidth = sidebarPixelWidth_ > 0 ? pixels(sidebarPixelWidth_) : percent(1.0f);

        std::string headerText = "\xe2\x96\xbe " + label +
                                 "  " + std::to_string(count);
        div(ctx, mk(parent, id),
            ComponentConfig{}
                .with_label(headerText)
                .with_size(ComponentSize{secWidth, children()})
                .with_padding(Padding{
                    .top = pixels(7), .right = pixels(10),
                    .bottom = pixels(5), .left = pixels(10)})
                .with_custom_background(theme::SECTION_HEADER_BG)
                .with_custom_text_color(afterhours::Color{160, 160, 160, 255})
                .with_font_size(pixels(theme::layout::FONT_CAPTION))
                .with_letter_spacing(0.5f)
                .with_alignment(TextAlignment::Left)
                .with_roundness(0.0f)
                .with_debug_name("section_hdr"));
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

        auto rowBg = selected ? theme::SELECTED_BG : theme::SIDEBAR_BG;

        std::string fname = sidebar_detail::basename_from_path(file.path);
        std::string dir = sidebar_detail::dir_from_path(file.path);
        std::string statusStr(1, statusChar);

        auto rowWidth = sidebarPixelWidth_ > 0 ? pixels(sidebarPixelWidth_) : percent(1.0f);

        // Row container (clickable div with Row flex)
        auto row = div(ctx, mk(parent, id),
            ComponentConfig{}
                .with_size(ComponentSize{rowWidth, h720(ROW_H)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(afterhours::ui::FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_custom_background(rowBg)
                .with_custom_hover_bg(selected ? theme::SELECTED_BG : theme::HOVER_BG)
                .with_cursor(afterhours::ui::CursorType::Pointer)
                .with_padding(Padding{
                    .top = pixels(3), .right = pixels(10),
                    .bottom = pixels(3), .left = pixels(10)})
                .with_gap(pixels(6))
                .with_roundness(0.0f)
                .with_debug_name("file_row"));

        row.ent().addComponentIfMissing<HasClickListener>([](Entity&){});

        auto textCol = selected ? afterhours::Color{255, 255, 255, 255}
                                : theme::TEXT_PRIMARY;

        // Filename: sidebarW - padL(10) - padR(10) - gap(6) - status(16)
        float nameW = sidebarPixelWidth_ - 42.0f;
        if (nameW < 40.0f) nameW = 40.0f;

        std::string label = fname;
        if (!dir.empty()) label += "  " + dir;

        div(ctx, mk(row.ent(), 1),
            ComponentConfig{}
                .with_label(label)
                .with_size(ComponentSize{pixels(nameW), children()})
                .with_transparent_bg()
                .with_custom_text_color(textCol)
                .with_font_size(pixels(theme::layout::FONT_CHROME))
                .with_alignment(TextAlignment::Left)
                .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
                .with_roundness(0.0f)
                .with_debug_name("file_name"));

        // Status letter (fixed width, colored)
        auto statusCol = sidebar_detail::status_color(statusChar);
        div(ctx, mk(row.ent(), 3),
            ComponentConfig{}
                .with_label(statusStr)
                .with_size(ComponentSize{pixels(16), children()})
                .with_transparent_bg()
                .with_custom_text_color(statusCol)
                .with_font_size(pixels(theme::layout::FONT_META))
                .with_alignment(TextAlignment::Right)
                .with_roundness(0.0f)
                .with_debug_name("file_status"));

        // Click -> select this file
        if (row.ent().get<HasClickListener>().down) {
            auto rEntities = afterhours::EntityQuery({.force_merge = true})
                                 .whereHasComponent<RepoComponent>()
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

        auto rowBg = selected ? theme::SELECTED_BG : theme::SIDEBAR_BG;

        std::string fname = sidebar_detail::basename_from_path(path);
        std::string dir = sidebar_detail::dir_from_path(path);

        auto rowWidth = sidebarPixelWidth_ > 0 ? pixels(sidebarPixelWidth_) : percent(1.0f);

        // Row container (clickable div with Row flex)
        auto row = div(ctx, mk(parent, id),
            ComponentConfig{}
                .with_size(ComponentSize{rowWidth, h720(ROW_H)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(afterhours::ui::FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_custom_background(rowBg)
                .with_custom_hover_bg(selected ? theme::SELECTED_BG : theme::HOVER_BG)
                .with_cursor(afterhours::ui::CursorType::Pointer)
                .with_padding(Padding{
                    .top = pixels(3), .right = pixels(10),
                    .bottom = pixels(3), .left = pixels(10)})
                .with_gap(pixels(6))
                .with_roundness(0.0f)
                .with_debug_name("untracked_row"));

        row.ent().addComponentIfMissing<HasClickListener>([](Entity&){});

        auto textCol = selected ? afterhours::Color{255, 255, 255, 255}
                                : theme::TEXT_PRIMARY;

        // Filename: sidebarW - padL(10) - padR(10) - gap(6) - status(16)
        float nameW = sidebarPixelWidth_ - 42.0f;
        if (nameW < 40.0f) nameW = 40.0f;

        std::string label = fname;
        if (!dir.empty()) label += "  " + dir;

        div(ctx, mk(row.ent(), 1),
            ComponentConfig{}
                .with_label(label)
                .with_size(ComponentSize{pixels(nameW), children()})
                .with_transparent_bg()
                .with_custom_text_color(textCol)
                .with_font_size(pixels(theme::layout::FONT_CHROME))
                .with_alignment(TextAlignment::Left)
                .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
                .with_roundness(0.0f)
                .with_debug_name("file_name"));

        // Status letter "U" (fixed width, gray)
        div(ctx, mk(row.ent(), 3),
            ComponentConfig{}
                .with_label("U")
                .with_size(ComponentSize{pixels(16), children()})
                .with_transparent_bg()
                .with_custom_text_color(sidebar_detail::status_color('U'))
                .with_font_size(pixels(theme::layout::FONT_META))
                .with_alignment(TextAlignment::Right)
                .with_roundness(0.0f)
                .with_debug_name("file_status"));

        if (row.ent().get<HasClickListener>().down) {
            auto rEntities = afterhours::EntityQuery({.force_merge = true})
                                 .whereHasComponent<RepoComponent>()
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
                ComponentConfig{}
                    .with_label("No commits yet")
                    .with_size(ComponentSize{percent(1.0f), h720(32)})
                    .with_padding(Padding{
                        .top = h720(16), .right = w1280(8),
                        .bottom = h720(8), .left = w1280(8)})
                    .with_custom_text_color(theme::EMPTY_STATE_TEXT)
                    .with_alignment(TextAlignment::Center)
                    .with_roundness(0.0f)
                    .with_debug_name("empty_log"));
            return;
        }

        constexpr int MAX_VISIBLE = 500;
        int count = std::min(static_cast<int>(repo.commitLog.size()), MAX_VISIBLE);

        for (int i = 0; i < count; ++i) {
            render_commit_row(ctx, scrollParent, i, repo.commitLog[i], repo);
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
                    .with_font_size(pixels(theme::layout::FONT_META))
                    .with_alignment(TextAlignment::Center)
                    .with_roundness(0.0f)
                    .with_debug_name("lazy_load"));
        }
    }

    // Render a single commit row with Row flex: [dot] [label with message+badges+hash]
    void render_commit_row(UIContext<InputAction>& ctx,
                           Entity& parent, int index,
                           const CommitEntry& commit,
                           RepoComponent& repo) {
        bool selected = (commit.hash == repo.selectedCommitHash);
        constexpr float ROW_H = static_cast<float>(theme::layout::COMMIT_ROW_HEIGHT);

        auto rowBg = selected ? theme::SELECTED_BG : theme::SIDEBAR_BG;

        int baseId = index * 2 + 10;
        float sidebarW = sidebarPixelWidth_ > 0 ? sidebarPixelWidth_ : 300.0f;

        // Build single-line label: "subject  [badges]  hash"
        auto badges = commit_log_detail::parse_decorations(commit.decorations);
        std::string commitLabel = commit.subject;
        if (!badges.empty()) {
            for (auto& badge : badges) {
                commitLabel += "  [" + badge.label + "]";
            }
        }
        commitLabel += "  " + commit.hash.substr(0, 7);

        // Row container (clickable div with Row flex)
        auto row = div(ctx, mk(parent, baseId),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(sidebarW), h720(ROW_H)})
                .with_flex_direction(FlexDirection::Row)
                .with_flex_wrap(afterhours::ui::FlexWrap::NoWrap)
                .with_align_items(AlignItems::Center)
                .with_custom_background(rowBg)
                .with_custom_hover_bg(selected ? theme::SELECTED_BG : theme::HOVER_BG)
                .with_cursor(afterhours::ui::CursorType::Pointer)
                .with_padding(Padding{
                    .top = pixels(4), .right = pixels(10),
                    .bottom = pixels(4), .left = pixels(10)})
                .with_gap(pixels(8))
                .with_roundness(0.0f)
                .with_debug_name("commit_row"));

        row.ent().addComponentIfMissing<HasClickListener>([](Entity&){});

        // Purple dot (8x8 circle)
        div(ctx, mk(row.ent(), 1),
            ComponentConfig{}
                .with_size(ComponentSize{pixels(8), pixels(8)})
                .with_custom_background(theme::GRAPH_DOT)
                .with_roundness(1.0f)
                .with_debug_name("commit_dot"));

        // Commit label: sidebarW - padL(10) - padR(10) - dot(8) - gap(8)
        float labelW = sidebarW - 36.0f;
        if (labelW < 40.0f) labelW = 40.0f;

        auto textCol = selected ? afterhours::Color{255, 255, 255, 255}
                                : theme::TEXT_PRIMARY;
        div(ctx, mk(row.ent(), 2),
            ComponentConfig{}
                .with_label(commitLabel)
                .with_size(ComponentSize{pixels(labelW), children()})
                .with_transparent_bg()
                .with_custom_text_color(textCol)
                .with_font_size(pixels(theme::layout::FONT_CHROME))
                .with_alignment(TextAlignment::Left)
                .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
                .with_roundness(0.0f)
                .with_debug_name("commit_label"));

        // Click -> select this commit
        if (row.ent().get<HasClickListener>().down) {
            auto rEntities = afterhours::EntityQuery({.force_merge = true})
                                 .whereHasComponent<RepoComponent>()
                                 .gen();
            if (!rEntities.empty()) {
                auto& r = rEntities[0].get().get<RepoComponent>();
                r.selectedCommitHash = commit.hash;
                r.selectedFilePath.clear();
            }
        }
    }

    // (render_decoration_badges removed — badges now inline in commit label text)

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
            ComponentConfig{}
                .with_label(summary)
                .with_size(ComponentSize{percent(1.0f), children()})
                .with_padding(Padding{
                    .top = h720(8), .right = w1280(16),
                    .bottom = h720(8), .left = w1280(16)})
                .with_custom_text_color(theme::TEXT_PRIMARY)
                .with_alignment(TextAlignment::Left)
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
            ComponentConfig{}
                .with_size(ComponentSize{percent(1.0f), h720(44)})
                .with_flex_direction(FlexDirection::Row)
                .with_justify_content(JustifyContent::FlexEnd)
                .with_align_items(AlignItems::Center)
                .with_padding(Padding{
                    .top = h720(8), .right = w1280(16),
                    .bottom = h720(8), .left = w1280(16)})
                .with_render_layer(CONTENT_LAYER)
                .with_debug_name("dialog_buttons"));

        // Cancel button
        auto cancelBtn = button(ctx, mk(btnRow.ent(), 1),
            ComponentConfig{}
                .with_label("Cancel")
                .with_size(ComponentSize{children(), h720(32)})
                .with_padding(Padding{
                    .top = h720(0), .right = w1280(16),
                    .bottom = h720(0), .left = w1280(16)})
                .with_margin(Margin{
                    .top = {}, .bottom = {},
                    .left = {}, .right = w1280(8)})
                .with_custom_background(theme::BUTTON_SECONDARY)
                .with_custom_text_color(theme::TEXT_PRIMARY)
                .with_roundness(0.04f)
                .with_alignment(TextAlignment::Center)
                .with_render_layer(CONTENT_LAYER)
                .with_debug_name("cancel_btn"));

        if (cancelBtn) {
            editor.showUnstagedDialog = false;
        }

        // "Commit Staged Only" button (primary blue)
        auto stagedOnlyBtn = button(ctx, mk(btnRow.ent(), 2),
            ComponentConfig{}
                .with_label("Commit Staged Only")
                .with_size(ComponentSize{children(), h720(32)})
                .with_padding(Padding{
                    .top = h720(0), .right = w1280(16),
                    .bottom = h720(0), .left = w1280(16)})
                .with_margin(Margin{
                    .top = {}, .bottom = {},
                    .left = {}, .right = w1280(8)})
                .with_custom_background(theme::BUTTON_PRIMARY)
                .with_custom_text_color(Color{255, 255, 255, 255})
                .with_roundness(0.04f)
                .with_alignment(TextAlignment::Center)
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
            ComponentConfig{}
                .with_label("Stage All & Commit")
                .with_size(ComponentSize{children(), h720(32)})
                .with_padding(Padding{
                    .top = h720(0), .right = w1280(16),
                    .bottom = h720(0), .left = w1280(16)})
                .with_custom_background(theme::STATUS_ADDED)
                .with_custom_text_color(Color{255, 255, 255, 255})
                .with_roundness(0.04f)
                .with_alignment(TextAlignment::Center)
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
