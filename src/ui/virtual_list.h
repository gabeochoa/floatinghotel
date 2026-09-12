#pragma once

#include <concepts>
#include <utility>

#include <afterhours/src/plugins/ui/imm_components.h>
#include "zoom.h"

namespace ui {

template <typename Height, typename RenderRow>
auto virtual_list(auto& ctx, afterhours::ui::imm::EntityParent parent,
                  size_t count, Height height, RenderRow&& renderRow,
                  afterhours::ui::imm::ComponentConfig config = {}) {
    const float scale = zoom::get();
    auto result = [&] {
        if constexpr (std::invocable<Height, size_t>) {
            return afterhours::ui::imm::virtual_list(ctx, parent, count,
                [&](size_t index) { return height(index) * scale; },
                std::forward<RenderRow>(renderRow), config);
        } else {
            return afterhours::ui::imm::virtual_list(ctx, parent, count,
                height * scale, std::forward<RenderRow>(renderRow), config);
        }
    }();
    auto& list = result.ent().template get<afterhours::ui::UIComponent>();
    list.skip_grid_snap = true;
    for (const auto id : list.children) {
        auto child = afterhours::ui::UICollectionHolder::getEntityForID(id);
        if (!child.valid() || !child->template has<afterhours::ui::UIComponent>()) continue;
        auto& component = child->template get<afterhours::ui::UIComponent>();
        component.skip_grid_snap = true;
        auto& desired = component.desired[afterhours::ui::Axis::Y];
        if (desired.dim == afterhours::ui::Dim::Pixels) desired.value /= scale;
    }
    return result;
}

}
