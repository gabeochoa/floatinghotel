#pragma once

#include <functional>
#include <string>
#include <vector>

#include <afterhours/src/graphics.h>
#include <afterhours/src/ecs.h>

#include "zoom.h"

#include "../ecs/components.h"
#include "../ecs/network_ops_system.h"
#include "../ecs/query_helpers.h"
#include "../git/git_commands.h"
#include "../git/git_runner.h"
#include "../settings.h"
#include "diff_renderer.h"
#include "change_navigation.h"
#include "file_picker.h"
#include "repo_search.h"

namespace menu_setup {

struct MenuItem {
    std::string label;
    std::string shortcut;
    bool enabled = true;
    bool isSeparator = false;
    std::function<void()> action;

    static MenuItem item(const std::string& label, const std::string& shortcut,
                         std::function<void()> action = nullptr) {
        return {label, shortcut, true, false, std::move(action)};
    }

    static MenuItem separator() {
        return {"", "", false, true, nullptr};
    }
};

struct Menu {
    std::string label;
    std::vector<MenuItem> items;
};

inline void set_pending_toast(const std::string& msg) {
    auto* menu = ecs::find_singleton<ecs::MenuComponent>();
    if (menu) menu->pendingToasts.push_back({msg});
}

inline std::vector<Menu> createMenuBar() {
    std::vector<Menu> menus;

    // File menu
    menus.push_back({"File", {
        MenuItem::item("Quit", "Cmd+Q", [] {
            afterhours::graphics::request_quit();
        }),
    }});

    // Edit menu
    menus.push_back({"Edit", {
        MenuItem::item("Copy", "Cmd+C", [] {
            std::string txt = ui::diff_sel::build_copy_text(
                ui::diff_sel::state(), Settings::get().get_copy_with_location());
            if (txt.empty()) {
                set_pending_toast("No diff selection to copy");
                return;
            }
            afterhours::clipboard::set_text(txt);
            set_pending_toast("Copied selection");
        }),
        MenuItem::item("Copy With Location (toggle)", "", [] {
            bool v = !Settings::get().get_copy_with_location();
            Settings::get().set_copy_with_location(v);
            set_pending_toast(v ? "Copy now includes file:line"
                                : "Copy now excludes location");
        }),
        MenuItem::separator(),
        MenuItem::item("Find...", "Cmd+F", [] {
            if (auto* repo = ecs::find_singleton<ecs::RepoComponent, ecs::ActiveTab>()) ui::open_find(*repo);
        }),
        MenuItem::item("Go to File...", "Cmd+P", [] {
            if (auto* l = ecs::find_singleton<ecs::LayoutComponent>()) {
                l->filePickerPosition = {};
                l->filePickerOpen = true;
                l->filePickerFocus = true;
            }
        }),
        MenuItem::item("Go to Line...", "Ctrl+G", [] {
            auto* layout = ecs::find_singleton<ecs::LayoutComponent>();
            auto* repo = ecs::find_singleton<ecs::RepoComponent, ecs::ActiveTab>();
            if (layout && repo) ecs::open_line_picker(*repo, *layout);
        }),
        MenuItem::item("Search Repository...", "Cmd+Shift+F", [] {
            if (auto* repo = ecs::find_singleton<ecs::RepoComponent, ecs::ActiveTab>()) {
                ecs::open_repo_search(*repo);
            }
        }),
        MenuItem::item("Search Commits...", "", [] {
            if (auto* repo = ecs::find_singleton<ecs::RepoComponent, ecs::ActiveTab>())
                repo->commitSearchOpen = true;
        }),
        MenuItem::item("Compare Revisions...", "", [] {
            if (auto* repo = ecs::find_singleton<ecs::RepoComponent, ecs::ActiveTab>()) {
                navigation::comparison_editor(*repo);
            }
        }),
    }});

    // View menu
    menus.push_back({"View", {
        MenuItem::item("Review Workspace (toggle)", "", [] {
            if (auto* repo = ecs::find_singleton<ecs::RepoComponent, ecs::ActiveTab>())
                repo->reviewWorkspace = !repo->reviewWorkspace;
        }),
        MenuItem::item("Back", "Alt+Left", [] {
            if (auto* repo = ecs::find_singleton<ecs::RepoComponent, ecs::ActiveTab>()) navigation::step(*repo, -1);
        }),
        MenuItem::item("Forward", "Alt+Right", [] {
            if (auto* repo = ecs::find_singleton<ecs::RepoComponent, ecs::ActiveTab>()) navigation::step(*repo, 1);
        }),
        MenuItem::item("Next Change", "", [] {
            auto* repo = ecs::find_singleton<ecs::RepoComponent, ecs::ActiveTab>();
            auto* review = ecs::find_singleton<ecs::ReviewComponent, ecs::ActiveTab>();
            if (repo && review) {
                const auto notice = ui::navigate_change(*repo, *review, 1);
                if (!notice.empty()) set_pending_toast(notice);
            }
        }),
        MenuItem::item("Previous Change", "", [] {
            auto* repo = ecs::find_singleton<ecs::RepoComponent, ecs::ActiveTab>();
            auto* review = ecs::find_singleton<ecs::ReviewComponent, ecs::ActiveTab>();
            if (repo && review) {
                const auto notice = ui::navigate_change(*repo, *review, -1);
                if (!notice.empty()) set_pending_toast(notice);
            }
        }),
        MenuItem::separator(),
        MenuItem::item("Collapse reading panel", "", [] {
            if (auto* layout = ecs::find_singleton<ecs::LayoutComponent>()) {
                layout->readingPanelCollapsed = !layout->shelfCollapsed;
                if (!*layout->readingPanelCollapsed)
                    if (auto* repo = ecs::find_singleton<ecs::RepoComponent, ecs::ActiveTab>()) navigation::restore_anchor(*repo);
            }
        }),
        MenuItem::item("Toggle Sidebar", "Cmd+B", [] {
            auto* l = ecs::find_singleton<ecs::LayoutComponent>();
            if (l) l->sidebarVisible = !l->sidebarVisible;
        }),
        MenuItem::item("Toggle Command Log", "Cmd+Shift+L", [] {
            auto* l = ecs::find_singleton<ecs::LayoutComponent>();
            if (l) l->commandLogVisible = !l->commandLogVisible;
        }),
        // Light theme is not fully supported yet (main pane stays dark), so the
        // theme toggle is hidden until it's fixed — dark-only for now.
        MenuItem::separator(),
        MenuItem::item("Inline Diff", "Cmd+Shift+I", [] {
            auto* l = ecs::find_singleton<ecs::LayoutComponent>();
            if (l) l->diffViewMode = ecs::LayoutComponent::DiffViewMode::Inline;
        }),
        MenuItem::item("Ignore Whitespace (toggle)", "", [] {
            auto* repo = ecs::find_singleton<ecs::RepoComponent, ecs::ActiveTab>();
            if (!repo) return;
            repo->ignoreWhitespace = !repo->ignoreWhitespace;
            repo->refreshRequested = true;
            repo->cachedFilePath.clear();
            if (auto* cache = ecs::find_singleton<ecs::CommitDetailCache, ecs::ActiveTab>())
                cache->cachedCommitHash.clear();
            set_pending_toast(repo->ignoreWhitespace ? "Whitespace differences ignored" : "All differences shown");
        }),
        MenuItem::item("Zero-context Diff (toggle)", "", [] {
            if (auto* repo = ecs::find_singleton<ecs::RepoComponent, ecs::ActiveTab>()) {
                repo->diffContext = repo->diffContext == 0 ? 3 : 0;
                repo->refreshRequested = true;
                repo->cachedFilePath.clear();
                if (auto* cache = ecs::find_singleton<ecs::CommitDetailCache, ecs::ActiveTab>())
                    cache->cachedCommitHash.clear();
                set_pending_toast(repo->diffContext == 0 ? "Only changed lines shown" : "Three surrounding lines shown");
            }
        }),
        MenuItem::item("Show Whitespace (toggle)", "", [] {
            if (auto* l = ecs::find_singleton<ecs::LayoutComponent>())
                l->visibleWhitespace = !l->visibleWhitespace;
        }),
        MenuItem::item("Side-by-Side Diff", "Cmd+Shift+D", [] {
            auto* l = ecs::find_singleton<ecs::LayoutComponent>();
            if (l) l->diffViewMode = ecs::LayoutComponent::DiffViewMode::SideBySide;
        }),
        MenuItem::separator(),
        MenuItem::item("Changed Files View", "", [] {
            auto* l = ecs::find_singleton<ecs::LayoutComponent>();
            if (l) l->fileViewMode = ecs::LayoutComponent::FileViewMode::Flat;
        }),
        MenuItem::item("Tree View", "", [] {
            auto* l = ecs::find_singleton<ecs::LayoutComponent>();
            if (l) l->fileViewMode = ecs::LayoutComponent::FileViewMode::Tree;
        }),
        MenuItem::item("All Files View", "", [] {
            auto* l = ecs::find_singleton<ecs::LayoutComponent>();
            if (l) l->fileViewMode = ecs::LayoutComponent::FileViewMode::All;
        }),
        MenuItem::separator(),
        MenuItem::item("Zoom In", "", [] { ui::zoom::step(ui::zoom::kStep); }),
        MenuItem::item("Zoom Out", "", [] { ui::zoom::step(-ui::zoom::kStep); }),
        MenuItem::item("Reset Zoom", "", [] { ui::zoom::reset(); }),
        MenuItem::separator(),
        MenuItem::item("Larger Code Text", "Cmd+=", [] {
            Settings::get().set_code_font_size(Settings::get().get_code_font_size() + 1.f);
        }),
        MenuItem::item("Smaller Code Text", "Cmd+-", [] {
            Settings::get().set_code_font_size(Settings::get().get_code_font_size() - 1.f);
        }),
        MenuItem::item("Reset Code Text", "Cmd+0", [] { Settings::get().set_code_font_size(Settings::kDefaultCodeFontSize); }),
    }});

    // Git menu
    menus.push_back({"Repository", {
        MenuItem::item("Stage File", "Cmd+Shift+S", [] {
            auto* r = ecs::find_singleton<ecs::RepoComponent, ecs::ActiveTab>();
            if (r && !r->selectedFilePath().empty()) {
                auto res = git::stage_file(r->repoPath, r->selectedFilePath());
                ecs::toast_on_git_failure(res, "Stage");
                r->refreshRequested = true;
            }
        }),
        MenuItem::item("Unstage File", "Cmd+Shift+U", [] {
            auto* r = ecs::find_singleton<ecs::RepoComponent, ecs::ActiveTab>();
            if (r && !r->selectedFilePath().empty()) {
                auto res = git::unstage_file(r->repoPath, r->selectedFilePath());
                ecs::toast_on_git_failure(res, "Unstage");
                r->refreshRequested = true;
            }
        }),
        MenuItem::separator(),
        MenuItem::item("Commit...", "Cmd+Enter", [] {
            auto* e = ecs::find_singleton<ecs::CommitEditorComponent, ecs::ActiveTab>();
            if (e) e->commitRequested = true;
        }),
        MenuItem::item("Amend Last Commit", "", [] {
            auto* e = ecs::find_singleton<ecs::CommitEditorComponent, ecs::ActiveTab>();
            if (e) e->isAmend = true;
        }),
        MenuItem::separator(),
        MenuItem::item("New Branch...", "Cmd+Shift+B", [] {
            auto* bd = ecs::find_singleton<ecs::BranchDialogState, ecs::ActiveTab>();
            if (bd) {
                bd->showNewBranchDialog = true;
                bd->newBranchName.clear();
            }
        }),
        MenuItem::item("Checkout Branch...", "Cmd+Shift+O", [] {
            auto* l = ecs::find_singleton<ecs::LayoutComponent>();
            if (l) {
                l->sidebarMode = ecs::LayoutComponent::SidebarMode::Refs;
                l->sidebarVisible = true;
            }
        }),
        MenuItem::separator(),
        MenuItem::item("Push", "Cmd+Shift+P", [] {
            auto* r = ecs::find_singleton<ecs::RepoComponent, ecs::ActiveTab>();
            if (r) ecs::enqueue_network_op("Push", git::git_run_async(r->repoPath, {"push"}));
        }),
        MenuItem::item("Pull", "", [] {
            auto* r = ecs::find_singleton<ecs::RepoComponent, ecs::ActiveTab>();
            if (r) ecs::enqueue_network_op("Pull", git::git_run_async(r->repoPath, {"pull"}));
        }),
        MenuItem::item("Fetch", "", [] {
            auto* r = ecs::find_singleton<ecs::RepoComponent, ecs::ActiveTab>();
            if (r) ecs::enqueue_network_op("Fetch", git::git_run_async(r->repoPath, {"fetch"}));
        }),
    }});

    // Help menu
    menus.push_back({"Help", {
        MenuItem::item("Keyboard Shortcuts", "Cmd+Shift+/", [] {
            if (auto* layout = ecs::find_singleton<ecs::LayoutComponent>()) layout->shortcutsOpen = true;
        }),
        MenuItem::item("Command Log", "", [] {
            auto* l = ecs::find_singleton<ecs::LayoutComponent>();
            if (l) l->commandLogVisible = !l->commandLogVisible;
        }),
    }});

    return menus;
}

}  // namespace menu_setup
