#pragma once

#include <functional>
#include <string>
#include <vector>

#include <afterhours/src/graphics.h>
#include <afterhours/src/ecs.h>

#include "../ecs/components.h"
#include "../ecs/query_helpers.h"
#include "../git/git_commands.h"
#include "../git/git_runner.h"

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
    if (menu) menu->pendingToast = msg;
}

inline std::vector<Menu> createMenuBar() {
    std::vector<Menu> menus;

    // File menu
    menus.push_back({"File", {
        MenuItem::item("Open Repository...", "Cmd+O", [] {
            set_pending_toast("Open Repository is not yet implemented");
        }),
        MenuItem::separator(),
        MenuItem::item("Close Tab", "Cmd+W", [] {
            set_pending_toast("Close Tab is not yet implemented");
        }),
        MenuItem::separator(),
        MenuItem::item("Quit", "Cmd+Q", [] {
            afterhours::graphics::request_quit();
        }),
    }});

    // Edit menu
    menus.push_back({"Edit", {
        MenuItem::item("Copy", "Cmd+C", [] {
            set_pending_toast("Copy is not yet implemented");
        }),
        MenuItem::item("Select All", "Cmd+A", [] {
            set_pending_toast("Select All is not yet implemented");
        }),
        MenuItem::separator(),
        MenuItem::item("Find...", "Cmd+F", [] {
            set_pending_toast("Find is not yet implemented");
        }),
    }});

    // View menu
    menus.push_back({"View", {
        MenuItem::item("Toggle Sidebar", "Cmd+B", [] {
            auto* l = ecs::find_singleton<ecs::LayoutComponent>();
            if (l) l->sidebarVisible = !l->sidebarVisible;
        }),
        MenuItem::item("Toggle Command Log", "Cmd+Shift+L", [] {
            auto* l = ecs::find_singleton<ecs::LayoutComponent>();
            if (l) l->commandLogVisible = !l->commandLogVisible;
        }),
        MenuItem::separator(),
        MenuItem::item("Inline Diff", "Cmd+Shift+I", [] {
            auto* l = ecs::find_singleton<ecs::LayoutComponent>();
            if (l) l->diffViewMode = ecs::LayoutComponent::DiffViewMode::Inline;
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
        MenuItem::item("Zoom In", "Cmd+=", [] {
            set_pending_toast("Zoom In is not yet implemented");
        }),
        MenuItem::item("Zoom Out", "Cmd+-", [] {
            set_pending_toast("Zoom Out is not yet implemented");
        }),
        MenuItem::item("Reset Zoom", "Cmd+0", [] {
            set_pending_toast("Reset Zoom is not yet implemented");
        }),
    }});

    // Git menu
    menus.push_back({"Repository", {
        MenuItem::item("Stage File", "Cmd+Shift+S", [] {
            auto* r = ecs::find_singleton<ecs::RepoComponent, ecs::ActiveTab>();
            if (r && !r->selectedFilePath.empty()) {
                git::stage_file(r->repoPath, r->selectedFilePath);
                r->refreshRequested = true;
            }
        }),
        MenuItem::item("Unstage File", "Cmd+Shift+U", [] {
            auto* r = ecs::find_singleton<ecs::RepoComponent, ecs::ActiveTab>();
            if (r && !r->selectedFilePath.empty()) {
                git::unstage_file(r->repoPath, r->selectedFilePath);
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
            if (r) { git::git_push(r->repoPath); r->refreshRequested = true; }
        }),
        MenuItem::item("Pull", "", [] {
            auto* r = ecs::find_singleton<ecs::RepoComponent, ecs::ActiveTab>();
            if (r) { git::git_pull(r->repoPath); r->refreshRequested = true; }
        }),
        MenuItem::item("Fetch", "", [] {
            auto* r = ecs::find_singleton<ecs::RepoComponent, ecs::ActiveTab>();
            if (r) { git::git_fetch(r->repoPath); r->refreshRequested = true; }
        }),
    }});

    // Help menu
    menus.push_back({"Help", {
        MenuItem::item("Keyboard Shortcuts", "Cmd+?", [] {
            set_pending_toast("Keyboard Shortcuts is not yet implemented");
        }),
        MenuItem::item("Command Log", "", [] {
            auto* l = ecs::find_singleton<ecs::LayoutComponent>();
            if (l) l->commandLogVisible = !l->commandLogVisible;
        }),
        MenuItem::item("About floatinghotel", "", [] {
            set_pending_toast("About floatinghotel is not yet implemented");
        }),
    }});

    return menus;
}

}  // namespace menu_setup
