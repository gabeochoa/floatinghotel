#pragma once

#include "../ecs/ui_imports.h"

namespace ui {

inline void bind_reading_view(ecs::RepoComponent& repo, Entity& entity,
                             const std::string& view, bool ready = true) {
    auto& state = repo.reading;
    state.entity = entity.id;
    state.key = repo.repoPath + "\n" + view;
    state.request = navigation::stamp(repo, "reading-layout");
    state.ready = ready;
    state.bound = true;
    state.rows.clear();
    state.projectedLine.reset();
    state.codeRows = true;
    if (entity.has<afterhours::ui::HasScrollView>()) {
        auto& scroll = entity.get<afterhours::ui::HasScrollView>();
        scroll.anchor_child = -1;
        if (state.previousDocument != repo.workspace().active_id())
            scroll.scroll_offset = scroll.scroll_target = scroll.last_eased_offset = {0.f, 0.f};
    }
}

}
