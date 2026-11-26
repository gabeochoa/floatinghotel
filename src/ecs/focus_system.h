#pragma once

#include "../ui/focus.h"
#include "../ui/context_menu.h"
#include "../util/signpost.h"

namespace ecs {

struct BeginFocusFrame : afterhours::System<UIContext<InputAction>> {
    void for_each_with(Entity&, UIContext<InputAction>& ctx, float) override {
        static auto signpostLog = signpost::log_for("frame");
        signpost::event(signpostLog, "frame_tick", "");
        if (auto* layout = find_singleton<LayoutComponent>()) {
            const auto* owner = find_singleton_entity<RepoComponent, ActiveTab>();
            if (layout->focusRepositoryOwner != (owner ? owner->id : -1)) {
                // Stash the outgoing repo's focus and restore the incoming
                // one's instead of resetting on every switch.
                if (layout->focusRepositoryOwner >= 0) {
                    layout->modalByOwner[layout->focusRepositoryOwner] = layout->modal;
                    layout->focusByOwner[layout->focusRepositoryOwner] = layout->focus;
                    if (layout->focusByOwner.size() > 8) layout->focusByOwner.erase(layout->focusByOwner.begin());
                }
                const int next = owner ? owner->id : -1;
                auto saved = layout->focusByOwner.find(next);
                layout->focus = saved != layout->focusByOwner.end() ? saved->second : reading::focus::State{};
                auto savedModal = layout->modalByOwner.find(next);
                layout->modal = savedModal != layout->modalByOwner.end() ? savedModal->second : std::nullopt;
                layout->focusRepositoryOwner = next;
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
        auto* repo = find_singleton<RepoComponent, ActiveTab>();
        if (!layout || !repo) return;
        const auto* owner = find_singleton_entity<RepoComponent, ActiveTab>();
        if (layout->focusRepositoryOwner != owner->id) {
            if (layout->focusRepositoryOwner >= 0) {
                layout->modalByOwner[layout->focusRepositoryOwner] = layout->modal;
                layout->focusByOwner[layout->focusRepositoryOwner] = layout->focus;
                if (layout->focusByOwner.size() > 8) layout->focusByOwner.erase(layout->focusByOwner.begin());
            }
            auto saved = layout->focusByOwner.find(owner->id);
            layout->focus = saved != layout->focusByOwner.end() ? saved->second : reading::focus::State{};
            auto savedModal = layout->modalByOwner.find(owner->id);
            layout->modal = savedModal != layout->modalByOwner.end() ? savedModal->second : std::nullopt;
            layout->focusRepositoryOwner = owner->id;
        }
        const auto visible = ui::stored_popups(*repo, *layout);
        auto& state = layout->focus;
        state.sync(repo->repoPath, repo->workspace().generation(), visible);
        if (repo->readingFocus) {
            if (repo->readingFocus->repository == repo->repoPath && repo->readingFocus->document == repo->workspace().active_id()) {
                state.pending = repo->readingFocus;
                state.pendingGeneration = repo->workspace().generation();
            }
            repo->readingFocus.reset();
        }
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
            if ((*target == *state.pending || (state.pending->control.empty() && entity.has<ui::FocusIdentity>() &&
                    target->item == state.pending->item)) && (entity.has<HasClickListener>() || ui::text_control(entity))) {
                ui::focus_control(ctx, entity);
                state.pending.reset();
                return;
            }
            if (entity.has<ui::FocusIdentity>() && entity.has<HasClickListener>() && target->item.empty()) fallback = entity.id;
        }
        if (fallback) {
            auto entity = afterhours::ui::UICollectionHolder::getEntityForID(*fallback);
            if (entity.valid()) ui::focus_control(ctx, **entity);
            state.pending.reset();
        }
    }
};

struct RevealTreeRowSystem : afterhours::System<UIContext<InputAction>> {
    void for_each_with(Entity&, UIContext<InputAction>&, float) override {
        auto* repo = find_singleton<RepoComponent, ActiveTab>();
        const auto* layout = find_singleton<LayoutComponent>();
        if (!repo || !layout) return;
        // One sidebar tree now: the review tree, except in All-files mode
        // where the plain files tree is shown.
        auto& state = layout->fileViewMode == LayoutComponent::FileViewMode::All ?
            repo->filesTreeNavigation : repo->reviewTreeNavigation;
        if (!state.pendingReveal || state.pendingFocus) return;
        auto focused = afterhours::ui::UICollectionHolder::getEntityForID(state.revealEntity);
        if (!focused.valid()) return;
        const auto target = ui::focus_target(**focused);
        if (!target || target->repository != repo->repoPath || target->document != repo->workspace().active_id()) return;
        if (target->region != reading::focus::Region::Tree || target->item != state.revealPath) { state.pendingReveal = false; return; }
        if (!focused->get<afterhours::ui::UIComponent>().was_rendered_to_screen) return;
        const auto wheel = afterhours::input::get_mouse_wheel_move_v();
        if (wheel.x != 0.f || wheel.y != 0.f) { state.pendingReveal = false; return; }
        afterhours::ui::scroll_to_entity(**focused);
        const auto rect = ui::screen_rect(**focused), visible = ui::visible_rect(**focused);
        if (visible.height >= rect.height - 1.f) state.pendingReveal = false;
    }
};

}
