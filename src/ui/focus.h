#pragma once

#include "../ecs/ui_imports.h"
#include "geometry.h"
#include "context_menu.h"
#include <afterhours/src/plugins/ui/text_input/text_area_state.h>

namespace ui {

struct FocusIdentity : afterhours::BaseComponent {
    reading::focus::Target target;
};

inline void bind_focus(Entity& entity, const ecs::RepoComponent& repo, reading::focus::Region region,
                       std::string item = {}, std::optional<reading::DocumentId> document = {}) {
    const auto owner = document.value_or(region == reading::focus::Region::History || region == reading::focus::Region::Menu ||
        region == reading::focus::Region::Search || region == reading::focus::Region::SearchPreview
        ? reading::DocumentId{} : repo.workspace().active_id());
    entity.addComponentIfMissing<FocusIdentity>().target = {repo.repoPath, owner, region, std::move(item), {}};
}

inline void bind_focus_region(Entity& entity, const ecs::RepoComponent& repo, reading::focus::Region region) {
    entity.addComponentIfMissing<HasClickListener>([](Entity&) {});
    if (entity.has<afterhours::HasColor>()) entity.get<afterhours::HasColor>().skip_hover_override = true;
    bind_focus(entity, repo, region);
}

inline bool text_control(const Entity& entity) {
    return entity.has<afterhours::text_input::HasTextInputState>() ||
           entity.has<afterhours::text_input::HasTextAreaState>();
}

inline std::optional<reading::focus::Target> focus_target(Entity& entity) {
    std::string control = entity.has<afterhours::ui::UIComponentDebug>() ? entity.get<afterhours::ui::UIComponentDebug>().name() : "";
    Entity* current = &entity;
    while (current) {
        if (text_control(*current) && current->has<afterhours::ui::UIComponentDebug>())
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

inline reading::focus::ShortcutOwner shortcut_owner(UIContext<InputAction>& ctx, const ecs::RepoComponent& repo) {
    reading::focus::ShortcutOwner owner;
    auto focused = afterhours::ui::UICollectionHolder::getEntityForID(ctx.focus_id);
    if (!focused.valid()) return owner;
    auto target = focus_target(**focused);
    if (target && target->repository == repo.repoPath &&
        (target->document.value == 0 || target->document == repo.workspace().active_id())) owner.region = target->region;
    Entity* current = *focused;
    while (current) {
        if (text_control(*current)) { owner.text = true; break; }
        if (!current->has<afterhours::ui::UIComponent>()) break;
        const int parent = current->get<afterhours::ui::UIComponent>().parent;
        if (parent == current->id) break;
        auto next = afterhours::ui::UICollectionHolder::getEntityForID(parent);
        current = next.valid() ? *next : nullptr;
    }
    return owner;
}

inline bool shortcuts_blocked(const ecs::LayoutComponent& layout) {
    const auto* menu = ecs::find_singleton<ecs::MenuComponent>();
    return layout.shortcutsOpen || is_context_menu_open() || (menu && menu->activeMenuIndex >= 0);
}

inline bool reader_visible(const ecs::RepoComponent& repo, const ecs::LayoutComponent& layout) {
    return !layout.shelfCollapsed && !layout.filePickerOpen &&
        !repo.commitSearchOpen && !repo.fileHistoryOpen;
}

inline bool history_shortcuts(UIContext<InputAction>& ctx, const ecs::RepoComponent& repo, const ecs::LayoutComponent& layout) {
    return !shortcuts_blocked(layout) && reader_visible(repo, layout) && !shortcut_owner(ctx, repo).text;
}

inline bool reader_shortcuts(UIContext<InputAction>& ctx, const ecs::RepoComponent& repo, const ecs::LayoutComponent& layout) {
    const auto* review = ecs::find_singleton<ecs::ReviewComponent, ecs::ActiveTab>();
    return !shortcuts_blocked(layout) && reader_visible(repo, layout) && (!review || !review->sinceReviewOpen) &&
        shortcut_owner(ctx, repo).reader();
}

inline std::vector<reading::focus::Popup> open_popups(const ecs::RepoComponent& repo, const ecs::LayoutComponent& layout) {
    using reading::focus::Popup;
    std::vector<Popup> visible;
    if (const auto* menu = ecs::find_singleton<ecs::MenuComponent>(); menu && menu->activeMenuIndex >= 0) visible.push_back(Popup::Menu);
    if (is_context_menu_open()) visible.push_back(Popup::ContextMenu);
    if (layout.filePickerOpen) visible.push_back(Popup::Picker);
    if (repo.workspace().document(repo.workspace().active_id())->find.open) visible.push_back(Popup::Find);
    if (repo.repoSearchOpen) visible.push_back(Popup::Search);
    if (repo.repoSearchOpen && repo.repoSearchPreviewOpen) visible.push_back(Popup::SearchPreview);
    if (const auto* review = ecs::find_singleton<ecs::ReviewComponent, ecs::ActiveTab>()) {
        if (review->basketOpen && layout.feedback.width > 0.f) visible.push_back(Popup::Feedback);
        if (!review->composingKey.empty()) visible.push_back(Popup::Composer);
        if (review->sinceReviewOpen) visible.push_back(Popup::Snapshot);
    }
    if (repo.comparisonEditorOpen) visible.push_back(Popup::ComparisonEditor);
    if (repo.commitSearchOpen) visible.push_back(Popup::CommitSearch);
    if (repo.fileHistoryOpen) visible.push_back(Popup::FileHistory);
    if (layout.diffOptionsOpen) visible.push_back(Popup::Options);
    return visible;
}

inline void focus_control(UIContext<InputAction>& ctx, Entity& entity) {
    if (text_control(entity) && entity.has<afterhours::ui::UIComponent>()) {
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
