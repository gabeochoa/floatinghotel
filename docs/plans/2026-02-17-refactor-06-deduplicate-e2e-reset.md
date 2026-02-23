# Refactor 06: Deduplicate E2E Reset Logic

## Problem

App state reset logic is duplicated across 4 locations in `main.cpp`:

| Location                         | Lines     | What it resets                        |
|----------------------------------|-----------|---------------------------------------|
| `HandleMakeTestRepo`             | 71-239    | Layout, tabs, editor, menus, repo,    |
|                                  |           | scroll, toasts, modals, focus         |
| `HandleResetUI`                  | 254-282   | Layout, tabs                          |
| `e2eRunner.set_reset_callback`   | 697-722   | Layout, editor, menus                 |
| `HandleTabCommands` (reset_tabs) | 332-350   | Tabs                                  |

Each copy queries the same singleton components
(`LayoutComponent`, `TabStripComponent`, `CommitEditorComponent`,
`MenuComponent`) and resets the same fields. When a new piece of state is
added (e.g. a new dialog flag), every reset site needs updating -- and
usually one gets missed.

## Plan

1. Create `src/ecs/app_reset.h` with focused reset helpers:

```cpp
namespace ecs {

// Reset layout to default state
void reset_layout_defaults(LayoutComponent& layout);

// Close all tabs except the first, activate it
void reset_tabs(TabStripComponent& tabStrip, LayoutComponent& layout);

// Clear commit editor state
void reset_commit_editor(CommitEditorComponent& editor);

// Close all menus
void reset_menus(MenuComponent& menu);

// Clear scroll offsets, toasts, modals, focus (full UI reset)
void reset_ui_transient_state();

// Convenience: resets everything above
void reset_all_app_state();

} // namespace ecs
```

2. Replace the inline reset code in all 4 locations with calls to these
   helpers.

3. `HandleMakeTestRepo` calls `reset_all_app_state()` then does its
   repo-specific work (load test repo, sync git data).

4. `HandleResetUI` calls `reset_layout_defaults()` + `reset_tabs()`.

5. `set_reset_callback` calls `reset_layout_defaults()` +
   `reset_commit_editor()` + `reset_menus()`.

6. `HandleTabCommands::reset_tabs` calls `ecs::reset_tabs()`.

## Risk

Low. The reset behavior is well-defined and currently consistent across
copies. Extracting it into shared functions changes no behavior.

One thing to watch: `HandleMakeTestRepo` does additional repo-specific
clearing (selected file, branch dialogs, etc.) that the other resets
don't need. The helper should not include repo-specific clearing --
`HandleMakeTestRepo` handles that itself after calling the shared reset.

## Estimated impact

- ~120 lines of duplicated inline code replaced by ~30 lines of helper calls
- 1 new file (~60 lines)
- Future state additions need updating in 1 place instead of 4
