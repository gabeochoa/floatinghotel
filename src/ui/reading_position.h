#pragma once

#include "../ecs/ui_imports.h"

namespace ui {

inline void remember_reading_position(ecs::RepoComponent& repo, Entity& entity,
    const std::string& view, bool ready = true) {
    auto& state = repo.reading;
    std::string key = repo.repoPath + "\n" + view;
    if (state.key != key) {
        if (!state.key.empty() && state.restoreFrames == 0) {
            auto previous = EntityHelper::getEntityForID(state.entity);
            if (previous.valid() && previous->has<afterhours::ui::HasScrollView>()) {
                const auto& offset = previous->get<afterhours::ui::HasScrollView>().scroll_offset;
                state.lastOffset = {offset.x, offset.y};
            }
            state.offsets[state.key] = state.lastOffset;
        }
        state.key = key;
        auto saved = state.offsets.find(key);
        state.restoringOffset = saved == state.offsets.end() ? std::pair{0.f, 0.f} : saved->second;
        state.restoreFrames = 3;
    } else if (state.entity != entity.id) {
        state.restoringOffset = state.lastOffset;
        state.restoreFrames = 3;
    }
    state.entity = entity.id;
    if (!entity.has<afterhours::ui::HasScrollView>()) return;
    auto& scroll = entity.get<afterhours::ui::HasScrollView>();
    if (!ready) {
        scroll.scroll_offset = scroll.scroll_target = scroll.last_eased_offset = {0.f, 0.f};
        return;
    }
    if (state.restoreFrames > 0) {
        scroll.scroll_offset = scroll.scroll_target = scroll.last_eased_offset =
            {state.restoringOffset.first, state.restoringOffset.second};
        scroll.anchor_child = -1;
        --state.restoreFrames;
    }
    state.lastOffset = {scroll.scroll_offset.x, scroll.scroll_offset.y};
}

}
