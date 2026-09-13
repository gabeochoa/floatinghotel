#pragma once

#include "ui_imports.h"
#include "../ui/diff_renderer.h"
#include "../ui/geometry.h"

namespace ecs {

struct ReadingLayoutSystem : afterhours::System<UIContext<InputAction>> {
    void for_each_with(Entity&, UIContext<InputAction>& ctx, float) override {
        auto* repo = find_singleton<RepoComponent, ActiveTab>();
        if (!repo || !repo->reading.bound) return;
        auto& state = repo->reading;
        state.bound = false;
        if (!state.request || !navigation::accepts(*repo, *state.request, "reading-layout")) return;
        auto entity = afterhours::ui::UICollectionHolder::getEntityForID(state.entity);
        if (!entity.valid() || !entity->has<afterhours::ui::HasScrollView>()) return;
        auto& scroll = entity->get<afterhours::ui::HasScrollView>();
        const auto viewport = ui::visible_rect(**entity);
        if (viewport.width <= 0.f || viewport.height <= 0.f || !scroll.viewport_size) return;
        const auto wheel = afterhours::input::get_mouse_wheel_move_v();
        const bool userScroll = scroll.dragging_scrollbar ||
            ((wheel.x != 0.f || wheel.y != 0.f) && afterhours::ui::is_mouse_inside(ctx.mouse.pos, viewport));
        if (userScroll) navigation::cancel_anchor(*repo);
        if (!state.ready || !repo->hasLoadedOnce || repo->isRefreshing || repo->refreshRequested ||
            (source_tab_active(*repo) && repo->fullFileFuture.valid())) return;
        const auto* document = repo->workspace().document(repo->workspace().active_id());
        const bool changedLayout = state.previousDocument == document->id &&
            (state.previousKey != state.key || std::fabs(state.width - viewport.width) > .5f || std::fabs(state.height - viewport.height) > .5f ||
             std::fabs(state.contentHeight - scroll.content_size.y) > .5f);
        const bool revealing = repo->fullFileNavigateFrames > 0 || repo->diffTargetFrames > 0;
        if (changedLayout && !userScroll && !revealing) navigation::restore_anchor(*repo);
        std::optional<reading::ReadingAnchor> sample;
        std::optional<float> targetY;
        int nearestDistance = std::numeric_limits<int>::max();
        if (state.codeRows) for (const auto& row : ui::diff_sel::state().lastLines) {
            auto lineEntity = afterhours::ui::UICollectionHolder::getEntityForID(row.ent);
            if (!lineEntity.valid() || !lineEntity->has<afterhours::ui::UIComponent>()) continue;
            const auto rect = ui::screen_rect(**lineEntity);
            if (rect.height <= 0.f) continue;
            if (document->restoreAnchor && document->anchor && row.filePath == document->anchor->path) {
                const auto& anchor = *document->anchor;
                const int line = anchor.side == reading::DiffSide::Before ? row.oldLine : row.newLine;
                if (line > 0) {
                    const int distance = std::abs(line - anchor.line);
                    const int end = row.logicalColumn + reading::column_at_byte(row.content, row.content.size()) - 1;
                    const bool matches = line == anchor.line && anchor.column >= row.logicalColumn && anchor.column <= end;
                    if (matches || (state.projectedLine == line && nearestDistance >= 0 && distance < nearestDistance)) {
                        targetY = rect.y + scroll.scroll_offset.y - viewport.y;
                        nearestDistance = matches ? -1 : distance;
                    }
                }
            }
            const float y = rect.y - viewport.y;
            if (!sample && y >= 36.f * ui::zoom::get() && y < viewport.height) {
                const bool before = row.sign == '-' || row.side == 1;
                const int line = before ? row.oldLine : row.newLine;
                if (line > 0) sample = reading::ReadingAnchor{row.filePath, reading::anchor_revision(document->location),
                    before ? reading::DiffSide::Before : reading::DiffSide::After, line, row.logicalColumn,
                    y / viewport.height, row.sign};
            }
        }
        for (const auto& row : state.rows) {
            auto lineEntity = afterhours::ui::UICollectionHolder::getEntityForID(row.entity);
            if (!lineEntity.valid() || !lineEntity->has<afterhours::ui::UIComponent>()) continue;
            const auto rect = ui::screen_rect(**lineEntity);
            if (document->restoreAnchor && document->anchor && row.path == document->anchor->path) {
                const auto& anchor = *document->anchor;
                const bool before = anchor.side == reading::DiffSide::Before;
                const int begin = before ? row.oldStart : row.newStart;
                const int end = before ? row.oldEnd : row.newEnd;
                if (anchor.line >= begin && anchor.line <= end && anchor.column >= row.column && anchor.column <= row.endColumn)
                    targetY = rect.y + scroll.scroll_offset.y - viewport.y;
            }
            const float y = rect.y - viewport.y;
            if (!row.folded && !sample && y >= 36.f * ui::zoom::get() && y < viewport.height)
                sample = reading::ReadingAnchor{row.path, reading::anchor_revision(document->location),
                    reading::DiffSide::After, row.newStart, row.column, y / viewport.height, ' '};
        }
        const bool applyingAnchor = document->restoreAnchor && document->anchor && targetY;
        if (applyingAnchor) {
            const float target = std::clamp(*targetY - document->anchor->viewportFraction * viewport.height,
                0.f, std::max(0.f, scroll.content_size.y - viewport.height));
            scroll.scroll_offset.y = scroll.scroll_target.y = scroll.last_eased_offset.y = target;
            scroll.anchor_child = -1;
            navigation::restored_anchor(*repo);
        } else if (!revealing && sample && (!document->anchor || state.wasRevealing || state.offset != scroll.scroll_offset.y)) {
            navigation::remember_anchor(*repo, std::move(*sample));
        }
        state.wasRevealing = revealing && !applyingAnchor;
        state.previousDocument = document->id;
        state.previousKey = state.key;
        state.width = viewport.width;
        state.height = viewport.height;
        state.contentHeight = scroll.content_size.y;
        state.offset = scroll.scroll_offset.y;
    }
};

}
