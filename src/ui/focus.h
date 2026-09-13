#pragma once

#include "../ecs/ui_imports.h"
#include "geometry.h"

namespace ui {

struct FocusIdentity : afterhours::BaseComponent {
    reading::focus::Target target;
};

inline void bind_focus(Entity& entity, const ecs::RepoComponent& repo, reading::focus::Region region,
                       std::string item = {}, std::optional<reading::DocumentId> document = {}) {
    const auto owner = document.value_or(region == reading::focus::Region::History || region == reading::focus::Region::Menu
        ? reading::DocumentId{} : repo.workspace().active_id());
    if (item.empty() && (region == reading::focus::Region::Code || region == reading::focus::Region::Tree || region == reading::focus::Region::History))
        entity.addComponentIfMissing<HasClickListener>([](Entity&) {});
    entity.addComponentIfMissing<FocusIdentity>().target = {repo.repoPath, owner, region, std::move(item), {}};
}

inline std::optional<reading::focus::Target> focus_target(Entity& entity) {
    std::string control = entity.has<afterhours::ui::UIComponentDebug>() ? entity.get<afterhours::ui::UIComponentDebug>().name() : "";
    Entity* current = &entity;
    while (current) {
        if (current->has<afterhours::text_input::HasTextInputState>() && current->has<afterhours::ui::UIComponentDebug>())
            control = current->get<afterhours::ui::UIComponentDebug>().name();
        if (current->has<FocusIdentity>()) {
            auto target = current->get<FocusIdentity>().target;
            target.control = std::move(control);
            return target;
        }
        if (!current->has<afterhours::ui::UIComponent>()) break;
        const int parent = current->get<afterhours::ui::UIComponent>().parent;
        if (parent == current->id) break;
        auto next = afterhours::ui::UICollectionHolder::getEntityForID(parent);
        current = next.valid() ? *next : nullptr;
    }
    return {};
}

inline void focus_control(UIContext<InputAction>& ctx, Entity& entity) {
    if (entity.has<afterhours::text_input::HasTextInputState>() && entity.has<afterhours::ui::UIComponent>()) {
        for (const auto id : entity.get<afterhours::ui::UIComponent>().children) {
            auto child = afterhours::ui::UICollectionHolder::getEntityForID(id);
            if (child.valid() && child->has<HasClickListener>()) { ctx.set_focus(id); return; }
        }
    }
    ctx.set_focus(entity.id);
}

inline void remember_focus_origin(UIContext<InputAction>& ctx, Entity& entity) {
    if (auto* layout = ecs::find_singleton<ecs::LayoutComponent>()) layout->focus.origin = focus_target(entity);
    focus_control(ctx, entity);
}

}
