# Toolbar & Menu Button Audit

**Date:** 2026-02-17
**Status:** Draft

## Problem

22 of 27 menu items and 5 of 8 toolbar buttons do nothing when clicked. The infrastructure (git commands, layout state) exists but isn't wired to the UI. There are no e2e tests validating that any button actually performs its action.

## Full Inventory

### File Menu

| # | Item | Shortcut | Current State | Action Needed | Tier |
|---|------|----------|---------------|---------------|------|
| F1 | Open Repository... | Cmd+O | nullptr | Needs file dialog or text input modal | B |
| F2 | Close Tab | Cmd+W | nullptr | Unclear semantics (single-repo app) | C |
| F3 | Quit | Cmd+Q | **Works** | None | - |

### Edit Menu

| # | Item | Shortcut | Current State | Action Needed | Tier |
|---|------|----------|---------------|---------------|------|
| E1 | Copy | Cmd+C | nullptr | Needs clipboard integration | B |
| E2 | Select All | Cmd+A | nullptr | Needs text selection system | B |
| E3 | Find... | Cmd+F | nullptr | Needs search UI | B |

### View Menu

| # | Item | Shortcut | Current State | Action Needed | Tier |
|---|------|----------|---------------|---------------|------|
| V1 | Toggle Sidebar | Cmd+B | nullptr | Wire to `layout.sidebarVisible` | **A** |
| V2 | Toggle Command Log | Cmd+Shift+L | nullptr | Wire to `layout.commandLogVisible` | **A** |
| V3 | Inline Diff | Cmd+Shift+I | nullptr | Wire to `layout.diffViewMode = Inline` | **A** |
| V4 | Side-by-Side Diff | Cmd+Shift+D | nullptr | Wire to `layout.diffViewMode = SideBySide` | **A** |
| V5 | Changed Files View | - | nullptr | Wire to `layout.fileViewMode = Flat` | **A** |
| V6 | Tree View | - | nullptr | Wire to `layout.fileViewMode = Tree` | **A** |
| V7 | All Files View | - | nullptr | Wire to `layout.fileViewMode = All` | **A** |
| V8 | Zoom In | Cmd+= | nullptr | No zoom infrastructure exists | B |
| V9 | Zoom Out | Cmd+- | nullptr | No zoom infrastructure exists | B |
| V10 | Reset Zoom | Cmd+0 | nullptr | No zoom infrastructure exists | B |

### Repository Menu

| # | Item | Shortcut | Current State | Action Needed | Tier |
|---|------|----------|---------------|---------------|------|
| R1 | Stage File | Cmd+Shift+S | nullptr | Wire to `git::stage_file()` on selected file | **A** |
| R2 | Unstage File | Cmd+Shift+U | nullptr | Wire to `git::unstage_file()` on selected file | **A** |
| R3 | Commit... | Cmd+Enter | nullptr | Wire to `editor.commitRequested = true` | **A** |
| R4 | Amend Last Commit | - | nullptr | Wire to `editor.isAmend = true; editor.commitRequested = true` | **A** |
| R5 | New Branch... | Cmd+Shift+B | **Works** | None | - |
| R6 | Checkout Branch... | Cmd+Shift+O | **Works** | None | - |
| R7 | Push | Cmd+Shift+P | nullptr | Wire to `git_push()` + refresh | **A** |
| R8 | Pull | Cmd+Shift+L | nullptr | Wire to `git_pull()` + refresh | **A** |
| R9 | Fetch | - | nullptr | Wire to `git_fetch()` + refresh | **A** |

### Help Menu

| # | Item | Shortcut | Current State | Action Needed | Tier |
|---|------|----------|---------------|---------------|------|
| H1 | Keyboard Shortcuts | Cmd+? | nullptr | Needs new panel/modal | B |
| H2 | Command Log | - | nullptr | Same as Toggle Command Log? Wire to `commandLogVisible` | **A** |
| H3 | About floatinghotel | - | nullptr | Needs about modal | B |

### Toolbar Buttons (Full-Width Mode)

| # | Button | Current State | Action Needed | Tier |
|---|--------|---------------|---------------|------|
| T1 | Refresh | **Works** | None | - |
| T2 | Stage All | Broken: only refreshes, never calls `git::stage_all()` | Fix: call `stage_all()` then refresh | **A** |
| T3 | Unstage All | Broken: only refreshes, never calls `git::unstage_all()` | Fix: call `unstage_all()` then refresh | **A** |
| T4 | Commit | **Works** | None | - |
| T5 | Push | Empty if-block | Wire to `git_push()` + refresh | **A** |
| T6 | Pull | Empty if-block | Wire to `git_pull()` + refresh | **A** |
| T7 | Fetch | Empty if-block | Wire to `git_fetch()` + refresh | **A** |
| T8 | Branch selector | **Works** | None | - |

### Toolbar Buttons (Sidebar Mode)

| # | Button | Current State | Action Needed | Tier |
|---|--------|---------------|---------------|------|
| S1 | Commit | **Works** | None | - |
| S2 | Push | Empty + TODO | Wire to `git_push()` + refresh | **A** |
| S3 | Pull | Empty + TODO | Wire to `git_pull()` + refresh | **A** |
| S4 | Stash | Empty + TODO | No `git stash` command exists yet | B |

## Tiers

- **Tier A** — Directly wirable. Backend exists, just connect action to UI. (19 items)
- **Tier B** — Needs new infrastructure (file dialog, clipboard, zoom, stash, search UI, about modal). (11 items)
- **Tier C** — Ambiguous / skip. (1 item: Close Tab)

## Implementation Approach (Tier A)

### 1. Menu item actions (`src/ui/menu_setup.h`)

Wire each nullptr action to a lambda that queries the relevant ECS component and mutates state. Pattern:

```cpp
MenuItem::item("Toggle Sidebar", "Cmd+B", [] {
    auto entities = afterhours::EntityQuery({.force_merge = true})
        .whereHasComponent<ecs::LayoutComponent>()
        .gen();
    if (!entities.empty()) {
        auto& layout = entities[0].get().get<ecs::LayoutComponent>();
        layout.sidebarVisible = !layout.sidebarVisible;
    }
}),
```

For git operations (Push/Pull/Fetch/Stage/Unstage), the action calls the git function then sets `refreshRequested = true`.

### 2. Toolbar button fixes (`src/ecs/toolbar_system.h`)

- **Stage All**: Add `git::stage_all(repo.repoPath)` before refresh
- **Unstage All**: Add `git::unstage_all(repo.repoPath)` before refresh
- **Push/Pull/Fetch**: Add `git_push/git_pull/git_fetch(repo.repoPath)` + refresh
- **Sidebar Push/Pull**: Same pattern

### 3. Tier B stubs

For unimplemented items, show a toast: `afterhours::toast::send_info(ctx, "Not yet implemented")`. This requires passing the UIContext into the menu action, which currently uses `std::function<void()>`. Options:

- **(a)** Change `MenuItem::action` signature to accept a context parameter
- **(b)** Use a global/singleton to post toasts without context
- **(c)** Set a flag on MenuComponent and have MenuBarSystem show the toast

Option (c) is simplest — add a `std::string pendingToast` field to `MenuComponent`, set it in the action lambda, and have `MenuBarSystem` drain it.

## E2E Test Plan

Each test follows the pattern: set up repo state -> click menu/toolbar -> wait -> assert state changed (via `expect_text` / `expect_no_text` / `screenshot` / custom `validate` commands).

### New custom e2e commands needed

To validate internal state beyond visible text, add app-specific commands via the `property_getter` callback already supported by E2ERunner:

```
validate sidebar_visible=true
validate diff_view_mode=Inline
validate command_log_visible=false
validate staged_count=3
validate unstaged_count=0
validate branch=main
```

These read from RepoComponent / LayoutComponent and return string values.

### Test scripts

#### `tests/e2e_scripts/flow_menu_view.e2e` — View menu items

```
make_test_repo
resize 1280 720
wait_frames 5

# V1: Toggle Sidebar off
validate sidebar_visible=true
click_text "View"
wait_frames 3
click_text "Toggle Sidebar"
wait_frames 5
validate sidebar_visible=false
screenshot menu_view_01_sidebar_hidden

# V1: Toggle Sidebar back on
click_text "View"
wait_frames 3
click_text "Toggle Sidebar"
wait_frames 5
validate sidebar_visible=true
screenshot menu_view_02_sidebar_visible

# V2: Toggle Command Log
validate command_log_visible=false
click_text "View"
wait_frames 3
click_text "Toggle Command Log"
wait_frames 5
validate command_log_visible=true
expect_text "Command Log"
screenshot menu_view_03_command_log

# V3/V4: Switch diff view modes
click_text "View"
wait_frames 3
click_text "Side-by-Side Diff"
wait_frames 5
validate diff_view_mode=SideBySide
screenshot menu_view_04_side_by_side

click_text "View"
wait_frames 3
click_text "Inline Diff"
wait_frames 5
validate diff_view_mode=Inline
screenshot menu_view_05_inline

# V5/V6/V7: File view modes
click_text "View"
wait_frames 3
click_text "Tree View"
wait_frames 5
validate file_view_mode=Tree

click_text "View"
wait_frames 3
click_text "All Files View"
wait_frames 5
validate file_view_mode=All

click_text "View"
wait_frames 3
click_text "Changed Files View"
wait_frames 5
validate file_view_mode=Flat
screenshot menu_view_06_flat_view
```

#### `tests/e2e_scripts/flow_menu_repository.e2e` — Repository menu git actions

```
make_test_repo
resize 1280 720
wait_frames 5

# R1: Stage File (select a file first)
# Click first unstaged file in sidebar
click_text "modified.txt"
wait_frames 3
click_text "Repository"
wait_frames 3
click_text "Stage File"
wait_frames 10
screenshot menu_repo_01_stage_file

# R2: Unstage File
click_text "Repository"
wait_frames 3
click_text "Unstage File"
wait_frames 10
screenshot menu_repo_02_unstage_file

# R7/R8/R9: Push/Pull/Fetch (these may fail without remote, but should not crash)
click_text "Repository"
wait_frames 3
click_text "Fetch"
wait_frames 10
screenshot menu_repo_03_fetch
```

#### `tests/e2e_scripts/flow_toolbar_actions.e2e` — Toolbar buttons

```
make_test_repo
resize 1280 720
wait_frames 5

# T2: Stage All
click_text "Stage All"
wait_frames 10
screenshot toolbar_01_stage_all
# Verify unstaged is now empty and staged is populated
validate unstaged_count=0

# T3: Unstage All
click_text "Unstage All"
wait_frames 10
screenshot toolbar_02_unstage_all
validate staged_count=0

# T5/T6/T7: Push/Pull/Fetch (no remote, but exercises the code path)
click_text "Fetch"
wait_frames 10
screenshot toolbar_03_fetch

# T1: Refresh
click_text "Refresh"
wait_frames 10
screenshot toolbar_04_refresh
```

#### `tests/e2e_scripts/flow_menu_help.e2e` — Help menu

```
make_test_repo
resize 1280 720
wait_frames 5

# H2: Command Log (same as Toggle Command Log)
click_text "Help"
wait_frames 3
click_text "Command Log"
wait_frames 5
validate command_log_visible=true
screenshot menu_help_01_command_log
```

## E2E Test Files (TDD — written before implementation)

Each item has its own test file. All tests use `validate` commands backed by a `property_getter` callback in `main.cpp` that reads ECS component state.

| File | Tests | Key Validations |
|------|-------|-----------------|
| `flow_v1_toggle_sidebar.e2e` | V1: Toggle Sidebar | `sidebar_visible=false` then `true` |
| `flow_v2_toggle_command_log.e2e` | V2: Toggle Command Log | `command_log_visible=true` then `false` |
| `flow_v3_inline_diff.e2e` | V3: Inline Diff | `diff_view_mode=Inline` |
| `flow_v4_side_by_side_diff.e2e` | V4: Side-by-Side Diff | `diff_view_mode=SideBySide` |
| `flow_v5_changed_files_view.e2e` | V5: Changed Files View | `file_view_mode=Flat` |
| `flow_v6_tree_view.e2e` | V6: Tree View | `file_view_mode=Tree` |
| `flow_v7_all_files_view.e2e` | V7: All Files View | `file_view_mode=All` |
| `flow_r1_stage_file.e2e` | R1: Stage File | `staged_count=2` after staging |
| `flow_r2_unstage_file.e2e` | R2: Unstage File | `staged_count=0` after unstaging |
| `flow_r3_commit_menu.e2e` | R3: Commit... | Screenshot of commit editor |
| `flow_r4_amend_commit.e2e` | R4: Amend Last Commit | `is_amend=true` |
| `flow_r7_push_menu.e2e` | R7: Push (menu) | App still responsive after no-remote push |
| `flow_r8_pull_menu.e2e` | R8: Pull (menu) | App still responsive after no-remote pull |
| `flow_r9_fetch_menu.e2e` | R9: Fetch (menu) | App still responsive after no-remote fetch |
| `flow_t2_stage_all.e2e` | T2: Stage All (toolbar) | `unstaged_count=0, untracked_count=0` |
| `flow_t3_unstage_all.e2e` | T3: Unstage All (toolbar) | `staged_count=0` |
| `flow_t5_push_toolbar.e2e` | T5: Push (toolbar) | App still responsive |
| `flow_t6_pull_toolbar.e2e` | T6: Pull (toolbar) | App still responsive |
| `flow_t7_fetch_toolbar.e2e` | T7: Fetch (toolbar) | App still responsive |
| `flow_s2_push_sidebar.e2e` | S2: Push (sidebar) | App still responsive |
| `flow_s3_pull_sidebar.e2e` | S3: Pull (sidebar) | App still responsive |
| `flow_h2_command_log_help.e2e` | H2: Command Log (Help) | `command_log_visible=true` |

**Total: 22 test files, 0 passing initially (TDD)**

### Validate properties available

| Property | Source | Values |
|----------|--------|--------|
| `sidebar_visible` | `LayoutComponent::sidebarVisible` | `true`/`false` |
| `command_log_visible` | `LayoutComponent::commandLogVisible` | `true`/`false` |
| `diff_view_mode` | `LayoutComponent::diffViewMode` | `Inline`/`SideBySide` |
| `file_view_mode` | `LayoutComponent::fileViewMode` | `Flat`/`Tree`/`All` |
| `sidebar_mode` | `LayoutComponent::sidebarMode` | `Changes`/`Refs` |
| `staged_count` | `RepoComponent::stagedFiles.size()` | integer |
| `unstaged_count` | `RepoComponent::unstagedFiles.size()` | integer |
| `untracked_count` | `RepoComponent::untrackedFiles.size()` | integer |
| `branch` | `RepoComponent::currentBranch` | string |
| `selected_file` | `RepoComponent::selectedFilePath` | string |
| `is_amend` | `CommitEditorComponent::isAmend` | `true`/`false` |
| `refresh_requested` | `RepoComponent::refreshRequested` | `true`/`false` |

## Files Changed

| File | Change |
|------|--------|
| `src/ui/menu_setup.h` | Wire 16 menu item actions |
| `src/ecs/toolbar_system.h` | Fix Stage All / Unstage All, implement Push/Pull/Fetch |
| `src/ecs/components.h` | Add `pendingToast` to MenuComponent (for tier B stubs) |
| `src/ecs/menu_bar_system.h` | Drain `pendingToast` and show toast |
| `src/main.cpp` | Add property_getter callback for validate commands (DONE) |
| `tests/e2e_scripts/flow_*.e2e` | 22 new test files (DONE) |

## Open Questions

1. **Close Tab** — Remove from menu or implement as "close current repo view"?
2. **Shortcut conflicts** — Cmd+Shift+L is listed for both "Toggle Command Log" and "Pull". Pick one.
3. **Push/Pull/Fetch without remote** — Show toast error or silently fail? The `git_push/pull/fetch` functions will return a `GitResult` with `.success() == false`.
4. **Stash** — Add `git stash` / `git stash pop` to `git_runner` or defer?
5. **Amend** — Should amend pre-fill the subject/body from the previous commit message?

## Progress Tracker

Run `./scripts/run_flow_tests.sh` and count PASS/FAIL to track progress.

- [x] V1: Toggle Sidebar
- [x] V2: Toggle Command Log
- [x] V3: Inline Diff
- [x] V4: Side-by-Side Diff
- [x] V5: Changed Files View
- [x] V6: Tree View
- [x] V7: All Files View
- [x] R1: Stage File
- [x] R2: Unstage File
- [x] R3: Commit...
- [x] R4: Amend Last Commit
- [x] R7: Push (menu)
- [x] R8: Pull (menu) — shortcut cleared (was conflicting with Command Log)
- [x] R9: Fetch (menu)
- [x] T2: Stage All (toolbar)
- [x] T3: Unstage All (toolbar)
- [x] T5: Push (toolbar)
- [x] T6: Pull (toolbar)
- [x] T7: Fetch (toolbar)
- [x] S2: Push (sidebar)
- [x] S3: Pull (sidebar)
- [x] H2: Command Log (Help)

### Tier B stubs (show "not yet implemented" toast)
- [x] F1: Open Repository...
- [x] F2: Close Tab
- [x] E1: Copy
- [x] E2: Select All
- [x] E3: Find...
- [x] V8: Zoom In
- [x] V9: Zoom Out
- [x] V10: Reset Zoom
- [x] H1: Keyboard Shortcuts
- [x] H3: About floatinghotel
- [x] S4: Stash (sidebar toolbar)
