#pragma once

#include "../ui/focus.h"
#include "../ui/context_menu.h"

namespace ecs {

struct BeginFocusFrame : afterhours::System<UIContext<InputAction>> {
    void for_each_with(Entity&, UIContext<InputAction>& ctx, float) override {
        if (auto* layout = find_singleton<LayoutComponent>()) {
            const auto* owner = find_singleton_entity<RepoComponent, ActiveTab>();
            if (layout->focusRepositoryOwner != (owner ? owner->id : -1)) {
                layout->focus = {};
                layout->focusRepositoryOwner = owner ? owner->id : -1;
                return;
            }
            auto focused = afterhours::ui::UICollectionHolder::getEntityForID(ctx.focus_id);
            layout->focus.origin = focused.valid() ? ui::focus_target(**focused) : std::nullopt;
        }
    }
};

struct RestoreFocusSystem : afterhours::System<UIContext<InputAction>> {
    void for_each_with(Entity&, UIContext<InputAction>& ctx, float) override {
        auto* layout = find_singleton<LayoutComponent>();
        const auto* repo = find_singleton<RepoComponent, ActiveTab>();
        if (!layout || !repo) return;
        const auto* owner = find_singleton_entity<RepoComponent, ActiveTab>();
        if (layout->focusRepositoryOwner != owner->id) {
            layout->focus = {};
            layout->focusRepositoryOwner = owner->id;
        }
        const auto visible = ui::open_popups(*repo, *layout);
        auto& state = layout->focus;
        state.sync(repo->repoPath, repo->workspace().generation(), visible);
        if (!state.pending) return;
        if (!state.valid(*state.pending, repo->workspace())) { state.pending.reset(); return; }
        std::optional<int> fallback;
        for (Entity& entity : afterhours::EntityQuery<>(afterhours::ui::UICollectionHolder::get().collection,
                {.force_merge = true, .ignore_temp_warning = true}).whereHasComponent<afterhours::ui::UIComponent>().gen()) {
            const auto& component = entity.get<afterhours::ui::UIComponent>();
            if (!component.was_rendered_to_screen || component.should_hide) continue;
            const auto rect = ui::visible_rect(entity);
            if (rect.width <= 0.f || rect.height <= 0.f) continue;
            const auto target = ui::focus_target(entity);
            if (!target || target->repository != state.pending->repository || target->document != state.pending->document ||
                target->region != state.pending->region) continue;
            if (*target == *state.pending) {
                ui::focus_control(ctx, entity);
                state.pending.reset();
                return;
            }
            if (entity.has<ui::FocusIdentity>() && target->item.empty()) fallback = entity.id;
        }
        if (fallback) {
            auto entity = afterhours::ui::UICollectionHolder::getEntityForID(*fallback);
            if (entity.valid()) ui::focus_control(ctx, **entity);
            state.pending.reset();
        }
    }
};

struct RevealTreeFocusSystem : afterhours::System<UIContext<InputAction>> {
    void for_each_with(Entity&, UIContext<InputAction>& ctx, float) override {
        auto* repo = find_singleton<RepoComponent, ActiveTab>();
        const auto* layout = find_singleton<LayoutComponent>();
        if (!repo || !layout) return;
        auto& state = layout->sidebarNavigation == LayoutComponent::SidebarNavigation::Review ?
            repo->reviewTreeNavigation : repo->filesTreeNavigation;
        if (!state.pendingReveal || state.pendingFocus) return;
        auto focused = afterhours::ui::UICollectionHolder::getEntityForID(ctx.focus_id);
        if (!focused.valid()) return;
        const auto target = ui::focus_target(**focused);
        if (!target || target->repository != repo->repoPath || target->document != repo->workspace().active_id()) return;
        if (target->region != reading::focus::Region::Tree || target->item != state.path) { state.pendingReveal = false; return; }
        if (!focused->get<afterhours::ui::UIComponent>().was_rendered_to_screen) return;
        const auto wheel = afterhours::input::get_mouse_wheel_move_v();
        if (wheel.x != 0.f || wheel.y != 0.f) { state.pendingReveal = false; return; }
        Entity* current = *focused;
        while (current->has<afterhours::ui::UIComponent>()) {
            const int id = current->get<afterhours::ui::UIComponent>().parent;
            if (id == current->id) break;
            auto parent = afterhours::ui::UICollectionHolder::getEntityForID(id);
            if (!parent.valid()) break;
            current = *parent;
            if (!current->has<afterhours::ui::HasScrollView>()) continue;
            auto& scroll = current->get<afterhours::ui::HasScrollView>();
            const auto rect = ui::screen_rect(**focused), viewport = ui::visible_rect(*current);
            if (viewport.height <= 0.f) continue;
            float delta = rect.y < viewport.y ? rect.y - viewport.y :
                rect.y + rect.height > viewport.y + viewport.height ? rect.y + rect.height - viewport.y - viewport.height : 0.f;
            if (delta == 0.f) continue;
            const float offset = std::clamp(scroll.scroll_offset.y + delta, 0.f,
                std::max(0.f, scroll.content_size.y - scroll.viewport_or_zero().y));
            scroll.scroll_offset.y = scroll.scroll_target.y = scroll.last_eased_offset.y = offset;
            scroll.anchor_child = -1;
        }
        const auto rect = ui::screen_rect(**focused), visible = ui::visible_rect(**focused);
        if (visible.height >= rect.height - 1.f) state.pendingReveal = false;
    }
};

}
