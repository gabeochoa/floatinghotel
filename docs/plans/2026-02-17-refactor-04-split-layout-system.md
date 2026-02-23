# Refactor 04: Split layout_system.h Into Focused Files

## Problem

`src/ecs/layout_system.h` is 1,380 lines containing 4 unrelated
responsibilities in a single header:

| Responsibility             | Lines  | What it does                               |
|----------------------------|--------|--------------------------------------------|
| `LayoutUpdateSystem`       | ~170   | Recalculates panel rects each frame        |
| `render_command_log`       | ~120   | Renders the git command log panel          |
| `commit_detail_view` + `render_commit_detail` | ~530 | Commit detail parsing + UI |
| `MainContentSystem`        | ~400   | Main content area: diff, welcome, empty    |
| Misc helpers               | ~30    | `format_timestamp`                         |

`LayoutUpdateSystem` is a pure geometry calculator with no UI rendering.
It has no reason to share a file with `MainContentSystem` (a UI renderer)
or `render_commit_detail` (commit detail UI).

## Plan

### New file structure

```
src/ecs/layout_system.h          (~170 lines) - LayoutUpdateSystem only
src/ecs/main_content_system.h    (~400 lines) - MainContentSystem
src/ui/commit_detail.h           (~530 lines) - commit_detail_view + render_commit_detail
src/ui/command_log.h             (~120 lines) - render_command_log + format_timestamp
```

### Steps

1. Create `src/ui/command_log.h`:
   - Move `format_timestamp` and `render_command_log` from `layout_system.h`.

2. Create `src/ui/commit_detail.h`:
   - Move the `commit_detail_view` namespace (parse helpers, `CommitInfo`).
   - Move `render_commit_detail`.
   - After refactor 01, the helpers come from `src/util/git_helpers.h`
     instead of being inlined here.

3. Create `src/ecs/main_content_system.h`:
   - Move `MainContentSystem` (includes `render_welcome_screen`,
     `render_sidebar_divider`).
   - Include `commit_detail.h` and `command_log.h`.

4. Update `layout_system.h`:
   - Remove everything except `LayoutUpdateSystem`.

5. Update `main.cpp`:
   - Replace `#include "ecs/layout_system.h"` with includes for all
     new headers (or just `main_content_system.h` which pulls in the rest).

## Risk

Low. Pure file reorganization. No logic changes. The `using` declarations
move with their consumers.

## Estimated impact

- `layout_system.h` drops from 1,380 to ~170 lines
- 3 new focused files, each under 530 lines
- Each file has a single clear responsibility
- Faster incremental compiles (changing commit detail UI doesn't recompile layout)
