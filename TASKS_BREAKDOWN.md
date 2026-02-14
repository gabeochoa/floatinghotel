# floatinghotel — Implementation Task Breakdown

Tasks are ordered by dependency chain: infrastructure first, then git backend, then UI systems, then features, then polish. Each task is scoped for 1-2 hours of senior dev work.

**Total: 35 implementation tasks** (20 P0, 12 P1, 3 P2)

---

## Phase 1: Infrastructure & Framework

### TASK-01: Set up build system with Makefile + vendored dependencies — T009
- **Scope:** `makefile`, `vendor/` (afterhours submodule)
- **Dependencies:** None
- **Priority:** P0
- **Complexity:** S
- **Acceptance Criteria:**
  - [ ] Makefile compiles C++23 with clang++
  - [ ] afterhours is vendored under `vendor/afterhours/`
  - [ ] Sokol headers are vendored or fetched
  - [ ] `make` produces a binary in `output/`
  - [ ] Builds clean on macOS with no warnings

### TASK-02: Create application entry point with Sokol window + ECS — T049
- **Scope:** `src/main.cpp`, `src/preload.cpp`
- **Dependencies:** TASK-01 (T009)
- **Priority:** P0
- **Complexity:** M
- **Acceptance Criteria:**
  - [ ] `src/main.cpp` initializes Sokol with Metal backend
  - [ ] Opens a 1200x800 window titled "floatinghotel"
  - [ ] Registers afterhours ECS with pre-layout, post-layout, and render systems
  - [ ] `src/preload.cpp` loads monospace + proportional fonts
  - [ ] Window renders with dark background (#1E1E1E) and exits cleanly

### TASK-03: Define ECS components — T050
- **Scope:** `src/ecs/components.h`
- **Dependencies:** TASK-02 (T049)
- **Priority:** P0
- **Complexity:** S
- **Acceptance Criteria:**
  - [ ] `RepoComponent` defined with FileStatus, CommitEntry, FileDiff, DiffHunk structs
  - [ ] `LayoutComponent` defined with panel sizes, view modes, computed rects
  - [ ] `CommitEditorComponent`, `MenuComponent`, `CommandLogComponent`, `SettingsComponent` defined
  - [ ] All components inherit from `afterhours::BaseComponent`
  - [ ] Compiles without errors when included from main.cpp

### TASK-04: Define dark theme constants — T051
- **Scope:** `src/ui/theme.h`
- **Dependencies:** TASK-02 (T049)
- **Priority:** P0
- **Complexity:** S
- **Acceptance Criteria:**
  - [ ] `theme::` namespace with all color constants from REQUIREMENTS.md (window chrome, text, status badges, diff colors, interactive, status bar)
  - [ ] Layout constants defined (sidebar default/min width, row heights, padding values)
  - [ ] Font size constants (monospace 14px for code, proportional for UI chrome)
  - [ ] Compiles and colors render correctly when applied to background

### TASK-05: Implement subprocess runner — T010
- **Scope:** `src/util/process.h`, `src/util/process.cpp`
- **Dependencies:** TASK-01 (T009)
- **Priority:** P0
- **Complexity:** S
- **Acceptance Criteria:**
  - [ ] `ProcessResult` struct with stdout, stderr, exitCode fields
  - [ ] `run_process()` function that executes a command with arguments, captures stdout/stderr
  - [ ] Works with `posix_spawn` or `popen` on macOS
  - [ ] Handles non-zero exit codes without crashing
  - [ ] Tested with a simple command like `echo hello`

### TASK-06: Implement git CLI runner — T011
- **Scope:** `src/git/git_runner.h`, `src/git/git_runner.cpp`
- **Dependencies:** TASK-05 (T010)
- **Priority:** P0
- **Complexity:** S
- **Acceptance Criteria:**
  - [ ] `GitResult` struct wrapping ProcessResult with `success()` helper
  - [ ] `git_run(repoPath, args)` runs `git -C <repoPath> <args...>` and returns GitResult
  - [ ] All commands are logged (command string, output, error, success/fail)
  - [ ] Handles missing git binary gracefully (error message, not crash)

### TASK-07: Implement git status parser (porcelain v2) — T012
- **Scope:** `src/git/git_parser.h`, `src/git/git_parser.cpp` (status section)
- **Dependencies:** TASK-06 (T011)
- **Priority:** P0
- **Complexity:** M
- **Acceptance Criteria:**
  - [ ] Runs `git status --porcelain=v2 --branch`
  - [ ] Parses branch name, ahead/behind counts from `# branch.` headers
  - [ ] Parses ordinary changed entries (`1 ...`), renamed entries (`2 ...`), untracked (`? ...`)
  - [ ] Populates `RepoComponent::stagedFiles`, `unstagedFiles`, `untrackedFiles`
  - [ ] Correctly identifies index vs worktree status characters

### TASK-08: Implement git log parser — T013
- **Scope:** `src/git/git_parser.h`, `src/git/git_parser.cpp` (log section)
- **Dependencies:** TASK-06 (T011)
- **Priority:** P0
- **Complexity:** S
- **Acceptance Criteria:**
  - [ ] Runs `git log --format="%H%x00%h%x00%s%x00%an%x00%aI%x00%D" -100`
  - [ ] Parses NUL-separated fields into `CommitEntry` structs
  - [ ] Handles empty decorations field gracefully
  - [ ] Supports pagination: accepts `--skip=N` parameter for lazy loading
  - [ ] Returns empty vector for repos with no commits

### TASK-09: Implement git diff parser (unified diff format) — T014
- **Scope:** `src/git/git_parser.h`, `src/git/git_parser.cpp` (diff section)
- **Dependencies:** TASK-06 (T011)
- **Priority:** P0
- **Complexity:** M
- **Acceptance Criteria:**
  - [ ] Parses `--- a/file` / `+++ b/file` headers for file paths
  - [ ] Parses `@@ -start,count +start,count @@` hunk headers
  - [ ] Categorizes lines as addition (+), deletion (-), or context (space)
  - [ ] Counts insertions/deletions per file and per hunk
  - [ ] Populates `FileDiff` and `DiffHunk` structs correctly
  - [ ] Handles rename diffs and new/deleted file diffs

### TASK-10: Implement git error humanizer — T015
- **Scope:** `src/git/error_humanizer.h`, `src/git/error_humanizer.cpp`
- **Dependencies:** TASK-06 (T011)
- **Priority:** P0
- **Complexity:** S
- **Acceptance Criteria:**
  - [ ] Lookup table mapping common git stderr patterns to friendly messages
  - [ ] Covers: authentication failure, merge conflict, non-fast-forward rejection, detached HEAD
  - [ ] Falls back to raw stderr for unrecognized errors
  - [ ] `humanize_error(stderr_string) -> string` function signature

### TASK-11: Build draggable divider widget — T016
- **Scope:** `src/ui/split_panel.h` (divider section)
- **Dependencies:** TASK-02 (T049)
- **Priority:** P0
- **Complexity:** S
- **Acceptance Criteria:**
  - [ ] Renders a thin vertical or horizontal divider bar (4px hit zone, 1px visible)
  - [ ] Uses `HasDragListener` to track mouse drag
  - [ ] Reports position delta to parent via callback or component data
  - [ ] Changes cursor to resize cursor on hover
  - [ ] Respects min/max constraints on both sides

### TASK-12: Build split pane widget — T017
- **Scope:** `src/ui/split_panel.h`, `src/ui/split_panel.cpp`
- **Dependencies:** TASK-11 (T016)
- **Priority:** P0
- **Complexity:** M
- **Acceptance Criteria:**
  - [ ] `split_panel()` function creates a horizontal or vertical split with two child regions
  - [ ] Uses draggable divider to resize panes
  - [ ] Supports min/max width/height constraints per pane
  - [ ] Built on top of afterhours `div()` and `HasDragListener`
  - [ ] Tested with two colored panels that resize correctly

### TASK-13: Build dropdown menu widget — T018
- **Scope:** `src/ui/menu_setup.h`, `src/ui/menu_setup.cpp`
- **Dependencies:** TASK-02 (T049)
- **Priority:** P0
- **Complexity:** M
- **Acceptance Criteria:**
  - [ ] Menu bar item click opens a dropdown list of menu items
  - [ ] Menu items support: label, keyboard shortcut text, click callback, separator, disabled state
  - [ ] Only one dropdown open at a time; clicking elsewhere closes it
  - [ ] Supports hover-to-switch between adjacent menu bar items while open
  - [ ] Built on top of afterhours `div()`, `button()`, and `HasClickListener`

---

## Phase 2: Git Read Operations — UI Systems

### TASK-14: Implement status bar system — T019
- **Scope:** `src/ecs/status_bar_system.h`
- **Dependencies:** TASK-03 (T050), TASK-04 (T051), TASK-07 (T012)
- **Priority:** P0
- **Complexity:** M
- **Acceptance Criteria:**
  - [ ] Renders a full-width bar at window bottom with blue background (#007ACC)
  - [ ] Shows: branch name, dirty indicator (colored dot), ahead/behind counts, last commit info
  - [ ] Hides ahead/behind when counts are 0
  - [ ] Reads data from `RepoComponent` each frame
  - [ ] Includes a command log toggle button on the right

### TASK-15: Implement sidebar changed files list — T020
- **Scope:** `src/ecs/sidebar_system.h` (file list section)
- **Dependencies:** TASK-03 (T050), TASK-04 (T051), TASK-07 (T012), TASK-12 (T017)
- **Priority:** P0
- **Complexity:** M
- **Acceptance Criteria:**
  - [ ] Renders scrollable list of changed files in top portion of sidebar
  - [ ] Each row shows: status badge (M/A/D/R/U with correct color), filename, change stats (+N/-N)
  - [ ] Row height 24px with hover highlight and selection highlight
  - [ ] Clicking a file sets `RepoComponent::selectedFilePath` (triggers diff view)
  - [ ] Shows "No changes" text when working tree is clean
  - [ ] Current branch name shown above file list

### TASK-16: Implement sidebar commit log — T021
- **Scope:** `src/ecs/sidebar_system.h` (commit log section)
- **Dependencies:** TASK-03 (T050), TASK-04 (T051), TASK-08 (T013), TASK-12 (T017)
- **Priority:** P0
- **Complexity:** M
- **Acceptance Criteria:**
  - [ ] Renders scrollable list of commits in bottom portion of sidebar
  - [ ] Each row (40px) shows: short hash, truncated subject, relative time on first line; author and decorations on second line
  - [ ] Branch/tag decorations rendered as colored badges (origin/* = blue, HEAD = green, tags = gray)
  - [ ] Clicking a commit sets `RepoComponent::selectedCommitHash`
  - [ ] Lazy loads next 100 commits when scrolled near bottom

### TASK-17: Implement inline diff viewer — T022
- **Scope:** `src/ecs/main_content_system.h`, `src/ui/diff_renderer.h`
- **Dependencies:** TASK-03 (T050), TASK-04 (T051), TASK-09 (T014), TASK-12 (T017)
- **Priority:** P0
- **Complexity:** L
- **Acceptance Criteria:**
  - [ ] Renders unified diff in the main content area when a file is selected
  - [ ] Deleted lines: red background with red text; Added lines: green background with green text
  - [ ] Hunk headers displayed as separator bars with blue text
  - [ ] Line number gutter shows old/new line numbers
  - [ ] Shows total change statistics header: "N files changed, +X -Y"
  - [ ] Shows empty state ("Select a file to view diff") when no file selected

### TASK-18: Implement menu bar system — T023
- **Scope:** `src/ecs/menu_bar_system.h`
- **Dependencies:** TASK-03 (T050), TASK-13 (T018)
- **Priority:** P0
- **Complexity:** M
- **Acceptance Criteria:**
  - [ ] Renders menu bar at top of window with items: File, Edit, View, Git, Help
  - [ ] Uses dropdown menu widget for each menu
  - [ ] File menu: Open Repo, Close Tab, Quit
  - [ ] View menu: Toggle Sidebar, Toggle Inline/Side-by-Side Diff, Toggle Command Log
  - [ ] Git menu: Refresh, Stage All, Unstage All, Commit, New Branch, Checkout Branch
  - [ ] Menu items show keyboard shortcut hints (right-aligned)

### TASK-19: Implement main layout system — T024
- **Scope:** `src/main.cpp` (layout orchestration)
- **Dependencies:** TASK-12 (T017), TASK-14 (T019), TASK-15 (T020), TASK-16 (T021), TASK-17 (T022), TASK-18 (T023)
- **Priority:** P0
- **Complexity:** M
- **Acceptance Criteria:**
  - [ ] Window divided into: menu bar (top), toolbar area (below menu), sidebar+main split (middle), status bar (bottom)
  - [ ] Sidebar and main content use split pane with draggable divider
  - [ ] Sidebar internally split into files (top 60%) and commit log (bottom 40%) with draggable divider
  - [ ] All panels resize correctly when window is resized
  - [ ] Default sidebar width is 280px, minimum 180px, maximum 50% of window

---

## Phase 3: Git Write Operations

### TASK-20: Implement file-level staging and unstaging — T025
- **Scope:** `src/git/git_commands.h`, `src/ecs/sidebar_system.h` (stage buttons)
- **Dependencies:** TASK-06 (T011), TASK-15 (T020)
- **Priority:** P0
- **Complexity:** M
- **Acceptance Criteria:**
  - [ ] `git add <file>` stages an individual file
  - [ ] `git restore --staged <file>` unstages an individual file
  - [ ] Stage/unstage buttons visible on each file row (or via keyboard shortcut)
  - [ ] Sidebar refreshes after stage/unstage to show updated status
  - [ ] "Stage All" and "Unstage All" actions available

### TASK-21: Implement hunk-level staging — T026
- **Scope:** `src/git/git_commands.h`, `src/ui/diff_renderer.h` (hunk buttons)
- **Dependencies:** TASK-09 (T014), TASK-17 (T022), TASK-20 (T025)
- **Priority:** P0
- **Complexity:** L
- **Acceptance Criteria:**
  - [ ] Each hunk in the diff viewer has [Stage Hunk] and [Discard Hunk] buttons in the hunk header
  - [ ] Staging writes the hunk to a temp patch file and runs `git apply --cached <patch>`
  - [ ] Unstaging runs `git apply --cached --reverse <patch>`
  - [ ] Staged hunks show a green left border indicator
  - [ ] Diff view refreshes after staging/unstaging a hunk

### TASK-22: Implement commit message editor — T027
- **Scope:** `src/ecs/sidebar_system.h` (commit editor section)
- **Dependencies:** TASK-03 (T050), TASK-04 (T051)
- **Priority:** P0
- **Complexity:** M
- **Acceptance Criteria:**
  - [ ] Subject line `text_input()` with character counter (right-aligned "N/50")
  - [ ] Counter turns yellow at 50 chars, red at 72
  - [ ] Body `text_area()` with vertical guide line at column 72
  - [ ] Editor appears at bottom of sidebar, above status bar
  - [ ] Editor expands/collapses when activated/deactivated

### TASK-23: Implement commit workflow — T028
- **Scope:** `src/git/git_commands.h`, `src/ecs/sidebar_system.h` (commit button)
- **Dependencies:** TASK-06 (T011), TASK-22 (T027), TASK-24 (T029)
- **Priority:** P0
- **Complexity:** M
- **Acceptance Criteria:**
  - [ ] "Commit" button runs `git commit -m "subject\n\nbody"` with the editor content
  - [ ] Dropdown options: Amend (`--amend`), Fixup (`--fixup=HEAD`), Commit & Push
  - [ ] Clears the commit editor on successful commit
  - [ ] Refreshes status and log after commit
  - [ ] Shows error in status bar on commit failure (e.g. nothing staged)

### TASK-24: Implement confirmation dialogs — T029
- **Scope:** `src/ui/ui_context.h` (dialog helpers)
- **Dependencies:** TASK-02 (T049)
- **Priority:** P0
- **Complexity:** S
- **Acceptance Criteria:**
  - [ ] `confirm_dialog(title, message, confirm_label, cancel_label)` using afterhours modal plugin
  - [ ] Modal backdrop dims the rest of the UI
  - [ ] Focus trapped to dialog buttons
  - [ ] Used for: hunk discard, branch delete, force push warnings
  - [ ] Supports optional checkbox ("Remember this choice")

### TASK-25: Implement unstaged changes commit dialog — T030
- **Scope:** `src/ecs/sidebar_system.h` (dialog trigger)
- **Dependencies:** TASK-23 (T028), TASK-24 (T029)
- **Priority:** P0
- **Complexity:** S
- **Acceptance Criteria:**
  - [ ] When committing with both staged and unstaged changes, show modal dialog
  - [ ] Dialog text: "You have unstaged changes. Commit only staged changes?"
  - [ ] Buttons: [Stage All & Commit] [Commit Staged Only] [Cancel]
  - [ ] "Remember this choice" checkbox persists preference in SettingsComponent
  - [ ] Skips dialog if user previously chose to remember

### TASK-26: Implement branch operations — T031
- **Scope:** `src/git/git_commands.h`, `src/ecs/sidebar_system.h` (refs view)
- **Dependencies:** TASK-06 (T011), TASK-15 (T020), TASK-24 (T029)
- **Priority:** P0
- **Complexity:** M
- **Acceptance Criteria:**
  - [ ] Sidebar toggles between "Changes" view and "Refs" view
  - [ ] Refs view shows local branches and expandable remote branches
  - [ ] Create branch: modal dialog with name input and "from" selector, runs `git switch -c <name>`
  - [ ] Delete branch: confirmation dialog, runs `git branch -d <name>` (with extra warning for `-D`)
  - [ ] Checkout: click branch name runs `git switch <name>`, refreshes all state

---

## Phase 4: Essential UX

### TASK-27: Build tree view widget — T032
- **Scope:** `src/ui/tree_view.h`, `src/ui/tree_view.cpp`
- **Dependencies:** TASK-02 (T049)
- **Priority:** P1
- **Complexity:** S
- **Acceptance Criteria:**
  - [ ] `tree_node()` function renders a collapsible node with indent level
  - [ ] Click to expand/collapse children
  - [ ] Arrow icon rotates on expand/collapse
  - [ ] Supports arbitrary nesting depth with consistent indentation
  - [ ] Built on top of afterhours `div()` and `button()`

### TASK-28: Implement file tree view in sidebar — T033
- **Scope:** `src/ecs/sidebar_system.h` (tree view mode)
- **Dependencies:** TASK-15 (T020), TASK-27 (T032)
- **Priority:** P1
- **Complexity:** M
- **Acceptance Criteria:**
  - [ ] Three-state view switcher at top of file section: "Changed" (flat), "Tree" (grouped), "All" (full tree)
  - [ ] "Tree" mode groups changed files by directory with collapsible tree nodes
  - [ ] "All" mode shows full repository file tree with change indicators on modified files
  - [ ] Tree nodes show directory names; leaf nodes show file names with status badges
  - [ ] Selection and click behavior same as flat list

### TASK-29: Implement side-by-side diff view — T034
- **Scope:** `src/ui/diff_renderer.h` (side-by-side mode)
- **Dependencies:** TASK-12 (T017), TASK-17 (T022)
- **Priority:** P1
- **Complexity:** M
- **Acceptance Criteria:**
  - [ ] Toggle between inline and side-by-side via Cmd+Shift+D or View menu
  - [ ] Side-by-side uses split pane with 50/50 split
  - [ ] Left panel shows old file (deletions highlighted), right shows new file (additions highlighted)
  - [ ] Synchronized scrolling between left and right panels
  - [ ] Line numbers displayed in both panels

### TASK-30: Implement commit detail view — T035
- **Scope:** `src/ecs/main_content_system.h` (commit detail mode)
- **Dependencies:** TASK-08 (T013), TASK-09 (T014), TASK-16 (T021), TASK-17 (T022)
- **Priority:** P1
- **Complexity:** M
- **Acceptance Criteria:**
  - [ ] Clicking a commit in the log shows full commit detail in main content area
  - [ ] Shows: full hash, author, date, full commit message
  - [ ] Shows diff for that commit (via `git show <hash> --format=""`)
  - [ ] Reuses diff renderer for displaying the commit diff
  - [ ] Back button or clicking a file returns to file diff view

### TASK-31: Build context menu widget — T036
- **Scope:** `src/ui/context_menu.h`, `src/ui/context_menu.cpp`
- **Dependencies:** TASK-02 (T049)
- **Priority:** P1
- **Complexity:** M
- **Acceptance Criteria:**
  - [ ] Right-click triggers a popup menu at cursor position
  - [ ] Menu items support: label, click callback, separator, disabled state, keyboard shortcut text
  - [ ] Clicking outside the menu or pressing Escape closes it
  - [ ] Menu positioned to stay within window bounds (flips if near edge)
  - [ ] Only one context menu open at a time

### TASK-32: Implement context menus on files, commits, branches — T037
- **Scope:** `src/ecs/sidebar_system.h` (context menu integration)
- **Dependencies:** TASK-15 (T020), TASK-16 (T021), TASK-26 (T031), TASK-31 (T036)
- **Priority:** P1
- **Complexity:** M
- **Acceptance Criteria:**
  - [ ] File context menu: Stage, Unstage, Discard Changes, Open in External Editor, Add to .gitignore
  - [ ] Commit context menu: Copy Hash, Cherry-pick, Revert, Create Branch Here
  - [ ] Branch context menu: Checkout, Delete, Rename
  - [ ] Context menu items trigger the appropriate git command or UI action
  - [ ] Disabled items shown grayed out (e.g. "Unstage" on an already unstaged file)

### TASK-33: Implement command log panel — T038
- **Scope:** `src/ecs/main_content_system.h` (command log overlay)
- **Dependencies:** TASK-03 (T050), TASK-04 (T051), TASK-06 (T011)
- **Priority:** P1
- **Complexity:** M
- **Acceptance Criteria:**
  - [ ] Toggle panel showing all git commands run with timestamps
  - [ ] Each entry shows: command string, stdout (collapsed by default), stderr if any, success/fail indicator
  - [ ] Scrollable, most recent entries at top
  - [ ] Toggle via status bar button or View menu
  - [ ] Panel appears at the bottom of the main content area

### TASK-34: Implement toolbar system — T039
- **Scope:** `src/ecs/toolbar_system.h`
- **Dependencies:** TASK-03 (T050), TASK-04 (T051)
- **Priority:** P1
- **Complexity:** S
- **Acceptance Criteria:**
  - [ ] Renders a horizontal toolbar below the menu bar
  - [ ] Buttons: Commit, Push, Pull, Fetch, Branch dropdown, Refresh
  - [ ] Buttons trigger the same actions as menu items
  - [ ] Buttons show disabled state when action is unavailable
  - [ ] Consistent button sizing and spacing

### TASK-35: Implement keyboard navigation system — T040
- **Scope:** `src/ecs/input_system.h`, `src/input/action_map.h`
- **Dependencies:** TASK-14 (T019), TASK-15 (T020), TASK-16 (T021), TASK-17 (T022)
- **Priority:** P1
- **Complexity:** L
- **Acceptance Criteria:**
  - [ ] Tab cycles focus between panels (sidebar files, sidebar log, main content)
  - [ ] Arrow keys navigate within the focused panel (up/down in lists)
  - [ ] `]` / `[` navigate between files in diff; `j` / `k` navigate between hunks
  - [ ] Global shortcuts: Cmd+R (refresh), Cmd+Shift+B (new branch), Cmd+Shift+D (toggle diff mode)
  - [ ] `s` to stage, `u` to unstage, `d` to discard current hunk (when diff viewer focused)
  - [ ] Action map is data-driven (easy to add/change shortcuts)

### TASK-36: Implement JSON settings persistence — T041
- **Scope:** `src/settings.h`, `src/settings.cpp`
- **Dependencies:** TASK-03 (T050)
- **Priority:** P1
- **Complexity:** M
- **Acceptance Criteria:**
  - [ ] Settings saved to `~/.config/floatinghotel/settings.json`
  - [ ] Persists: window size/position, sidebar width, commit log ratio, open repo paths, last active repo
  - [ ] Loads settings on startup, saves on changes (debounced)
  - [ ] Handles missing/corrupt settings file gracefully (use defaults)
  - [ ] Creates config directory if it doesn't exist

### TASK-37: Implement CLI launcher (fh command) — T042
- **Scope:** `src/main.cpp` (argument parsing), install script
- **Dependencies:** TASK-02 (T049)
- **Priority:** P1
- **Complexity:** S
- **Acceptance Criteria:**
  - [ ] `fh .` opens the current directory as a repo
  - [ ] `fh /path/to/repo` opens the specified repo
  - [ ] `fh` with no args opens last-used repo or shows "Open Repo" dialog
  - [ ] Validates that the path is a git repository before opening

### TASK-38: Implement git refresh system — T043
- **Scope:** `src/ecs/git_refresh_system.h`
- **Dependencies:** TASK-07 (T012), TASK-08 (T013)
- **Priority:** P1
- **Complexity:** M
- **Acceptance Criteria:**
  - [ ] Runs `git status` and `git log` on app launch to populate RepoComponent
  - [ ] Re-runs on window focus / app activate
  - [ ] Manual refresh via Cmd+R or toolbar Refresh button
  - [ ] Refreshes after any git write operation (commit, stage, branch switch)
  - [ ] Shows brief "Refreshing..." indicator in status bar during refresh

### TASK-39: Implement hunk discard with confirmation — T044
- **Scope:** `src/git/git_commands.h`, `src/ui/diff_renderer.h`
- **Dependencies:** TASK-21 (T026), TASK-24 (T029)
- **Priority:** P1
- **Complexity:** S
- **Acceptance Criteria:**
  - [ ] [Discard] button on each hunk header bar
  - [ ] Shows confirmation dialog: "Discard this hunk? This cannot be undone."
  - [ ] Runs `git apply --reverse <patch>` to discard the hunk from working tree
  - [ ] Refreshes diff view after discard
  - [ ] Full file discard via `git checkout -- <file>` with confirmation

### TASK-40: Implement commit message templates — T045
- **Scope:** `src/ecs/sidebar_system.h` (template picker)
- **Dependencies:** TASK-22 (T027)
- **Priority:** P1
- **Complexity:** S
- **Acceptance Criteria:**
  - [ ] Templates loaded from `~/.config/floatinghotel/templates/*.txt`
  - [ ] Template picker dropdown in commit editor: "Template: [None]"
  - [ ] Selecting a template populates subject and body fields
  - [ ] Conventional commit helper: dropdown with type prefixes (feat:, fix:, chore:, docs:, etc.)
  - [ ] Prefix prepended to subject line when selected

---

## Phase 5: Advanced Git & Polish

### TASK-41: Implement push/pull/fetch with background execution — T046
- **Scope:** `src/git/git_commands.h` (async section), `src/ecs/status_bar_system.h`
- **Dependencies:** TASK-06 (T011), TASK-14 (T019)
- **Priority:** P1
- **Complexity:** L
- **Acceptance Criteria:**
  - [ ] `git_run_async()` runs git commands in a background thread via `std::future`
  - [ ] Push, pull, fetch use async runner; UI stays responsive
  - [ ] Progress indicator shown in status bar during operation
  - [ ] Cancel button available for long-running operations
  - [ ] Force-push shows extra confirmation dialog with warning text

### TASK-42: Implement multi-repo tabs — T047
- **Scope:** `src/main.cpp` (tab management), `src/ecs/components.h` (tab state)
- **Dependencies:** TASK-19 (T024), TASK-36 (T041)
- **Priority:** P2
- **Complexity:** L
- **Acceptance Criteria:**
  - [ ] Tab bar below toolbar shows open repo tabs
  - [ ] Each tab is an independent repo view (own RepoComponent, sidebar, main content)
  - [ ] Tabs can be opened (File > Open Repo), closed, and pinned
  - [ ] Open tabs restored on app launch from settings
  - [ ] Active tab highlighted; inactive tabs show repo name only

### TASK-43: Implement full state persistence — T048
- **Scope:** `src/settings.h` (extended state)
- **Dependencies:** TASK-36 (T041), TASK-42 (T047)
- **Priority:** P2
- **Complexity:** M
- **Acceptance Criteria:**
  - [ ] Persists: panel divider positions, scroll positions, active tab, selected items, sidebar mode, diff view mode
  - [ ] All state restored on app relaunch to exactly the previous state
  - [ ] Per-repo state tracked separately (each tab remembers its own scroll/selection)
  - [ ] Migration logic for settings schema changes

---

## Task Summary by Priority

| Priority | Count | Description |
|----------|-------|-------------|
| P0 | 20 | MVP — must have for first usable version |
| P1 | 12 | Core workflow — important for daily use |
| P2 | 3 | Nice to have — polish and advanced features |

## Task Summary by Complexity

| Complexity | Count | Description |
|------------|-------|-------------|
| S (1 file) | 12 | Small, focused tasks |
| M (2-3 files) | 19 | Medium, moderate scope |
| L (4+ files) | 4 | Large, cross-cutting |

## Critical Path (P0 tasks in dependency order)

```
T009 (build system)
 ├── T049 (entry point)
 │    ├── T050 (components) ──┐
 │    ├── T051 (theme) ──────┤
 │    ├── T016 (divider) ────> T017 (split pane)
 │    ├── T018 (dropdown) ───> T023 (menu bar)
 │    └── T029 (dialogs)
 └── T010 (process) ──> T011 (git runner)
      ├── T012 (status parser)
      ├── T013 (log parser)
      ├── T014 (diff parser)
      └── T015 (error humanizer)

Mid-tier convergence:
  T050+T051+T012+T017 ──> T019 (status bar), T020 (sidebar files), T022 (diff viewer)
  T050+T051+T013+T017 ──> T021 (sidebar log)
  T050+T018 ──> T023 (menu bar)
  All above ──> T024 (main layout)

Write operations:
  T011+T020 ──> T025 (file staging) ──> T026 (hunk staging)
  T050+T051 ──> T027 (commit editor) ──> T028 (commit workflow) ──> T030 (unstaged dialog)
  T011+T020+T029 ──> T031 (branch ops)
```

## T-Number Quick Reference

| TASK | T-Number | Title | Priority |
|------|----------|-------|----------|
| 01 | T009 | Set up build system | P0 |
| 02 | T049 | Application entry point | P0 |
| 03 | T050 | ECS components | P0 |
| 04 | T051 | Dark theme constants | P0 |
| 05 | T010 | Subprocess runner | P0 |
| 06 | T011 | Git CLI runner | P0 |
| 07 | T012 | Git status parser | P0 |
| 08 | T013 | Git log parser | P0 |
| 09 | T014 | Git diff parser | P0 |
| 10 | T015 | Error humanizer | P0 |
| 11 | T016 | Draggable divider | P0 |
| 12 | T017 | Split pane widget | P0 |
| 13 | T018 | Dropdown menu widget | P0 |
| 14 | T019 | Status bar system | P0 |
| 15 | T020 | Sidebar changed files | P0 |
| 16 | T021 | Sidebar commit log | P0 |
| 17 | T022 | Inline diff viewer | P0 |
| 18 | T023 | Menu bar system | P0 |
| 19 | T024 | Main layout system | P0 |
| 20 | T025 | File-level staging | P0 |
| 21 | T026 | Hunk-level staging | P0 |
| 22 | T027 | Commit message editor | P0 |
| 23 | T028 | Commit workflow | P0 |
| 24 | T029 | Confirmation dialogs | P0 |
| 25 | T030 | Unstaged changes dialog | P0 |
| 26 | T031 | Branch operations | P0 |
| 27 | T032 | Tree view widget | P1 |
| 28 | T033 | File tree view | P1 |
| 29 | T034 | Side-by-side diff | P1 |
| 30 | T035 | Commit detail view | P1 |
| 31 | T036 | Context menu widget | P1 |
| 32 | T037 | Context menus integration | P1 |
| 33 | T038 | Command log panel | P1 |
| 34 | T039 | Toolbar system | P1 |
| 35 | T040 | Keyboard navigation | P1 |
| 36 | T041 | JSON settings | P1 |
| 37 | T042 | CLI launcher (fh) | P1 |
| 38 | T043 | Git refresh system | P1 |
| 39 | T044 | Hunk discard | P1 |
| 40 | T045 | Commit templates | P1 |
| 41 | T046 | Push/pull/fetch async | P1 |
| 42 | T047 | Multi-repo tabs | P2 |
| 43 | T048 | Full state persistence | P2 |
