#include "split_panel.h"

#include <cmath>

namespace ui {

using afterhours::Entity;
using afterhours::ui::UIContext;
using afterhours::ui::imm::ComponentConfig;
using afterhours::ui::imm::ElementResult;
using afterhours::ui::imm::div;
using afterhours::ui::imm::mk;
using afterhours::ui::pixels;
using afterhours::ui::percent;
using afterhours::ui::ComponentSize;
using afterhours::ui::FlexDirection;
using afterhours::ui::AlignItems;
using afterhours::ui::HasDragListener;

// ---- Draggable Divider ----

float draggable_divider(
    UIContext<InputAction>& ctx,
    Entity& parent,
    int id,
    SplitOrientation orientation,
    float position,
    float minPos,
    float maxPos,
    float totalCross) {

    bool isVertical = (orientation == SplitOrientation::Vertical);

    // Divider size: thin along split axis, full extent along cross axis
    constexpr float dividerThickness = 4.0f;
    float divW = isVertical ? dividerThickness : totalCross;
    float divH = isVertical ? totalCross : dividerThickness;

    auto divider = div(ctx, mk(parent, id),
        ComponentConfig{}
            .with_size(ComponentSize{pixels(divW), pixels(divH)})
            .with_custom_background(theme::BORDER)
            .with_roundness(0.0f)
            .with_debug_name("draggable_divider"));

    // Attach drag listener
    divider.ent().addComponentIfMissing<HasDragListener>(
        [](Entity& /*e*/) {});
    auto& drag = divider.ent().get<HasDragListener>();

    float delta = 0.0f;
    if (drag.down) {
        auto mousePos = afterhours::graphics::get_mouse_position();
        float mouseVal = isVertical
            ? static_cast<float>(mousePos.x)
            : static_cast<float>(mousePos.y);

        // Clamp mouse position to valid range
        float clamped = std::clamp(mouseVal, minPos, maxPos);
        delta = clamped - position;
    }

    return delta;
}

// ---- Split Pane ----

SplitPanelResult split_panel(
    UIContext<InputAction>& ctx,
    Entity& parent,
    int baseId,
    SplitPanelConfig& config,
    float totalWidth,
    float totalHeight) {

    SplitPanelResult result;

    bool isVertical = (config.orientation == SplitOrientation::Vertical);
    constexpr float dividerThickness = 4.0f;
    float totalSize = isVertical ? totalWidth : totalHeight;
    float crossSize = isVertical ? totalHeight : totalWidth;

    // Compute max position for first pane
    float maxFirst = totalSize - config.minSecond - dividerThickness;
    float minFirst = config.minFirst;

    // Clamp current split position
    config.splitPosition = std::clamp(config.splitPosition, minFirst, maxFirst);

    // Outer container
    auto direction = isVertical ? FlexDirection::Row : FlexDirection::Column;
    auto outer = div(ctx, mk(parent, baseId),
        ComponentConfig{}
            .with_size(ComponentSize{pixels(totalWidth), pixels(totalHeight)})
            .with_flex_direction(direction)
            .with_align_items(AlignItems::Stretch)
            .with_custom_background(theme::WINDOW_BG)
            .with_roundness(0.0f)
            .with_debug_name("split_panel_outer"));

    // First pane (left or top)
    float firstSize = config.splitPosition;
    ComponentSize firstPaneSize = isVertical
        ? ComponentSize{pixels(firstSize), pixels(totalHeight)}
        : ComponentSize{pixels(totalWidth), pixels(firstSize)};

    auto firstPane = div(ctx, mk(outer.ent(), baseId + 1),
        ComponentConfig{}
            .with_size(firstPaneSize)
            .with_flex_direction(FlexDirection::Column)
            .with_custom_background(theme::SIDEBAR_BG)
            .with_roundness(0.0f)
            .with_debug_name("split_first_pane"));

    result.firstPane = &firstPane.ent();

    // Draggable divider between the two panes
    // The divider position is relative to the outer container's origin,
    // which is the parent's position. We pass the absolute-ish offset.
    float delta = draggable_divider(
        ctx, outer.ent(), baseId + 2,
        config.orientation,
        config.splitPosition,
        minFirst, maxFirst,
        crossSize);

    config.splitPosition = std::clamp(config.splitPosition + delta,
                                       minFirst, maxFirst);
    result.splitPosition = config.splitPosition;

    // Second pane (right or bottom) fills remaining space
    float secondSize = totalSize - config.splitPosition - dividerThickness;
    if (secondSize < 0.0f) secondSize = 0.0f;

    ComponentSize secondPaneSize = isVertical
        ? ComponentSize{pixels(secondSize), pixels(totalHeight)}
        : ComponentSize{pixels(totalWidth), pixels(secondSize)};

    auto secondPane = div(ctx, mk(outer.ent(), baseId + 3),
        ComponentConfig{}
            .with_size(secondPaneSize)
            .with_flex_direction(FlexDirection::Column)
            .with_custom_background(theme::PANEL_BG)
            .with_roundness(0.0f)
            .with_debug_name("split_second_pane"));

    result.secondPane = &secondPane.ent();

    return result;
}

}  // namespace ui
