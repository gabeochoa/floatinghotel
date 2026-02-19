#pragma once

#include <functional>
#include <string>
#include <vector>

#include <afterhours/src/graphics.h>
#include <afterhours/src/ecs.h>

#include "../ecs/components.h"
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

inline std::vector<Menu> createMenuBar() {
    std::vector<Menu> menus;

    // File menu
    menus.push_back({"File", {
        MenuItem::item("Open Repository...", "Cmd+O"),
        MenuItem::separator(),
        MenuItem::item("Close Tab", "Cmd+W"),
        MenuItem::separator(),
        MenuItem::item("Quit", "Cmd+Q", [] {
            afterhours::graphics::request_quit();
        }),
    }});

    // Edit menu
    menus.push_back({"Edit", {
        MenuItem::item("Copy", "Cmd+C"),
        MenuItem::item("Select All", "Cmd+A"),
        MenuItem::separator(),
        MenuItem::item("Find...", "Cmd+F"),
    }});

    // View menu
    menus.push_back({"View", {
        MenuItem::item("Toggle Sidebar", "Cmd+B", [] {
            auto q = afterhours::EntityQuery({.force_merge = true})
                .whereHasComponent<ecs::LayoutComponent>().gen();
            if (!q.empty()) {
                auto& l = q[0].get().get<ecs::LayoutComponent>();
                l.sidebarVisible = !l.sidebarVisible;
            }
        }),
        MenuItem::item("Toggle Command Log", "Cmd+Shift+L", [] {
            auto q = afterhours::EntityQuery({.force_merge = true})
                .whereHasComponent<ecs::LayoutComponent>().gen();
            if (!q.empty()) {
                auto& l = q[0].get().get<ecs::LayoutComponent>();
                l.commandLogVisible = !l.commandLogVisible;
            }
        }),
        MenuItem::separator(),
        MenuItem::item("Inline Diff", "Cmd+Shift+I", [] {
            auto q = afterhours::EntityQuery({.force_merge = true})
                .whereHasComponent<ecs::LayoutComponent>().gen();
            if (!q.empty())
                q[0].get().get<ecs::LayoutComponent>().diffViewMode =
                    ecs::LayoutComponent::DiffViewMode::Inline;
        }),
        MenuItem::item("Side-by-Side Diff", "Cmd+Shift+D", [] {
            auto q = afterhours::EntityQuery({.force_merge = true})
                .whereHasComponent<ecs::LayoutComponent>().gen();
            if (!q.empty())
                q[0].get().get<ecs::LayoutComponent>().diffViewMode =
                    ecs::LayoutComponent::DiffViewMode::SideBySide;
        }),
        MenuItem::separator(),
        MenuItem::item("Changed Files View", "", [] {
            auto q = afterhours::EntityQuery({.force_merge = true})
                .whereHasComponent<ecs::LayoutComponent>().gen();
            if (!q.empty())
                q[0].get().get<ecs::LayoutComponent>().fileViewMode =
                    ecs::LayoutComponent::FileViewMode::Flat;
        }),
        MenuItem::item("Tree View", "", [] {
            auto q = afterhours::EntityQuery({.force_merge = true})
                .whereHasComponent<ecs::LayoutComponent>().gen();
            if (!q.empty())
                q[0].get().get<ecs::LayoutComponent>().fileViewMode =
                    ecs::LayoutComponent::FileViewMode::Tree;
        }),
        MenuItem::item("All Files View", "", [] {
            auto q = afterhours::EntityQuery({.force_merge = true})
                .whereHasComponent<ecs::LayoutComponent>().gen();
            if (!q.empty())
                q[0].get().get<ecs::LayoutComponent>().fileViewMode =
                    ecs::LayoutComponent::FileViewMode::All;
        }),
        MenuItem::separator(),
        MenuItem::item("Zoom In", "Cmd+="),
        MenuItem::item("Zoom Out", "Cmd+-"),
        MenuItem::item("Reset Zoom", "Cmd+0"),
    }});

    // Git menu
    menus.push_back({"Repository", {
        MenuItem::item("Stage File", "Cmd+Shift+S", [] {
            auto q = afterhours::EntityQuery({.force_merge = true})
                .whereHasComponent<ecs::RepoComponent>().gen();
            if (!q.empty()) {
                auto& r = q[0].get().get<ecs::RepoComponent>();
                if (!r.selectedFilePath.empty()) {
                    git::stage_file(r.repoPath, r.selectedFilePath);
                    r.refreshRequested = true;
                }
            }
        }),
        MenuItem::item("Unstage File", "Cmd+Shift+U", [] {
            auto q = afterhours::EntityQuery({.force_merge = true})
                .whereHasComponent<ecs::RepoComponent>().gen();
            if (!q.empty()) {
                auto& r = q[0].get().get<ecs::RepoComponent>();
                if (!r.selectedFilePath.empty()) {
                    git::unstage_file(r.repoPath, r.selectedFilePath);
                    r.refreshRequested = true;
                }
            }
        }),
        MenuItem::separator(),
        MenuItem::item("Commit...", "Cmd+Enter", [] {
            auto q = afterhours::EntityQuery({.force_merge = true})
                .whereHasComponent<ecs::CommitEditorComponent>().gen();
            if (!q.empty())
                q[0].get().get<ecs::CommitEditorComponent>().commitRequested = true;
        }),
        MenuItem::item("Amend Last Commit", "", [] {
            auto q = afterhours::EntityQuery({.force_merge = true})
                .whereHasComponent<ecs::CommitEditorComponent>().gen();
            if (!q.empty())
                q[0].get().get<ecs::CommitEditorComponent>().isAmend = true;
        }),
        MenuItem::separator(),
        MenuItem::item("New Branch...", "Cmd+Shift+B", [] {
            auto repoEntities = afterhours::EntityQuery({.force_merge = true})
                .whereHasComponent<ecs::RepoComponent>()
                .gen();
            if (!repoEntities.empty()) {
                auto& repo = repoEntities[0].get().get<ecs::RepoComponent>();
                repo.showNewBranchDialog = true;
                repo.newBranchName.clear();
            }
        }),
        MenuItem::item("Checkout Branch...", "Cmd+Shift+O", [] {
            // Switch sidebar to Refs view
            auto layoutEntities = afterhours::EntityQuery({.force_merge = true})
                .whereHasComponent<ecs::LayoutComponent>()
                .gen();
            if (!layoutEntities.empty()) {
                auto& layout = layoutEntities[0].get().get<ecs::LayoutComponent>();
                layout.sidebarMode = ecs::LayoutComponent::SidebarMode::Refs;
                layout.sidebarVisible = true;
            }
        }),
        MenuItem::separator(),
        MenuItem::item("Push", "Cmd+Shift+P", [] {
            auto q = afterhours::EntityQuery({.force_merge = true})
                .whereHasComponent<ecs::RepoComponent>().gen();
            if (!q.empty()) {
                auto& r = q[0].get().get<ecs::RepoComponent>();
                git::git_push(r.repoPath);
                r.refreshRequested = true;
            }
        }),
        MenuItem::item("Pull", "Cmd+Shift+L", [] {
            auto q = afterhours::EntityQuery({.force_merge = true})
                .whereHasComponent<ecs::RepoComponent>().gen();
            if (!q.empty()) {
                auto& r = q[0].get().get<ecs::RepoComponent>();
                git::git_pull(r.repoPath);
                r.refreshRequested = true;
            }
        }),
        MenuItem::item("Fetch", "", [] {
            auto q = afterhours::EntityQuery({.force_merge = true})
                .whereHasComponent<ecs::RepoComponent>().gen();
            if (!q.empty()) {
                auto& r = q[0].get().get<ecs::RepoComponent>();
                git::git_fetch(r.repoPath);
                r.refreshRequested = true;
            }
        }),
    }});

    // Help menu
    menus.push_back({"Help", {
        MenuItem::item("Keyboard Shortcuts", "Cmd+?"),
        MenuItem::item("Command Log", "", [] {
            auto q = afterhours::EntityQuery({.force_merge = true})
                .whereHasComponent<ecs::LayoutComponent>().gen();
            if (!q.empty()) {
                auto& l = q[0].get().get<ecs::LayoutComponent>();
                l.commandLogVisible = !l.commandLogVisible;
            }
        }),
        MenuItem::item("About floatinghotel", ""),
    }});

    return menus;
}

}  // namespace menu_setup
