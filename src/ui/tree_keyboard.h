#pragma once

#include "focus.h"
#include "zoom.h"
#include "../util/text_decode.h"
#include "../util/tree_destination.h"

namespace ui {

inline std::optional<file_tree::Move> tree_keys(UIContext<InputAction>& ctx, ecs::RepoComponent& repo,
        const ecs::LayoutComponent& layout, file_tree::NavigationState& state, const std::string& context,
        const std::vector<file_tree::Row>& rows, const std::set<std::string>& collapsed) {
    if (state.context != context) state = {context};
    if (shortcuts_blocked(layout) || layout.filePickerOpen || rows.empty()) return {};
    auto focused = afterhours::ui::UICollectionHolder::getEntityForID(ctx.focus_id);
    const auto target = focused.valid() ? focus_target(**focused) : std::nullopt;
    const auto owner = shortcut_owner(ctx, repo);
    if (owner.region != reading::focus::Region::Tree) { state.typing = {}; return {}; }
    if (afterhours::input::is_key_down(343) || afterhours::input::is_key_down(347) ||
        afterhours::input::is_key_down(341) || afterhours::input::is_key_down(345) ||
        afterhours::input::is_key_down(342) || afterhours::input::is_key_down(346)) return {};
    if (owner.text) {
        state.typing = {};
        if (target && target->control == "commit_file_filter" && afterhours::input::is_key_pressed(258) &&
            !afterhours::input::is_key_down(340) && !afterhours::input::is_key_down(344)) {
            (void)ctx.pressed(InputAction::WidgetNext);
            if (std::none_of(rows.begin(), rows.end(), [&](const auto& row) { return row.path == state.path; })) state.path = rows.front().path;
            state.pendingFocus = state.pendingReveal = true;
            state.revealPath = state.path;
        }
        return {};
    }
    if (target && !target->item.empty()) {
        if (target->item != state.path) state.typing = {};
        state.path = target->item;
    }
    std::optional<file_tree::Key> key;
    if (afterhours::input::is_key_pressed(265)) { key = file_tree::Key::Up; (void)ctx.pressed(InputAction::WidgetUp); }
    if (afterhours::input::is_key_pressed(264)) { key = file_tree::Key::Down; (void)ctx.pressed(InputAction::WidgetDown); }
    if (afterhours::input::is_key_pressed(263)) { key = file_tree::Key::Left; (void)ctx.pressed(InputAction::WidgetLeft); }
    if (afterhours::input::is_key_pressed(262)) { key = file_tree::Key::Right; (void)ctx.pressed(InputAction::WidgetRight); }
    if (afterhours::input::is_key_pressed(257)) { key = file_tree::Key::Enter; (void)ctx.pressed(InputAction::WidgetPress); }
    std::optional<file_tree::Move> move;
    if (key) {
        state.typing = {};
        move = file_tree::navigate(rows, collapsed, state.path, *key);
    } else {
        for (int cp = afterhours::input::get_char_pressed(); cp != 0; cp = afterhours::input::get_char_pressed()) {
            if (cp < 32 || cp == 127 || cp > 0x10ffff || (cp >= 0xd800 && cp <= 0xdfff)) continue;
            std::string input;
            text_decode::append_utf8(input, static_cast<std::uint32_t>(cp));
            if (cp == 32) (void)ctx.pressed(InputAction::WidgetPress);
            if (auto selected = file_tree::type_select(rows, move ? move->path : state.path, state.typing,
                                                       input, std::chrono::steady_clock::now())) move = std::move(selected);
        }
    }
    if (move) {
        state.path = state.revealPath = move->path;
        state.pendingFocus = state.pendingReveal = true;
    }
    return move;
}

inline void reveal_tree_row(afterhours::ui::imm::EntityParent parent, file_tree::NavigationState& state,
                             const std::vector<file_tree::Row>& rows) {
    state.revealEntity = -1;
    if (!state.pendingFocus && !state.pendingReveal) return;
    auto [entity, owner] = afterhours::ui::imm::deref(parent);
    if (!entity.has<afterhours::ui::HasScrollView>()) return;
    auto row = std::find_if(rows.begin(), rows.end(), [&](const auto& item) { return item.path == state.revealPath; });
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
    if (state.pendingReveal && state.revealPath == path) state.revealEntity = row.id;
    if (state.pendingFocus && state.path == path) {
        ctx.set_focus(row.id);
        state.pendingFocus = false;
    } else if (!state.pendingFocus && ctx.has_focus(row.id)) state.path = path;
}

}
