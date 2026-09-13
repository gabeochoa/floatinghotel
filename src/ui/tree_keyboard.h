#pragma once

#include "focus.h"
#include "zoom.h"

namespace ui {

inline std::optional<file_tree::Move> tree_keys(UIContext<InputAction>& ctx, ecs::RepoComponent& repo,
        const ecs::LayoutComponent& layout, file_tree::NavigationState& state, const std::string& context,
        const std::vector<file_tree::Row>& rows, const std::set<std::string>& collapsed) {
    if (state.context != context) state = {context};
    if (shortcuts_blocked(layout) || layout.filePickerOpen || rows.empty()) return {};
    auto focused = afterhours::ui::UICollectionHolder::getEntityForID(ctx.focus_id);
    const auto target = focused.valid() ? focus_target(**focused) : std::nullopt;
    const auto owner = shortcut_owner(ctx, repo);
    if (owner.region != reading::focus::Region::Tree) return {};
    if (afterhours::input::is_key_down(343) || afterhours::input::is_key_down(347) ||
        afterhours::input::is_key_down(341) || afterhours::input::is_key_down(345) ||
        afterhours::input::is_key_down(342) || afterhours::input::is_key_down(346)) return {};
    if (owner.text) {
        if (target && target->control == "commit_file_filter" && afterhours::input::is_key_pressed(258) &&
            !afterhours::input::is_key_down(340) && !afterhours::input::is_key_down(344)) {
            (void)ctx.pressed(InputAction::WidgetNext);
            if (std::none_of(rows.begin(), rows.end(), [&](const auto& row) { return row.path == state.path; })) state.path = rows.front().path;
            state.pendingFocus = state.pendingReveal = true;
        }
        return {};
    }
    if (target && !target->item.empty()) state.path = target->item;
    std::optional<file_tree::Key> key;
    if (afterhours::input::is_key_pressed(265)) { key = file_tree::Key::Up; (void)ctx.pressed(InputAction::WidgetUp); }
    if (afterhours::input::is_key_pressed(264)) { key = file_tree::Key::Down; (void)ctx.pressed(InputAction::WidgetDown); }
    if (afterhours::input::is_key_pressed(263)) { key = file_tree::Key::Left; (void)ctx.pressed(InputAction::WidgetLeft); }
    if (afterhours::input::is_key_pressed(262)) { key = file_tree::Key::Right; (void)ctx.pressed(InputAction::WidgetRight); }
    if (afterhours::input::is_key_pressed(257)) { key = file_tree::Key::Enter; (void)ctx.pressed(InputAction::WidgetPress); }
    if (!key) return {};
    auto move = file_tree::navigate(rows, collapsed, state.path, *key);
    if (move) {
        state.path = move->path;
        state.pendingFocus = state.pendingReveal = true;
    }
    return move;
}

inline void reveal_tree_row(afterhours::ui::imm::EntityParent parent, const file_tree::NavigationState& state,
                             const std::vector<file_tree::Row>& rows) {
    if (!state.pendingFocus) return;
    auto [entity, owner] = afterhours::ui::imm::deref(parent);
    if (!entity.has<afterhours::ui::HasScrollView>()) return;
    auto row = std::find_if(rows.begin(), rows.end(), [&](const auto& item) { return item.path == state.path; });
    if (row == rows.end()) return;
    auto& scroll = entity.get<afterhours::ui::HasScrollView>();
    const float height = scroll.viewport_or_zero().y;
    const float top = static_cast<float>(row - rows.begin()) * 28.f * zoom::get();
    float target = scroll.scroll_offset.y;
    if (top < target) target = top;
    else if (top + 28.f * zoom::get() > target + height) target = top + 28.f * zoom::get() - height;
    target = std::clamp(target, 0.f, std::max(0.f, static_cast<float>(rows.size()) * 28.f * zoom::get() - height));
    scroll.scroll_offset.y = scroll.scroll_target.y = scroll.last_eased_offset.y = target;
    scroll.anchor_child = -1;
}

inline void bind_tree_row(UIContext<InputAction>& ctx, Entity& row, const ecs::RepoComponent& repo,
                           file_tree::NavigationState& state, const std::string& path) {
    bind_focus(row, repo, reading::focus::Region::Tree, path);
    if (state.pendingFocus && state.path == path) {
        ctx.set_focus(row.id);
        state.pendingFocus = false;
    } else if (!state.pendingFocus && ctx.has_focus(row.id)) state.path = path;
}

}
