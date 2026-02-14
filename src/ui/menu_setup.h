#pragma once

#include <functional>
#include <string>
#include <vector>

#include <afterhours/src/graphics.h>
#include <afterhours/src/ecs.h>

#include "../ecs/components.h"

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
        MenuItem::item("Toggle Sidebar", "Cmd+B"),
        MenuItem::item("Toggle Command Log", "Cmd+Shift+L"),
        MenuItem::separator(),
        MenuItem::item("Inline Diff", "Cmd+Shift+I"),
        MenuItem::item("Side-by-Side Diff", "Cmd+Shift+D"),
        MenuItem::separator(),
        MenuItem::item("Changed Files View", ""),
        MenuItem::item("Tree View", ""),
        MenuItem::item("All Files View", ""),
        MenuItem::separator(),
        MenuItem::item("Zoom In", "Cmd+="),
        MenuItem::item("Zoom Out", "Cmd+-"),
        MenuItem::item("Reset Zoom", "Cmd+0"),
    }});

    // Git menu
    menus.push_back({"Git", {
        MenuItem::item("Stage File", "Cmd+Shift+S"),
        MenuItem::item("Unstage File", "Cmd+Shift+U"),
        MenuItem::separator(),
        MenuItem::item("Commit...", "Cmd+Enter"),
        MenuItem::item("Amend Last Commit", ""),
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
        MenuItem::item("Push", "Cmd+Shift+P"),
        MenuItem::item("Pull", "Cmd+Shift+L"),
        MenuItem::item("Fetch", ""),
    }});

    // Help menu
    menus.push_back({"Help", {
        MenuItem::item("Keyboard Shortcuts", "Cmd+?"),
        MenuItem::item("Command Log", ""),
        MenuItem::item("About floatinghotel", ""),
    }});

    return menus;
}

}  // namespace menu_setup
