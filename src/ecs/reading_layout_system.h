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
        if (!state.request || !navigation::accepts(*repo, *state.request, navigation::reading_layout_key(*repo))) return;
        auto entity = afterhours::ui::UICollectionHolder::getEntityForID(state.entity);
        if (!entity.valid() || !entity->has<afterhours::ui::HasScrollView>()) return;
        auto& scroll = entity->get<afterhours::ui::HasScrollView>();
        const float positionedOffset = scroll.scroll_offset.y;
        const auto viewport = ui::reading_rect(**entity);
        if (viewport.width <= 0.f || viewport.height <= 0.f || !scroll.viewport_size) return;
        const auto wheel = afterhours::input::get_mouse_wheel_move_v();
        const bool userScroll = scroll.dragging_scrollbar ||
            ((wheel.x != 0.f || wheel.y != 0.f) && afterhours::ui::is_mouse_inside(ctx.mouse.pos, ui::visible_rect(**entity)));
        if (userScroll) navigation::cancel_anchor(*repo);
        if (!state.ready || !repo->hasLoadedOnce || repo->isRefreshing || repo->refreshRequested ||
            (source_tab_active(*repo) && repo->fullFileFuture.valid() && !repo->fullFileExtendRequest)) return;
        const auto* document = repo->workspace().document(repo->workspace().active_id());
        const bool changedLayout = state.previousDocument == document->id &&
            (state.previousKey != state.key || std::fabs(state.width - viewport.width) > .5f || std::fabs(state.height - viewport.height) > .5f ||
             (!(source_tab_active(*repo) && state.codeRows) && std::fabs(state.contentHeight - scroll.content_size.y) > .5f));
        const auto* review = find_singleton<ReviewComponent, ActiveTab>();
        const bool composing = review && !review->composingKey.empty() && !source_tab_active(*repo) &&
            review->composingScope == reading::scope(repo->workspace().review());
        const bool revealing = repo->fullFileNavigateFrames > 0 || repo->diffTargetFrames > 0;
        if (changedLayout && !userScroll && !revealing && !composing) navigation::restore_anchor(*repo);
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
                    const bool matches = line == anchor.line && anchor.column >= row.logicalColumn &&
                        (anchor.column < end || (row.finalFragment && anchor.column == end));
                    if (matches || (distance > 0 && state.projectedLine == line && nearestDistance >= 0 && distance < nearestDistance)) {
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
        if (std::getenv("FH_TRACE_READING") && (userScroll || changedLayout || state.offset != scroll.scroll_offset.y))
            log_info("Reading layout offset {} previous {} content {} previousContent {} userScroll {} changedLayout {} sample {} anchor {} restoring {}",
                scroll.scroll_offset.y, state.offset, scroll.content_size.y, state.contentHeight, userScroll, changedLayout,
                sample ? sample->line : 0, document->anchor ? document->anchor->line : 0, document->restoreAnchor);
        const bool applyingAnchor = !composing && document->restoreAnchor && document->anchor && targetY;
        if (applyingAnchor) {
            const float target = std::clamp(*targetY - document->anchor->viewportFraction * viewport.height,
                0.f, std::max(0.f, scroll.content_size.y - scroll.viewport_or_zero().y));
            scroll.scroll_offset.y = scroll.scroll_target.y = scroll.last_eased_offset.y = target;
            scroll.anchor_child = -1;
            if (std::getenv("FH_TRACE_READING")) log_info("Reading restore {} line {} column {} fraction {} target {} viewport {} content {}", document->anchor->path, document->anchor->line, document->anchor->column, document->anchor->viewportFraction, target, viewport.height, scroll.content_size.y);
            navigation::restored_anchor(*repo);
        } else if (!composing && !revealing && sample && (!document->anchor || state.wasRevealing || state.offset != scroll.scroll_offset.y)) {
            navigation::remember_anchor(*repo, std::move(*sample));
        }
        if (composing && !userScroll && (changedLayout || state.revealEditor)) {
            auto editor = afterhours::ui::UICollectionHolder::getEntityForID(state.editorEntity);
            if (editor.valid() && editor->has<afterhours::ui::UIComponent>()) {
                const auto rect = ui::screen_rect(**editor);
                float delta = 0.f;
                if (rect.y < viewport.y || rect.height > viewport.height) delta = rect.y - viewport.y;
                else if (rect.y + rect.height > viewport.y + viewport.height)
                    delta = rect.y + rect.height - viewport.y - viewport.height;
                const auto offset = std::clamp(scroll.scroll_offset.y + delta, 0.f,
                    std::max(0.f, scroll.content_size.y - scroll.viewport_or_zero().y));
                scroll.scroll_offset.y = scroll.scroll_target.y = scroll.last_eased_offset.y = offset;
                scroll.anchor_child = -1;
            }
        }
        if (positionedOffset != scroll.scroll_offset.y) {
            ui::PinFileHeaders pin;
            for (const auto id : entity->get<afterhours::ui::UIComponent>().children) {
                auto header = afterhours::ui::UICollectionHolder::getEntityForID(id);
                if (header.valid() && header->has<ui::StickyFileHeader>())
                    pin.for_each_with(header.asE(), header->get<ui::StickyFileHeader>(),
                        header->get<afterhours::ui::UIComponent>(), 0.f);
            }
        }
        state.wasRevealing = revealing && !applyingAnchor;
        state.previousDocument = document->id;
        state.previousKey = state.key;
        state.width = viewport.width;
        state.height = viewport.height;
        state.contentHeight = scroll.content_size.y;
        if (sample || applyingAnchor) state.offset = scroll.scroll_offset.y;
    }
};

}
