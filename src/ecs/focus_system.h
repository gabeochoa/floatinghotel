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

}
