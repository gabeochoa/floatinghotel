# Multi-Tab Design

## Overview

Add browser-style tabs to floatinghotel. Each tab is a fully independent workspace with its own repo, sidebar state, and diff selection. Tab strip sits at the very top of the window, above the menu bar.

## Data Model

Each tab is its own ECS entity with a `Tab` component and its own `RepoComponent` + `CommitEditorComponent`. The active tab is identified by an `ActiveTab` marker component.

```cpp
struct ActiveTab : public afterhours::BaseComponent {};

struct Tab : public afterhours::BaseComponent {
    std::string label;  // e.g. "myproject (main)"

    // Per-tab view state
    LayoutComponent::SidebarMode sidebarMode = SidebarMode::Changes;
    LayoutComponent::FileViewMode fileViewMode = FileViewMode::Flat;
    LayoutComponent::DiffViewMode diffViewMode = DiffViewMode::Inline;
    bool sidebarVisible = true;
};
```

Each tab entity also owns:
- `RepoComponent` — full repo state (file list, commit log, diffs, selections)
- `CommitEditorComponent` — in-progress commit message, amend state, unstaged policy

`LayoutComponent` stays as a shared singleton for window geometry and layout rects. The "which mode" fields (sidebar mode, diff view mode, etc.) move into `Tab`.

### Active Tab Queries

All existing systems that query `RepoComponent` must add `.whereHasComponent<ActiveTab>()` to their queries. This is a one-line change per call site. Example:

```cpp
// Before
auto q = EntityQuery({.force_merge = true})
    .whereHasComponent<RepoComponent>().gen();

// After
auto q = EntityQuery({.force_merge = true})
    .whereHasComponent<RepoComponent>()
    .whereHasComponent<ActiveTab>().gen();
```

### CommitEditorComponent Ownership

Per-tab. Each tab entity gets its own `CommitEditorComponent`. If you're mid-commit on tab 1 and switch to tab 2, your typed message stays on tab 1's entity untouched. No sync needed — the commit editor UI just queries the active tab's `CommitEditorComponent`.

## Tab Strip Rendering & Interaction

A `TabStripComponent` singleton holds tab ordering:

```cpp
struct TabStripComponent : public afterhours::BaseComponent {
    std::vector<afterhours::EntityID> tabOrder;
};
```

Insertion order is the vector order. Close removes an element. Reorder is a swap. No sorting needed.

The `TabBarSystem` runs in `once()`, iterates `tabOrder`, renders each tab left-to-right as a horizontal strip at the top of the window.

- **Click tab** → move `ActiveTab` marker from old tab to clicked tab, sync view state
- **Click "+" / Cmd+T** → spawn new tab entity, add to end of `tabOrder`, move `ActiveTab` to it
- **Click "x" / Cmd+W** → destroy tab entity, remove from `tabOrder`. Activate nearest neighbor. Always keep at least 1 tab (hide "x" when only one)
- **Overflow** → tabs shrink to a min-width, then the strip scrolls horizontally

### Tab Labels

Format: `basename(repoPath)` + ` (branch)` when space allows. Examples:
- `myproject (main)`
- `floatinghotel (feature/tabs)`
- `Untitled` for a fresh tab with no repo

Label updates whenever the branch changes.

## Open Repository Behavior

- **Open Repository (Cmd+O)** → replaces the current active tab's `RepoComponent` with the new repo
- **Shift+Open Repository** → spawns a new tab entity with its own `RepoComponent` pointing at the new repo

## Integration with Existing Systems

### TabSyncSystem

Runs early each frame. When the active tab changes:

1. Save outgoing tab: copy `LayoutComponent` view modes (sidebar mode, diff view, file view, sidebar visible) → outgoing `Tab` component
2. Load incoming tab: copy incoming `Tab` view modes → `LayoutComponent`

That's all it does. It does NOT swap `RepoComponent` or `CommitEditorComponent` — those live directly on the tab entity, and systems query them via the `ActiveTab` marker.

### AsyncGitDataRefreshSystem

Currently `System<RepoComponent>`, so `for_each_with` fires for every tab's `RepoComponent`. Inactive tabs should NOT auto-refresh. Add an early return:

```cpp
if (!entity.has<ActiveTab>()) return;
```

Background refresh for inactive tabs can be added later as an optimization.

## Implementation Plan

1. Add `Tab`, `ActiveTab`, `TabStripComponent` to `components.h`
2. Update all `RepoComponent` / `CommitEditorComponent` queries to include `.whereHasComponent<ActiveTab>()`
3. Add `TabBarSystem` — renders the strip, handles click/create/close
4. Add `TabSyncSystem` — syncs active tab view modes to/from `LayoutComponent`
5. Adjust `LayoutSystem` — account for tab strip height in layout rects
6. Spawn default tab entity in `main.cpp` initialization (with `ActiveTab` marker)
7. Wire Cmd+T (new tab), Cmd+W (close tab)
8. Update "Open Repository" to replace current tab / Shift+open for new tab
9. Update `SettingsComponent` to persist all open tab repos and restore on launch

## TODOs (Future)

- **Dotfile persistence**: Save tab state, layout prefs, open repos to a `.floatinghotel/` dotfile in the repo root (or a global config). Restore on next launch.
- **Memory optimization**: Minimize memory usage of inactive tabs — lazy-load or clear cached diffs, commit logs, and scroll state for tabs that aren't visible.
- **Scroll position save/restore**: The `HasScrollView::scroll_offset` lives on UI entities that are recreated each frame (immediate-mode). Saving/restoring scroll position on tab switch requires framework support (a keyed scroll state registry). Defer until afterhours provides this.
- **Multi-tab same repo**: Allow multiple tabs pointing at the same repo with shared `RepoComponent` (pooled by path).
- **Contextual tab opening**: Double-click file or commit to open in a new tab.
- **Background refresh**: Allow inactive tabs to refresh repo data on a timer (opt-in).
