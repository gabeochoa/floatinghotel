#pragma once

#include <filesystem>

#include "../ecs/ui_imports.h"
#include "../ecs/tab_bar_system.h"
#include "../util/repo_picker.h"
#include "welcome.h"
#include "zoom.h"

namespace ecs {

// Resolve every candidate path on a worker: existence plus the main-repo
// root (parent of --git-common-dir) that groups linked worktrees.
inline std::vector<repo_picker::Resolved> resolve_repo_picker_paths(
    std::vector<std::string> paths, std::stop_token stop) {
    std::vector<repo_picker::Resolved> out;
    for (auto& path : paths) {
        if (stop.stop_requested()) break;
        repo_picker::Resolved resolved;
        resolved.path = path;
        std::error_code error;
        resolved.exists = std::filesystem::is_directory(path, error) && !error;
        resolved.mainPath = repo_picker::normalize(path);
        if (resolved.exists) {
            auto common = git::git_run(path, {"rev-parse", "--path-format=absolute", "--git-common-dir"}, stop);
            if (common.success()) {
                auto dir = std::filesystem::path(git::catalog::trim_line(common.stdout_str()));
                if (dir.filename() == ".git") resolved.mainPath = dir.parent_path().string();
            }
        }
        out.push_back(std::move(resolved));
    }
    return out;
}

inline std::vector<repo_picker::Entry> current_repo_picker_entries(LayoutComponent& layout) {
    std::vector<repo_picker::TabInfo> tabs;
    if (auto* strip = find_singleton<TabStripComponent>())
        for (const auto id : strip->tabOrder) {
            auto tab = EntityHelper::getEntityForID(id);
            if (!tab.valid() || tab->cleanup || !tab->has<RepoComponent>()) continue;
            const auto& repo = tab->get<RepoComponent>();
            if (repo.repoPath.empty()) continue;
            tabs.push_back({repo.repoPath, repo.currentBranch, repo.isDirty, repo.statusKnown});
        }
    std::map<std::string, repo_picker::Resolved> resolved;
    for (const auto& item : layout.repoPickerResolved)
        resolved[repo_picker::normalize(item.path)] = item;
    return repo_picker::build(tabs, Settings::get().get_pinned_repos(),
                               Settings::get().get_recent_repos(), resolved);
}

inline void open_repo_picker(LayoutComponent& layout) {
    layout.repoPickerOpen = true;
    layout.repoPickerFocus = true;
    layout.repoPickerIndex = 0;
    std::string key;
    std::vector<std::string> paths;
    auto collect = [&](const std::string& path) {
        const auto norm = repo_picker::normalize(path);
        if (norm.empty() || key.find("\n" + norm + "\n") != std::string::npos) return;
        key += "\n" + norm + "\n";
        paths.push_back(path);
    };
    if (auto* strip = find_singleton<TabStripComponent>())
        for (const auto id : strip->tabOrder) {
            auto tab = EntityHelper::getEntityForID(id);
            if (tab.valid() && !tab->cleanup && tab->has<RepoComponent>())
                collect(tab->get<RepoComponent>().repoPath);
        }
    for (const auto& path : Settings::get().get_pinned_repos()) collect(path);
    for (const auto& path : Settings::get().get_recent_repos()) collect(path);
    if (layout.repoPickerResolveKey != key && !layout.repoPickerFuture.valid()) {
        layout.repoPickerResolveKey = key;
        layout.repoPickerFuture = async_work::launch(
            [paths](std::stop_token stop) { return resolve_repo_picker_paths(paths, stop); },
            async_work::Priority::Background, std::vector<repo_picker::Resolved>{});
    }
}

struct RepoPickerSystem : afterhours::System<UIContext<InputAction>> {
    void for_each_with(Entity&, UIContext<InputAction>& ctx, float) override {
        auto* layout = find_singleton<LayoutComponent>();
        if (!layout) return;
        const bool super = afterhours::input::is_key_down(afterhours::keys::LEFT_SUPER) ||
                           afterhours::input::is_key_down(afterhours::keys::RIGHT_SUPER);
        const bool alt = afterhours::input::is_key_down(afterhours::keys::LEFT_ALT) ||
                         afterhours::input::is_key_down(afterhours::keys::RIGHT_ALT);
        if (!layout->repoPickerOpen && super && alt && !ui::shortcuts_blocked(*layout) &&
            afterhours::input::is_key_pressed(afterhours::keys::O))
            open_repo_picker(*layout);
        if (layout->repoPickerFuture.valid() &&
            layout->repoPickerFuture.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            layout->repoPickerResolved = layout->repoPickerFuture.get();
            layout->repoPickerFuture = {};
        }
        auto& root = ui_imm::getUIRootEntity();
        const float zoom = ui::zoom::get();
        const float screenWidth = ctx.screen_width / zoom;
        const float screenHeight = ctx.screen_height / zoom;
        const float width = std::max(100.f, std::min(620.f, screenWidth - 32.f));
        const float height = std::max(160.f, std::min(440.f, screenHeight - 80.f));
        auto modal = afterhours::modal::detail::modal_impl(ctx, mk(root, 596100), layout->repoPickerOpen,
            afterhours::ModalConfig{}.with_size(pixels(width), pixels(height))
                .with_show_close_button(false).with_closed_by(afterhours::ClosedBy::Any)
                .with_backdrop_color({0, 0, 0, 0}));
        if (!modal) {
            ctx.remove_input_gate("repo_picker");
            return;
        }
        modal.cmp().absolute_pos_x = (ctx.screen_width - width * zoom) * .5f;
        modal.cmp().absolute_pos_y = (ctx.screen_height - height * zoom) * .3f;
        div(ctx, mk(root, 596101), ComponentConfig{}
            .with_size(ComponentSize{pixels(screenWidth), pixels(screenHeight)})
            .with_absolute_position().with_custom_background(afterhours::Color{0, 0, 0, 64})
            .with_roundness(0.f).with_render_layer(998).with_debug_name("repo_picker_backdrop"));
        auto body = div(ctx, mk(modal.ent(), 596102), ComponentConfig{}
            .with_size(ComponentSize{percent(1.f), expand()})
            .with_flex_direction(FlexDirection::Column).with_no_wrap()
            .with_render_layer(1001).with_debug_name("repo_picker_overlay"));
        const auto modalId = modal.ent().id;
        ctx.add_input_gate("repo_picker", [modalId](afterhours::EntityID id) {
            return afterhours::modal::detail::is_entity_in_tree(modalId, id);
        });
        div(ctx, mk(body.ent(), 0), ComponentConfig{}.with_label("Repositories")
            .with_size(ComponentSize{percent(1.f), pixels(30)}).with_font_size(pixels(14)));
        auto input = afterhours::text_input::text_input(ctx, mk(body.ent(), 1), layout->repoPickerQuery,
            ComponentConfig{}.with_size(ComponentSize{percent(1.f), pixels(32)})
                .with_debug_name("repo_picker_input"));
        if (layout->repoPickerFocus) {
            ui::focus_control(ctx, input.ent());
            layout->repoPickerFocus = false;
        }
        const char* homeEnv = std::getenv("HOME");
        const std::string home = homeEnv ? homeEnv : "";
        const auto entries = current_repo_picker_entries(*layout);
        const auto ranked = repo_picker::rank(entries, layout->repoPickerQuery, home);
        layout->repoPickerIndex = ranked.empty() ? 0
            : std::clamp(layout->repoPickerIndex, 0, static_cast<int>(ranked.size()) - 1);
        auto activate = [&](const repo_picker::Entry& entry, bool newTab) {
            layout->repoPickerOpen = false;
            if (!entry.exists) {
                layout->relinkOpen = true;
                layout->relinkOld = entry.path;
                layout->relinkNew.clear();
                layout->relinkError.clear();
                return;
            }
            TabBarSystem::open_repository(entry.path, *layout, newTab);
        };
        if (!ranked.empty()) {
            if (afterhours::input::is_key_pressed(afterhours::keys::DOWN))
                layout->repoPickerIndex = std::min(layout->repoPickerIndex + 1, static_cast<int>(ranked.size()) - 1);
            if (afterhours::input::is_key_pressed(afterhours::keys::UP))
                layout->repoPickerIndex = std::max(0, layout->repoPickerIndex - 1);
            if (afterhours::input::is_key_pressed(afterhours::keys::ENTER)) {
                activate(entries[ranked[static_cast<size_t>(layout->repoPickerIndex)]], super);
                return;
            }
        }
        const bool resolving = layout->repoPickerFuture.valid();
        div(ctx, mk(body.ent(), 2), ComponentConfig{}
            .with_label(ranked.empty() ? (resolving ? std::string("Finding repositories...") : std::string("No repositories match"))
                                       : std::to_string(ranked.size()) + " repositories · Enter switch/open · Cmd+Enter new tab · Esc close")
            .with_size(ComponentSize{percent(1.f), pixels(28)}).with_font_size(pixels(12))
            .with_debug_name("repo_picker_status"));
        auto list = div(ctx, mk(body.ent(), 3), ComponentConfig{}
            .with_size(ComponentSize{percent(1.f), pixels(std::max(28.f, height - 118.f))})
            .with_overflow(Overflow::Scroll, Axis::Y).with_flex_direction(FlexDirection::Column)
            .with_no_wrap().with_debug_name("repo_picker_list"));
        std::optional<repo_picker::Section> lastSection;
        int rowId = 0;
        for (size_t r = 0; r < ranked.size(); ++r) {
            const auto& entry = entries[ranked[r]];
            if (layout->repoPickerQuery.empty() && lastSection != entry.section) {
                lastSection = entry.section;
                div(ctx, mk(list.ent(), 1000 + rowId++), ComponentConfig{}
                    .with_label(repo_picker::section_label(entry.section))
                    .with_size(ComponentSize{percent(1.f), pixels(22)}).with_font_size(pixels(12))
                    .with_custom_text_color(theme::TEXT_SECONDARY)
                    .with_debug_name("repo_picker_section_" + repo_picker::section_label(entry.section)));
            }
            std::string detail = repo_picker::shorten_home(entry.path, home);
            if (entry.open && !entry.branch.empty())
                detail += " · " + entry.branch + (entry.dirtyKnown ? (entry.dirty ? " · dirty" : " · clean") : "");
            if (entry.members.size() > 1)
                detail += " · +" + std::to_string(entry.members.size() - 1) + " worktree" + (entry.members.size() > 2 ? "s" : "");
            if (!entry.exists) detail += " · folder missing — Enter to relink";
            const bool selected = static_cast<int>(r) == layout->repoPickerIndex;
            auto row = button(ctx, mk(list.ent(), rowId++), preset::Button(ui::welcome::repo_display_name(entry.path))
                .with_size(ComponentSize{percent(1.f), pixels(30)}).with_alignment(TextAlignment::Left)
                .with_custom_background(selected ? theme::BUTTON_PRIMARY : theme::PANEL_BG)
                .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
                .with_font_size(pixels(14))
                .with_debug_name(entry.exists ? "repo_picker_result_" + ui::welcome::repo_display_name(entry.path)
                                              : "repo_picker_missing_" + ui::welcome::repo_display_name(entry.path)));
            div(ctx, mk(list.ent(), 2000 + rowId), ComponentConfig{}.with_label(detail)
                .with_size(ComponentSize{percent(1.f), pixels(18)}).with_font_size(pixels(11))
                .with_custom_text_color(theme::TEXT_SECONDARY)
                .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
                .with_debug_name("repo_picker_detail"));
            if (row) {
                activate(entry, super);
                return;
            }
        }
    }
};

// MRU repo-tab switcher: hold Ctrl+Alt, tap Tab (Shift reverses), release
// Ctrl to commit. No live preview — switching tabs saves/restores sessions,
// so the switch happens once, on release.
struct RepoTabSwitcher : afterhours::BaseComponent {
    std::vector<afterhours::EntityID> order;
    size_t index = 0;
    bool open = false;
};

inline bool render_repo_switcher(afterhours::ui::UIContext<InputAction>& ctx, ecs::LayoutComponent& layout, bool blocked) {
    auto& root = ui_imm::getUIRootEntity();
    auto& state = root.addComponentIfMissing<RepoTabSwitcher>();
    auto* strip = find_singleton<TabStripComponent>();
    const bool ctrl = afterhours::input::is_key_down(afterhours::keys::LEFT_CONTROL) || afterhours::input::is_key_down(afterhours::keys::RIGHT_CONTROL);
    const bool alt = afterhours::input::is_key_down(afterhours::keys::LEFT_ALT) || afterhours::input::is_key_down(afterhours::keys::RIGHT_ALT);
    const bool step = ctrl && alt && afterhours::input::is_key_pressed(afterhours::keys::TAB);
    ctx.remove_input_gate("repo-switcher");
    if (!strip || strip->tabOrder.size() < 2) {
        state.open = false;
        state.order.clear();
        return false;
    }
    if (state.open && (blocked || afterhours::input::is_key_pressed(afterhours::keys::ESCAPE))) {
        state.open = false;
        state.order.clear();
        return true;
    }
    if (step && !blocked && !layout.filePickerOpen && !layout.repoPickerOpen) {
        if (!state.open) {
            state.order = strip->mruOrder;
            // Tabs never switched to are missing from MRU; append in strip order.
            for (const auto id : strip->tabOrder)
                if (std::find(state.order.begin(), state.order.end(), id) == state.order.end())
                    state.order.push_back(id);
            state.order.erase(std::remove_if(state.order.begin(), state.order.end(), [&](afterhours::EntityID id) {
                                  auto tab = EntityHelper::getEntityForID(id);
                                  return !tab.valid() || tab->cleanup || !tab->has<Tab>();
                              }),
                              state.order.end());
            state.index = 0;
            state.open = state.order.size() > 1;
            if (!state.open) return true;
        }
        const bool shift = afterhours::input::is_key_down(afterhours::keys::LEFT_SHIFT) || afterhours::input::is_key_down(afterhours::keys::RIGHT_SHIFT);
        const long next = static_cast<long>(state.index) + (shift ? -1 : 1);
        state.index = static_cast<size_t>((next % static_cast<long>(state.order.size()) + static_cast<long>(state.order.size())) % static_cast<long>(state.order.size()));
    }
    if (!state.open) return false;
    if (!ctrl) {
        auto target = EntityHelper::getEntityForID(state.order[state.index]);
        state.open = false;
        state.order.clear();
        if (target.valid() && !target->has<ActiveTab>()) TabBarSystem::switch_to_tab(target.asE(), layout);
        return true;
    }
    ctx.add_input_gate("repo-switcher", [](int) { return false; });
    const float scale = ui::zoom::get();
    const float screenWidth = afterhours::graphics::get_screen_width() / scale;
    const float width = std::max(1.f, std::min(520.f, screenWidth - 24.f));
    const auto rows = std::min(state.order.size(), size_t{8});
    const float height = 40.f + static_cast<float>(rows) * 30.f;
    auto panel = div(ctx, mk(root, 596800), ComponentConfig{}
        .with_size(ComponentSize{pixels(width), pixels(height)})
        .with_absolute_position((screenWidth - width) * .5f, 80.f)
        .with_flex_direction(FlexDirection::Column).with_no_wrap()
        .with_padding(Padding{.left = pixels(8), .right = pixels(8)})
        .with_custom_background(theme::SIDEBAR_BG).with_border(theme::BORDER, pixels(1))
        .with_roundness(0.f).with_corner_radius(6.f)
        .with_render_layer(1000).with_debug_name("repo_mru_panel"));
    div(ctx, mk(panel.ent(), 0), ComponentConfig{}
        .with_label("Recent repositories · " + std::to_string(state.index + 1) + " of " + std::to_string(state.order.size()))
        .with_size(ComponentSize{percent(1.f), pixels(40)}).with_font_size(pixels(14))
        .with_custom_text_color(theme::TEXT_SECONDARY).with_roundness(0.f).with_render_layer(1001)
        .with_debug_name("repo_mru_position"));
    for (size_t i = 0; i < rows; ++i) {
        auto tab = EntityHelper::getEntityForID(state.order[i]);
        const std::string label = tab.valid() && tab->has<Tab>() ? tab->get<Tab>().label : "Untitled";
        div(ctx, mk(panel.ent(), static_cast<int>(i) + 1), ComponentConfig{}
            .with_label(label)
            .with_size(ComponentSize{percent(1.f), pixels(30)}).with_font_size(pixels(14)).with_text_inset(8.f)
            .with_custom_background(i == state.index ? theme::SELECTED_BG : theme::SIDEBAR_BG)
            .with_custom_text_color(theme::TEXT_PRIMARY)
            .with_text_overflow(afterhours::ui::TextOverflow::Ellipsis)
            .with_roundness(0.f).with_corner_radius(4.f).with_render_layer(1001)
            .with_debug_name(std::string(i == state.index ? "repo_mru_selected_" : "repo_mru_") + std::to_string(i)));
    }
    return true;
}

}  // namespace ecs
