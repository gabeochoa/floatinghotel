# Refactor 05: Consolidate Using Declarations

## Problem

Every ECS system header starts with a near-identical block of 20-30
`using` declarations importing `afterhours::ui::*` names:

```cpp
using afterhours::Entity;
using afterhours::ui::UIContext;
using afterhours::ui::imm::ComponentConfig;
using afterhours::ui::imm::div;
using afterhours::ui::imm::button;
using afterhours::ui::imm::mk;
using afterhours::ui::pixels;
using afterhours::ui::h720;
using afterhours::ui::w1280;
using afterhours::ui::percent;
using afterhours::ui::children;
using afterhours::ui::FlexDirection;
using afterhours::ui::AlignItems;
using afterhours::ui::JustifyContent;
using afterhours::ui::ComponentSize;
using afterhours::ui::Padding;
using afterhours::ui::Margin;
using afterhours::ui::TextAlignment;
using afterhours::ui::HasClickListener;
// ... more ...
```

This block appears in 6 system headers and `diff_renderer.h` (~150 total
duplicated lines). When a new UI type is needed, every file needs updating.

## Files affected

- `src/ecs/layout_system.h` (28 using declarations)
- `src/ecs/sidebar_system.h` (27 using declarations)
- `src/ecs/toolbar_system.h` (18 using declarations)
- `src/ecs/status_bar_system.h` (14 using declarations)
- `src/ecs/menu_bar_system.h` (16 using declarations)
- `src/ecs/tab_bar_system.h` (17 using declarations)
- `src/ui/diff_renderer.h` (16 using declarations)

## Plan

1. Create `src/ecs/ui_imports.h`:

```cpp
#pragma once

#include "../../vendor/afterhours/src/core/system.h"
#include "../input_mapping.h"
#include "../rl.h"
#include "../ui/theme.h"
#include "../ui_context.h"

namespace ecs {

using afterhours::Entity;
using afterhours::EntityHelper;
using afterhours::ui::UIContext;
using afterhours::ui::imm::ComponentConfig;
using afterhours::ui::imm::div;
using afterhours::ui::imm::button;
using afterhours::ui::imm::hstack;
using afterhours::ui::imm::mk;
using afterhours::ui::imm::checkbox;
using afterhours::ui::pixels;
using afterhours::ui::h720;
using afterhours::ui::w1280;
using afterhours::ui::expand;
using afterhours::ui::percent;
using afterhours::ui::children;
using afterhours::ui::FlexDirection;
using afterhours::ui::AlignItems;
using afterhours::ui::JustifyContent;
using afterhours::ui::ComponentSize;
using afterhours::ui::Padding;
using afterhours::ui::Margin;
using afterhours::ui::TextAlignment;
using afterhours::ui::FontSize;
using afterhours::ui::HasClickListener;
using afterhours::ui::HasDragListener;
using afterhours::ui::ClickActivationMode;
using afterhours::ui::Overflow;
using afterhours::ui::Axis;
using afterhours::ui::resolve_to_pixels;

} // namespace ecs
```

2. Replace the per-file `using` blocks with `#include "ui_imports.h"` in
   each system header.

3. For `src/ui/diff_renderer.h` (which uses `namespace ui` not `ecs`),
   either create a similar `src/ui/ui_imports.h` or have it include the
   ecs one and add a few local aliases.

## Risk

None. `using` declarations are purely syntactic. The compiled output is
identical.

## Estimated impact

- ~150 lines of duplication removed
- 1 new file (~35 lines)
- Adding a new UI type to the project requires editing 1 file instead of 7
