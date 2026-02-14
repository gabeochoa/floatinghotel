#pragma once

#include <algorithm>
#include <string>

#include "../input_mapping.h"
#include "../rl.h"
#include "theme.h"

namespace ui {

// Orientation of a draggable divider / split pane
enum class SplitOrientation { Horizontal, Vertical };

// ---- Draggable Divider (T016 dependency) ----

struct DividerConfig {
    SplitOrientation orientation = SplitOrientation::Vertical;
    float thickness = 4.0f;
};

// Render a draggable divider and return the pixel delta from dragging.
// `id` must be unique per divider instance.
// `position` is the current split position (pixels from left/top).
// `minPos` / `maxPos` clamp the drag range.
float draggable_divider(
    afterhours::ui::UIContext<InputAction>& ctx,
    afterhours::Entity& parent,
    int id,
    SplitOrientation orientation,
    float position,
    float minPos,
    float maxPos,
    float totalCross);

// ---- Split Pane (T017) ----

struct SplitPanelConfig {
    SplitOrientation orientation = SplitOrientation::Vertical;
    float splitPosition = 280.0f;   // Position in pixels from left/top
    float minFirst = 180.0f;        // Min size of first pane
    float minSecond = 200.0f;       // Min size of second pane
    std::string id = "split";       // Unique ID for state tracking
};

struct SplitPanelResult {
    afterhours::Entity* firstPane = nullptr;   // Entity for left/top pane
    afterhours::Entity* secondPane = nullptr;  // Entity for right/bottom pane
    float splitPosition = 0.0f;                // Updated position after drag
};

// Render a split panel with two child regions separated by a draggable divider.
//
// Usage:
//   SplitPanelConfig cfg;
//   auto result = split_panel(ctx, parentEntity, baseId, cfg, totalWidth, totalHeight);
//   // Add children to *result.firstPane ...
//   // Add children to *result.secondPane ...
//   cfg.splitPosition = result.splitPosition;  // persist for next frame
//
SplitPanelResult split_panel(
    afterhours::ui::UIContext<InputAction>& ctx,
    afterhours::Entity& parent,
    int baseId,
    SplitPanelConfig& config,
    float totalWidth,
    float totalHeight);

}  // namespace ui
