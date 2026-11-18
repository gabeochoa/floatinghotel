#pragma once

#include "context_menu.h"
#include "../ecs/components.h"
#include "../ecs/ui_imports.h"

namespace ui {

inline void render_comment_kind(UIContext<InputAction>& ctx, Entity& parent, int id,
        ecs::ReviewComponent& review, bool editing) {
    auto kind = review.composerDraft.kind;
    if (button(ctx, mk(parent, id), preset::Button(review_comment_kind_label(kind))
            .with_size(ComponentSize{pixels(110), pixels(26)}).with_font_size(pixels(12))
            .with_custom_background(theme::BUTTON_SECONDARY)
            .with_debug_name(editing ? "edit_comment_kind" : "compose_comment_kind"))) {
        std::vector<ContextMenuItem> choices;
        for (auto value : {ReviewCommentKind::Comment, ReviewCommentKind::Question, ReviewCommentKind::Suggestion,
                ReviewCommentKind::Blocker, ReviewCommentKind::Nit}) {
            choices.push_back(ContextMenuItem::item(review_comment_kind_label(value),
                [value, editing, scope = review.storageScope, path = review.storageRepoPath,
                 key = review.composing_key(), index = review.editing_index()] {
                    auto* active = ecs::find_singleton<ecs::ReviewComponent, ecs::ActiveTab>();
                    if (!active || active->storageScope != scope || active->storageRepoPath != path) return;
                    if (editing && active->composer == ecs::Composer::EditingExisting && active->editing_index() == index) active->composerDraft.kind = value;
                    else if (!editing && active->composer == ecs::Composer::Composing && active->composing_key() == key) active->composerDraft.kind = value;
                    else return;
                    active->mark_dirty();
                }));
        }
        show_context_menu(ctx.mouse.pos.x, ctx.mouse.pos.y, std::move(choices));
    }
}

}
