# floatinghotel — Requirements Document

A personal version control GUI application for managing git repositories, built with C++23, the afterhours ECS framework, and Sokol for cross-platform rendering.

## Project Overview

**Name:** floatinghotel

**Vision:** A clean, minimal git GUI inspired by VS Code's git sidebar functionality combined with the design minimalism of Apple Notes and Bear Notes. Personal tool built for daily use.

**Tech Stack:**
- **Language:** C++23 (clang++)
- **Framework:** afterhours (custom ECS with UI plugins, vendored)
- **Rendering:** Sokol (Metal on macOS, WebGL2 for web)
- **UI System:** afterhours immediate-mode UI — divs, buttons, sliders, dropdowns, checkboxes, flex layout, bevels, themes, animations, toasts, modals, text input, tree views, scroll containers
- **Build System:** Make
- **Git Backend:** Shell out to `git` CLI (not libgit2)
- **Reference Projects:** wordproc (~/p/wordproc), wm_afterhours (~/p/wm_afterhours)
- **Afterhours feature docs:** ~/p/wm_afterhours/docs/ (37+ feature/widget specs)
- **wordproc docs:** ~/p/wordproc/docs/ (design rules, style guide, performance notes)

## Target Platforms

All platforms supported by afterhours (macOS, Linux, Windows, Web). macOS is the primary development target. Cross-platform support comes from the framework.

## Visual Design

- **Theme:** Dark mode first, customization later
- **Inspiration:** Apple Notes / Bear Notes minimalism + VS Code git sidebar functionality
- **Style:** Clean, minimal — NOT Win95 themed (diverges from wordproc)
- **Fonts:** Dual font system — monospace for code/diffs, proportional for UI chrome. Both configurable.
- **Animations:** Subtle animations for MVP (button feedback, toast appearance), richer animations later
- **Corners/Shapes:** TBD — concept art exploration recommended before locking in

### Concept Art Prompt (for image generation)
```
A 4x4 grid of concept art for a desktop version control GUI application
called "floating hotel". Each panel shows a different visual style: retro
Win95, modern flat, terminal hacker aesthetic, brutalist, glassmorphism,
pixel art, synthwave neon, paper/skeuomorphic, monochrome ink, warm
wood/leather, sci-fi holographic, art deco. Each panel shows a typical
git workflow screen with a commit graph, file diff view, and branch list
sidebar. Desktop application UI mockup, detailed, UI/UX design --ar 16:9
```

---

## Core Features — Prioritized

### P0 — MVP (Build First)

These are the minimum features to start using daily.

#### Status View
- Show changed files (modified, added, deleted, renamed, untracked) in the sidebar
- File status indicated by letter badge + color (A=green, M=yellow, D=red, R=blue, U=gray)
- Sidebar file view switchable between:
  - Flat list of changed files only
  - Tree view of changed files grouped by directory
  - Full repository file tree with change indicators
- Uncommitted changes shown in sidebar only (not mixed into commit log)

#### Diff Viewer
- Toggleable between side-by-side and inline/unified views
- Diff coloring (red/green) for MVP; syntax highlighting deferred
- Default diff options for MVP; ignore-whitespace, word diff, context lines, and algorithm selection deferred
- Keyboard navigation: jump between files AND between hunks within a file
- Change statistics: insertions/deletions count per file and total

#### Interactive Staging (git add -p style)
- Hunk-level staging: review each hunk and accept/skip/quit with keyboard (y/n/s/q)
- File-level staging also available
- Line-level staging deferred
- Hunk-level discard (destructive, with confirmation)

#### Commit Workflow
- Full commit message editor with subject/body separation and character count warnings (50/72 rule)
- Commit message templates and conventional commit helpers
- Commit button with dropdown arrow for additional actions (amend, fixup, etc.)
- When committing with unstaged changes: show dialog "Commit only staged changes? Yes / No / [x] Remember for future"

#### Sidebar + Main Layout
- Left sidebar + right main content area
- Sidebar top half: current branch name + changed files list
- Sidebar bottom half: commit log (flat list for MVP; visual graph later)
- Right side: empty until a file is selected, then shows diff/detail view
- Draggable dividers between panels
- Full keyboard support: tab between panels, arrow keys within, global shortcuts, discoverable hints

#### Status Bar
- Full status bar showing: current branch, clean/dirty status, ahead/behind remote count, last commit info
- Status bar messages for routine operations (not toast notifications)
- Command log: shows all git commands run with their output (humanized for common errors, raw for rare ones)

#### Branch Operations (Basic)
- Create, delete, checkout branches
- Remote-tracking branch labels shown on commits in the log view

### P1 — Core Workflow

#### Remote Operations
- Push, pull, fetch with progress indicators
- Clone flow (not in MVP — "open existing repo" only)
- Remote management and upstream tracking
- Safety guards: force-push warnings and confirmation
- Background execution with cancellation support for long-running operations

#### Search
- Commit message search in the log view
- Full search (commits, files, content via git grep/log -S) — added incrementally
- Command palette (Cmd+P style fuzzy search for actions)

#### Blame View
- Full blame/annotate view showing author and commit per line

#### Commit Detail View
- Click a commit in the log to see full diff and metadata in the main panel

#### Context Menus
- Right-click context menus on files, commits, branches
- Add file to .gitignore from context menu

#### Detached HEAD Protection
- Clear visual warning when in detached HEAD state
- Guided tooling to help the user create a branch or get back to a branch
- Prevent accidental entry into detached HEAD state where possible

#### Image/Binary Diff
- Side-by-side image comparison for changed image files
- Grabbable slider handle to reveal old/new image
- Onion skin overlay mode

### P2 — Advanced Features

#### Interactive Rebase
- Full interactive rebase UI: reorder, squash, fixup, edit, drop
- Drag-and-drop commit reordering (deferred, but planned)

#### Worktree Support
- List, create, switch between worktrees
- Submodule support within worktrees

#### Submodule Support
- Show submodule status, init, update, sync
- Full submodule management in the sidebar

#### Merge Conflict Resolution
- Inline conflict resolution for MVP (show conflict markers, pick sections)
- Three-way merge view (ours/theirs/base) — deferred

#### .gitignore Editor
- UI to edit .gitignore with pattern suggestions
- Right-click file to add to .gitignore

#### Cherry-pick & Revert
- Cherry-pick and revert from the commit log view

#### Branch Comparison
- Visual diff between any two branches/commits — deferred

#### Commit Log Filtering
- Filter by author, date range, file path, message text — deferred (start with message search)

#### Visual Commit Graph
- Rich visual graph with colored branch lines — deferred (start with flat list)

#### Tag Management
- View tags in log, create, delete — low priority

### P3 — Future / Deferred

- **Code Editor Plugin:** Build a code editor within floatinghotel, eventually extract as an afterhours plugin. Refer to ~/p/wordproc/docs/ and ~/p/wm_afterhours/docs/ for editor feature specs.
- **Terminal Emulator:** Full PTY-based embedded terminal — deferred, use command log panel for now
- **Vim Bindings:** Vim keybindings for the code editor only (not log/navigation views) — deferred
- **Syntax Highlighting:** In diff viewer — deferred
- **AI Features:** Auto-generated commit messages, diff explanations — deferred
- **Plugin System:** No plugin API for now, but design code to be modular and extensible
- **LFS Support:** Deferred
- **Monorepo Support:** Path filtering within large repos — deferred
- **Patch Management:** Create/apply .patch files — deferred
- **Other VCS:** Sapling, Mercurial, Jujutsu — deferred (git-first architecture)
- **Native Menu Bar:** Start with in-app menu bar using afterhours; switch to native when afterhours adds support
- **Full Theme Customization:** Start dark-only, add light mode and full customization later
- **Drag-and-Drop:** For interactive rebase commit reordering — deferred
- **Auto-Fetch:** Periodic background fetch from remotes — deferred (manual only for now)
- **Git Hooks UI:** Visibility into hook execution — deferred
- **Custom Actions/Scripts:** User-defined commands from the UI — deferred
- **Reflog Viewer:** Not needed in GUI
- **Code Review Integration:** Links to PRs + CI status only; no in-app review
- **GitHub/GitLab API:** Browser links only for now; PR creation deferred

---

## UI/UX Specifications

### Layout Structure

```
+-------------------------------------------------------+
|  [Menu Bar: File | Edit | View | Git | Help]          |
+-------------------------------------------------------+
|  [Toolbar: Commit | Push | Pull | Fetch | Branch ▾]   |
+-------------------+-----------------------------------+
|                   |                                   |
|  SIDEBAR          |  MAIN CONTENT                     |
|                   |                                   |
|  [Branch: main]   |  (empty until file selected)      |
|                   |                                   |
|  Changed Files:   |  - Diff view (side-by-side or     |
|   M src/main.cpp  |    inline, toggleable)            |
|   A README.md     |  - Commit detail view             |
|   D old_file.txt  |  - Blame view                     |
|                   |                                   |
|  ─── divider ───  |                                   |
|                   |                                   |
|  Commit Log:      |                                   |
|   abc1234 msg...  |                                   |
|   def5678 msg...  |                                   |
|   (flat list)     |                                   |
|                   |                                   |
+-------------------+-----------------------------------+
|  [Status Bar: branch | dirty | +3/-1 | last commit]  |
+-------------------------------------------------------+
```

### Sidebar
- **Top section:** Current branch name + changed files list
- **Bottom section:** Commit log (flat list, then graph later)
- **Switchable view:** Toggle between changed files view and git references view (branches, remotes, tags, stashes)
- Divider between sections is draggable

### Main Content Area
- Empty by default
- Shows diff view when a changed file is selected
- Shows commit detail view when a commit is selected from log
- Shows blame view when triggered

### Tabs
- Each tab is a fully independent repo view (own sidebar, log, diff)
- On launch: auto-open all previously open repos in tabs
- Pinnable tabs for important repos

### Input Model
- **Primary:** Mouse-driven
- **Secondary:** Full keyboard navigation (tab between panels, arrow keys, global shortcuts)
- **Shortcuts:** Discoverable via a keyboard shortcuts reference panel
- **Vim bindings:** Only for the code editor (deferred)

### Notifications
- Status bar messages for operation results
- No toast popups
- Errors only: status bar for routine, errors shown with humanized messages for common cases

### Destructive Operations
- Confirmation dialogs before all destructive operations (discard, force push, branch delete, etc.)
- No undo system — prevention over recovery

---

## Technical Architecture

### Git Backend
- Shell out to `git` CLI for all operations
- Parse stdout/stderr for results and errors
- Humanize common error messages; show raw for rare errors
- All git commands logged to in-app command log panel + log file

### Async Operations
- Long-running operations (push, pull, fetch, clone) run in background threads
- UI stays responsive during background operations
- Cancellation support for long-running operations
- Progress indicators in the status bar

### Filesystem Watching
- Auto-refresh on filesystem changes (debounced)
- Watch for changes to tracked files and git state (.git directory)

### Performance Targets
- **Startup:** Under 300ms to first render (match wordproc's Metal startup speed ~50ms actual, ~300ms with loading)
- **Loading strategy:** Show sidebar immediately with spinner while git data loads. Load commit log first, then background-load file trees and other data.
- **Memory:** Under 500MB RAM per repo. Track cache sizes and release when over budget.
- **Repo scaling:** Graceful degradation for large repos. Virtualized lists for commit logs and file trees.
- **Scrolling:** Match afterhours framework defaults. Verify native macOS smooth scrolling works — file issue with afterhours maintainers if not.

### State Persistence
- Full state persistence between launches:
  - Window size and position
  - Open repo tabs (auto-restore on launch)
  - Panel sizes and divider positions
  - Scroll positions
  - Active tab and selected items
  - Sidebar mode (changed files vs. git refs)

### Configuration
- JSON config file (like wordproc's settings.json)
- Stored in platform-appropriate location

### Logging
- In-app command log panel showing all git commands with output and timestamps
- Persistent log file for debugging
- Humanized error messages for common git errors (auth failure, merge conflict, etc.)

### Credential Handling
- Passthrough to git's configured credential helpers
- On auth failure: show error message with suggestion to configure git credentials
- No in-app credential management or dialogs

### GPG/SSH Signing
- Respect whatever git's config specifies
- No visual indicators for signed commits (deferred)

### Accessibility
- Follow afterhours framework defaults (minimum font sizes, clamping)
- No additional accessibility work beyond framework for now

---

## Integration Points

### External Editor
- Built-in code editor (P3 — deferred, will be developed as an afterhours plugin)
- For MVP: diffs are view-only in the app

### Terminal
- Embedded terminal emulator (P3 — deferred)
- For MVP: command log panel showing git operations and output

### GitHub/GitLab
- Browser links to remotes only
- Show CI status links alongside PR info — deferred
- No API integration for MVP

### CLI Launcher
- `fh` CLI command to open a repo in the app (e.g., `fh .` or `fh /path/to/repo`)

---

## Non-Functional Requirements

### Performance
- Sub-300ms startup to first render
- Smooth 60fps UI interactions
- Background git operations with no UI blocking
- Virtualized scrolling for large lists
- 500MB memory budget per repo tab

### Reliability
- Graceful handling of git errors (never crash on bad git output)
- Confirmation before all destructive operations
- Detached HEAD protection and recovery guidance

### Maintainability
- Modular architecture designed for future plugin system
- Code editor as extractable plugin
- Clean separation between git backend, UI, and state management
- Follow patterns established in wordproc and wm_afterhours

### Security
- No credential storage in the app
- Passthrough to git's security mechanisms
- No execution of arbitrary commands (until embedded terminal is added)

---

## MVP Scope — What to Build First

**Phase 1: Core Shell**
1. Application skeleton: window, Metal/Sokol rendering, afterhours ECS setup
2. Dark theme setup
3. Tab bar (single tab initially)
4. Sidebar + main content layout with draggable divider
5. Status bar
6. In-app menu bar (File, Edit, View, Git, Help)
7. JSON settings persistence
8. CLI launcher (`fh`)

**Phase 2: Git Read Operations**
1. Open a repo (path argument)
2. Parse `git status` — show changed files in sidebar with status badges
3. Parse `git log` — show flat commit list in sidebar
4. Parse `git diff` — show diff in main content area (inline view first)
5. Branch name + status in status bar
6. Remote tracking labels on commits

**Phase 3: Git Write Operations**
1. Staging: file-level stage/unstage
2. Interactive staging: hunk-level accept/skip/quit (git add -p style)
3. Commit: subject + body editor with 50/72 warnings
4. Commit button with dropdown (amend, fixup)
5. Branch: create, delete, checkout
6. Confirmation dialogs for destructive operations

**Phase 4: Essential UX**
1. Filesystem watching + auto-refresh
2. Side-by-side diff view (toggle with inline)
3. Commit detail view (click commit to see full diff)
4. Context menus on files, commits, branches
5. Command palette
6. Keyboard shortcut reference panel
7. Command log panel
8. Toolbar

**Phase 5: Advanced Git**
1. Push, pull, fetch with background execution + progress + cancel
2. Force-push safety guards
3. Blame view
4. Hunk-level discard with confirmation
5. Commit message templates
6. Multi-repo tabs (restore on launch)
7. Full state persistence

---

## Afterhours Framework Capabilities Audit

What's available in afterhours vs what needs building for floatinghotel.

### Available (Implemented)

| Category | Components |
|----------|-----------|
| Layout | `div()`, `scroll_view()`, `separator()`, flexbox (Row/Column, justify, align, wrap) |
| Buttons | `button()`, `button_group()`, `image_button()`, `checkbox()`, `radio_group()`, `toggle_switch()` |
| Input | `text_input()`, `text_area()`, `slider()`, `dropdown()` |
| Navigation | `tab_container()` (buggy — see Known Issues), `navigation_bar()`, `pagination()` |
| Display | `progress_bar()`, `circular_progress()`, `image()`, `sprite()`, `icon_row()` |
| Composite | `setting_row()` |
| Systems | Modal plugin (stacking, focus trapping), Toast plugin (auto-dismiss, severity), Animations (on_click, on_appear, on_hover, loop), Theming (`Theme`, `ComponentConfig`, color utilities) |
| Interaction | `HasClickListener`, `HasDragListener`, `HasLeftRightListener` |
| Text | `TextSelection`, `LineIndex`, `TextLayoutCache`, `text_input()`, `text_area()`, `CommandHistory<T>`, clipboard |

### Not Implemented — Needed for P0

These afterhours primitives are designed/documented but not yet implemented. floatinghotel must either implement them upstream or build app-local versions.

| Primitive | floatinghotel Need | Criticality |
|-----------|-------------------|-------------|
| **Draggable Divider** | Sidebar/main content resize, commit log/files split | BLOCKER — build first |
| **Split Pane** | Side-by-side diff view, sidebar + main layout | BLOCKER — depends on Draggable Divider |
| **Tree Node** | File tree view in sidebar (grouped by directory) | BLOCKER — needed for tree view of changed files |
| **Context Menu** | Right-click on files, commits, branches | HIGH — P1 feature but architecture matters early |
| **Dropdown Menu** | Menu bar (File, Edit, View, Git, Help), commit button dropdown | HIGH — needed for menu bar in Phase 1 |
| **Anchored Popup** | Commit dropdown (amend/fixup), branch selector | MEDIUM — needed for commit workflow |
| **Tab Strip** | Repo tabs (auto-restore on launch) | MEDIUM — Phase 5 but architecture matters |
| **Tooltip** | Keyboard shortcut hints, toolbar button labels | LOW for MVP |
| **Popover** | Commit button with dropdown arrow | MEDIUM |
| **List Box** | Commit log list, file list | MEDIUM — can workaround with scroll_view + buttons |
| **Fuzzy Matcher** | Command palette search | LOW for MVP (P1 feature) |

### Not Implemented — Not Needed for MVP

Combobox, Segmented Control, Accordion, Rating, Form Helpers, Breadcrumb Bar, Minimap, Gutter, Scroll Decoration Layer, Drag and Drop.

### Known Vendor Issues

| Component | Issue | Workaround |
|-----------|-------|------------|
| `tab_container()` | Tab strip renders at screen-absolute position, ignoring parent bounds | Build manual tab buttons in a row |
| `toggle_switch()` | Creates sibling entities consuming extra layout space | Use `with_no_wrap()`, increase container height |
| Clipboard shortcuts | `text_input` doesn't wire Ctrl+C/V/X — requires action binding system | Wire manually in app input system |

### Framework Primitives Build Order for floatinghotel

Based on dependency chains and Phase 1-2 needs:

1. **Draggable Divider** — unlocks sidebar resize, panel splits
2. **Split Pane** (uses Draggable Divider) — unlocks sidebar+main layout, side-by-side diff
3. **Dropdown Menu** — unlocks menu bar
4. **Tree Node** — unlocks file tree view
5. **Anchored Popup** — unlocks commit dropdown, tooltips, context menus
6. **Context Menu** (uses Anchored Popup) — unlocks right-click menus
7. **Tab Strip** — unlocks repo tabs (can defer to Phase 5)

Decision: Build these as app-local code first, extract to afterhours later if they prove reusable. Follow wordproc's pattern of building on top of `div()` + `HasClickListener` + `HasDragListener`.

---

## P0 Feature Specifications — Detailed

### Status View — Implementation Details

**Git commands:**
- `git status --porcelain=v2 --branch` — machine-parseable format, includes branch tracking info
- Parse output: `1 .M ...` (ordinary changed), `? ...` (untracked), `2 R. ...` (renamed)

**Status badges:** Single character in a fixed-width column left of filename.
- `M` yellow on dark background (modified)
- `A` green (added/new)
- `D` red (deleted)
- `R` blue (renamed)
- `U` gray (untracked)
- `C` orange (conflicted)
- Badge is rendered as colored text in a monospace font, 14px, left-padded 4px from filename

**View switching:** Three-state segmented control at top of sidebar file section:
- "Changed" — flat list of changed files only (default)
- "Tree" — changed files grouped by directory (collapsible tree nodes)
- "All" — full repository file tree with change indicators on modified files

**Refresh strategy:**
- On app focus / window activate: run `git status` immediately
- Filesystem watching (Phase 4): debounced 500ms after .git/ or tracked file changes
- Manual refresh: Cmd+R or toolbar button
- During git operations: refresh after operation completes

**Sidebar file list row layout:**
```
[badge] [icon?] filename.ext    [+3/-1]
```
- Row height: 24px
- Hover: subtle background highlight
- Selected: accent background color
- Click: show diff in main content area

### Diff Viewer — Implementation Details

**Git commands:**
- `git diff` — unstaged changes
- `git diff --cached` — staged changes
- `git diff HEAD` — all uncommitted changes
- `git diff <commit>` — changes in a specific commit

**Diff parsing:** Parse unified diff format:
- `---` / `+++` headers for file paths
- `@@ -start,count +start,count @@` hunk headers
- `-` lines (deletions), `+` lines (additions), ` ` lines (context)

**Diff coloring (MVP):**
- Deleted lines: red background (#3d1117) with red text (#ff7b72)
- Added lines: green background (#0d1117) with green text (#7ee787)
- Hunk headers: blue text, slightly dimmed
- Context lines: default text color

**Inline/unified view (default):**
- Single scrollable column showing the diff output
- Hunk headers displayed as separator bars with `@@ ... @@` text
- Line numbers in a gutter (old line number | new line number)

**Side-by-side view (toggle):**
- Uses `split_panel` with 50/50 split
- Left panel: old file (deletions highlighted)
- Right panel: new file (additions highlighted)
- Synchronized scrolling between panels
- Toggle via Cmd+Shift+D or View menu

**Keyboard navigation:**
- `]` / `[` — next/previous file in diff
- `j` / `k` — next/previous hunk within current file
- Focus indicators on current file and current hunk

**Change statistics:**
- Per file: `+12 -3` shown in file list next to filename
- Total: `5 files changed, +47 -12` shown in diff header area

### Interactive Staging — Implementation Details

**Approach:** Do NOT use `git add -p` interactively (stdin piping is fragile). Instead:
1. Parse the full diff with `git diff`
2. Display hunks in the diff viewer with stage/unstage buttons per hunk
3. Use `git apply --cached` to stage individual hunks (write hunk to temp file, apply)
4. Use `git apply --cached --reverse` to unstage individual hunks

**Hunk UI:**
- Each hunk has a header bar with: hunk range info, [Stage] button, [Discard] button
- Staged hunks: green left border indicator
- Unstaged hunks: default left border
- Keyboard: `s` to stage current hunk, `u` to unstage, `d` to discard (with confirmation)

**File-level staging:**
- `git add <file>` to stage entire file
- `git restore --staged <file>` to unstage entire file
- Available via right-click context menu or sidebar button

**Hunk discard (destructive):**
- `git checkout -- <file>` for full file discard
- `git apply --reverse` for single hunk discard
- Always shows confirmation dialog: "Discard this hunk? This cannot be undone."

### Commit Workflow — Implementation Details

**Commit message editor location:** Bottom of the sidebar, above the status bar. Expands when activated:
```
+-------------------+
| [Branch: main]    |
| Changed Files:    |
|   M src/main.cpp  |
|                   |
| ─── divider ───   |
|                   |
| Commit Log:       |
|   abc1234 msg...  |
|                   |
| ─── divider ───   |
|                   |
| Subject:          |
| [________________]|  <- text_input, 50 char warning
| Body:             |
| [________________]|  <- text_area, 72 char/line warning
| [Commit ▾] [Amend]|
+-------------------+
```

**50/72 rule warnings:**
- Subject line: character counter right-aligned (e.g., "47/50")
- Counter turns yellow at 50, red at 72
- Body lines: vertical guide line at column 72 (subtle, like a ruler)
- Warning text appears below the editor if exceeded

**Commit button dropdown** (uses Anchored Popup or Popover):
- Primary: "Commit" — `git commit -m "subject\n\nbody"`
- "Amend" — `git commit --amend`
- "Fixup" — `git commit --fixup=HEAD`
- "Commit & Push" — commit then push

**Unstaged changes dialog:**
- When committing with both staged and unstaged changes:
- Modal dialog: "You have unstaged changes. Commit only staged changes?"
- Buttons: [Stage All & Commit] [Commit Staged Only] [Cancel]
- Checkbox: "Remember this choice" (persisted in settings)

**Commit message templates:**
- Stored in `~/.config/floatinghotel/templates/` as `.txt` files
- Loaded via dropdown in commit editor: "Template: [None ▾]"
- Conventional commit helper: type prefix dropdown (feat:, fix:, chore:, etc.)

### Sidebar + Main Layout — Implementation Details

**Default dimensions:**
- Sidebar width: 280px (min: 180px, max: 50% of window)
- Commit log section: 40% of sidebar height
- Changed files section: 60% of sidebar height (minus commit editor when visible)
- All dividers draggable

**Commit log row layout:**
```
abc1234  Fix parsing bug              2h ago
         John Doe  (origin/main, HEAD)
```
- Row height: 40px (two lines: message + metadata)
- Short hash (7 chars), truncated subject, relative time
- Second line: author name, branch/tag decorations
- Selected commit: accent background, shows full diff in main content

**Commit log pagination:**
- Initial load: 100 commits via `git log --oneline -100`
- Lazy load on scroll: fetch next 100 when within 20 rows of bottom
- Full format: `git log --format="%H%x00%h%x00%s%x00%an%x00%aI%x00%D" -100`

**Empty main content state:**
- When no file selected: centered text "Select a file to view diff" with muted color
- When no repo open: centered text "Open a repository to get started" with [Open] button

### Status Bar — Implementation Details

**Layout (left to right):**
```
[branch-icon] main | ● dirty | ↑3 ↓1 | Last: abc1234 "Fix bug" 2h ago | [cmd log]
```

- Branch name: bold, clickable (opens branch switcher)
- Dirty indicator: colored dot (green = clean, yellow = dirty)
- Ahead/behind: arrows with counts (hidden if up-to-date)
- Last commit: abbreviated info
- Command log toggle: rightmost, opens/closes command log panel

**Humanized errors:** Lookup table for common git error patterns:
- "Authentication failed" → "Git credentials not configured. Run `git config credential.helper` to set up."
- "CONFLICT" → "Merge conflict in {file}. Resolve conflicts before committing."
- "rejected" + "non-fast-forward" → "Remote has new commits. Pull before pushing."
- Default: show raw stderr with monospace font

### Branch Operations — Implementation Details

**Branch list location:** Sidebar switchable view (toggle between "Changes" and "Refs"):
- Branches section: local branches, expandable remote branches
- Each branch row: name, ahead/behind indicators, current branch highlighted

**Create branch:** Modal dialog triggered from:
- Menu: Git > New Branch...
- Keyboard: Cmd+Shift+B
- Context menu on a commit: "Create Branch Here..."
- Fields: branch name input, "from" selector (defaults to HEAD)
- Command: `git checkout -b <name>` or `git switch -c <name>`

**Delete branch:** Context menu on branch in refs view:
- Confirmation dialog: "Delete branch '{name}'? This cannot be undone."
- Command: `git branch -d <name>` (safe), `git branch -D <name>` (force, with extra warning)

**Checkout branch:** Click branch name in refs view, or:
- Menu: Git > Checkout Branch...
- Command palette: type branch name
- Command: `git switch <name>`

**Remote tracking labels:** Displayed as colored badges on commit log rows:
- `origin/main` — blue badge
- `HEAD` — green badge
- Tags: gray badge
- Format from `git log --format="%D"`

---

## Application Architecture

### ECS Structure (Following wordproc Pattern)

**Single Editor Entity Pattern:** Like wordproc, create one main entity at startup with all core components attached. Systems query for entities with specific component combinations.

#### Core ECS Components

```cpp
namespace ecs {

// Repository state — the main data component
struct RepoComponent : public afterhours::BaseComponent {
    std::string repoPath;           // Absolute path to repo root
    std::string currentBranch;      // Current branch name
    bool isDirty = false;           // Has uncommitted changes
    bool isDetachedHead = false;
    std::string headCommitHash;
    int aheadCount = 0;
    int behindCount = 0;
    
    // Parsed git status
    struct FileStatus {
        std::string path;
        char indexStatus;       // Status in index (staged)
        char workTreeStatus;   // Status in work tree
        std::string origPath;  // For renames
    };
    std::vector<FileStatus> stagedFiles;
    std::vector<FileStatus> unstagedFiles;
    std::vector<std::string> untrackedFiles;
    
    // Parsed git log
    struct CommitEntry {
        std::string hash;
        std::string shortHash;
        std::string subject;
        std::string author;
        std::string authorDate;     // ISO 8601
        std::string decorations;    // Branch/tag labels
    };
    std::vector<CommitEntry> commitLog;
    int commitLogLoaded = 0;        // How many loaded so far
    bool commitLogHasMore = true;
    
    // Currently selected items
    std::string selectedFilePath;   // File selected in sidebar
    std::string selectedCommitHash; // Commit selected in log
    
    // Diff data
    struct DiffHunk {
        int oldStart, oldCount;
        int newStart, newCount;
        std::string header;
        std::vector<std::string> lines; // With +/-/ prefixes
    };
    struct FileDiff {
        std::string filePath;
        std::string oldPath;        // For renames
        int additions = 0;
        int deletions = 0;
        std::vector<DiffHunk> hunks;
    };
    std::vector<FileDiff> currentDiff;
};

// Layout state for panel sizes and divider positions
struct LayoutComponent : public afterhours::BaseComponent {
    float sidebarWidth = 280.0f;
    float sidebarMinWidth = 180.0f;
    float commitLogRatio = 0.4f;    // Portion of sidebar for commit log
    float commitEditorHeight = 0.0f; // 0 when hidden, expands when editing
    
    // View modes
    enum class SidebarMode { Changes, Refs };
    SidebarMode sidebarMode = SidebarMode::Changes;
    
    enum class FileViewMode { Flat, Tree, All };
    FileViewMode fileViewMode = FileViewMode::Flat;
    
    enum class DiffViewMode { Inline, SideBySide };
    DiffViewMode diffViewMode = DiffViewMode::Inline;
    
    // Computed rects (updated each frame)
    struct Rect { float x, y, width, height; };
    Rect menuBar{};
    Rect toolbar{};
    Rect sidebar{};
    Rect sidebarFiles{};
    Rect sidebarLog{};
    Rect sidebarCommitEditor{};
    Rect mainContent{};
    Rect statusBar{};
};

// Commit editor state
struct CommitEditorComponent : public afterhours::BaseComponent {
    std::string subject;
    std::string body;
    bool isVisible = false;
    bool isAmend = false;
    
    // Template
    std::string activeTemplate;
    std::string conventionalPrefix; // "feat:", "fix:", etc.
    
    // User preference
    enum class UnstagedPolicy { Ask, StageAll, CommitStagedOnly };
    UnstagedPolicy unstagedPolicy = UnstagedPolicy::Ask;
};

// Menu state
struct MenuComponent : public afterhours::BaseComponent {
    int activeMenuIndex = -1;
    bool commandLogVisible = false;
};

// Command log — records all git operations
struct CommandLogComponent : public afterhours::BaseComponent {
    struct Entry {
        std::string command;
        std::string output;
        std::string error;
        bool success;
        double timestamp;
    };
    std::vector<Entry> entries;
};

// Settings persistence
struct SettingsComponent : public afterhours::BaseComponent {
    int windowWidth = 1200;
    int windowHeight = 800;
    int windowX = 100;
    int windowY = 100;
    float sidebarWidth = 280.0f;
    float commitLogRatio = 0.4f;
    std::vector<std::string> openRepoPaths; // For tab restore
    std::string lastActiveRepo;
    std::string settingsFilePath;           // ~/.config/floatinghotel/settings.json
};

} // namespace ecs
```

#### Core ECS Systems

```
Pre-Layout (afterhours built-in):
  registerUIPreLayoutSystems()    — context begin, clear children

UI-Creating Systems (run between pre/post layout):
  MenuBarSystem                   — renders File/Edit/View/Git/Help menu bar
  ToolbarSystem                   — renders toolbar buttons (Commit, Push, Pull, etc.)
  SidebarSystem                   — renders sidebar (file list, commit log, commit editor)
  MainContentSystem               — renders diff view, commit detail, or empty state
  StatusBarSystem                 — renders status bar

Post-Layout (afterhours built-in):
  registerUIPostLayoutSystems()   — entity mapping, autolayout, interactions

Update Systems:
  GitRefreshSystem                — polls git status/log, updates RepoComponent
  InputSystem                     — keyboard shortcuts, navigation
  FilesystemWatchSystem           — watches .git/ for changes (Phase 4)
  AutoRefreshSystem               — debounced refresh on focus/timer
  SettingsPersistSystem           — saves/loads settings.json

Render Systems:
  MainRenderSystem                — begin_drawing, clear_background
  registerUIRenderSystems()       — afterhours UI rendering
  registerModalRenderSystems()    — modal backdrop
  registerToastSystems()          — toast notifications

Validation (dev mode only):
  register_systems<InputAction>() — layout/accessibility validators
```

### Module / File Layout

```
floatinghotel/
├── makefile
├── vendor/
│   └── afterhours/              # Vendored submodule
├── src/
│   ├── main.cpp                 # App entry, Sokol/Metal setup, system registration
│   ├── preload.cpp              # Font loading, resource init
│   ├── settings.h/.cpp          # JSON settings persistence (singleton)
│   ├── ecs/
│   │   ├── components.h         # All ECS components (RepoComponent, LayoutComponent, etc.)
│   │   ├── sidebar_system.h     # Sidebar UI rendering
│   │   ├── main_content_system.h # Diff/commit detail rendering
│   │   ├── menu_bar_system.h    # Menu bar rendering + action handling
│   │   ├── toolbar_system.h     # Toolbar rendering
│   │   ├── status_bar_system.h  # Status bar rendering
│   │   ├── input_system.h       # Keyboard shortcuts, navigation
│   │   ├── git_refresh_system.h # Background git data refresh
│   │   └── component_helpers.h  # Pure functions on component data
│   ├── git/
│   │   ├── git_runner.h         # Shell out to git CLI, capture stdout/stderr
│   │   ├── git_parser.h         # Parse git status, log, diff output
│   │   ├── git_commands.h       # High-level git operations (stage, commit, branch)
│   │   └── error_humanizer.h    # Map common git errors to friendly messages
│   ├── ui/
│   │   ├── theme.h              # Dark theme colors, layout constants
│   │   ├── ui_context.h         # Toast notifications, UI helpers
│   │   ├── diff_renderer.h      # Diff coloring, hunk rendering
│   │   ├── split_panel.h        # App-local split pane (until afterhours has it)
│   │   ├── tree_view.h          # App-local tree node (until afterhours has it)
│   │   ├── context_menu.h       # App-local context menu (until afterhours has it)
│   │   └── menu_setup.h         # Menu bar definition
│   ├── input/
│   │   └── action_map.h         # Keyboard shortcut mappings
│   └── util/
│       ├── logging.h            # Log macros, log file output
│       ├── clipboard.h          # System clipboard wrapper
│       └── process.h            # Subprocess execution (for git CLI)
├── resources/
│   └── fonts/                   # Monospace + proportional fonts
├── output/                      # Build output
└── tests/
    └── e2e_scripts/             # E2E test scripts
```

### Git Backend Architecture

**Subprocess execution pattern:**
```cpp
struct GitResult {
    std::string stdout;
    std::string stderr;
    int exitCode;
    bool success() const { return exitCode == 0; }
};

// Synchronous (for fast operations < 100ms)
GitResult git_run(const std::string& repoPath, const std::vector<std::string>& args);

// Asynchronous (for slow operations: push, pull, fetch, clone)
std::future<GitResult> git_run_async(const std::string& repoPath,
                                      const std::vector<std::string>& args,
                                      std::function<void(float)> onProgress = nullptr);
```

**Key git commands by feature:**

| Feature | Command |
|---------|---------|
| Status | `git status --porcelain=v2 --branch` |
| Log | `git log --format="%H%x00%h%x00%s%x00%an%x00%aI%x00%D" -100` |
| Diff (unstaged) | `git diff` |
| Diff (staged) | `git diff --cached` |
| Diff (commit) | `git show <hash> --format=""` |
| Stage file | `git add <path>` |
| Unstage file | `git restore --staged <path>` |
| Stage hunk | `git apply --cached <patch-file>` |
| Unstage hunk | `git apply --cached --reverse <patch-file>` |
| Discard hunk | `git apply --reverse <patch-file>` |
| Commit | `git commit -m <message>` |
| Amend | `git commit --amend -m <message>` |
| Create branch | `git switch -c <name>` |
| Delete branch | `git branch -d <name>` |
| Checkout branch | `git switch <name>` |
| Branch list | `git branch -a --format="%(refname:short) %(upstream:short) %(upstream:track)"` |

**Parsing strategy:**
- Use `--porcelain=v2` and `--format=` with NUL separators (`%x00`) for reliable parsing
- Never parse human-readable output — always use machine-readable formats
- Log all commands to CommandLogComponent for debugging

### Dark Theme Color Palette

```cpp
namespace theme {
// Window chrome
constexpr Color WINDOW_BG       = {30, 30, 30, 255};     // #1E1E1E
constexpr Color SIDEBAR_BG      = {37, 37, 38, 255};     // #252526
constexpr Color PANEL_BG        = {30, 30, 30, 255};     // #1E1E1E
constexpr Color BORDER          = {58, 58, 58, 255};     // #3A3A3A

// Text
constexpr Color TEXT_PRIMARY     = {204, 204, 204, 255};  // #CCCCCC
constexpr Color TEXT_SECONDARY   = {128, 128, 128, 255};  // #808080
constexpr Color TEXT_ACCENT      = {78, 154, 6, 255};     // Muted green accent

// Status badges
constexpr Color STATUS_MODIFIED  = {227, 179, 65, 255};   // Yellow
constexpr Color STATUS_ADDED     = {87, 166, 74, 255};    // Green
constexpr Color STATUS_DELETED   = {220, 76, 71, 255};    // Red
constexpr Color STATUS_RENAMED   = {78, 154, 220, 255};   // Blue
constexpr Color STATUS_UNTRACKED = {128, 128, 128, 255};  // Gray
constexpr Color STATUS_CONFLICT  = {220, 140, 50, 255};   // Orange

// Diff colors
constexpr Color DIFF_ADD_BG      = {13, 17, 23, 255};     // Dark green tint
constexpr Color DIFF_ADD_TEXT     = {126, 231, 135, 255};  // Green text
constexpr Color DIFF_DEL_BG      = {61, 17, 23, 255};     // Dark red tint
constexpr Color DIFF_DEL_TEXT     = {255, 123, 114, 255};  // Red text
constexpr Color DIFF_HUNK_HEADER = {78, 154, 220, 255};   // Blue

// Interactive
constexpr Color BUTTON_PRIMARY   = {0, 122, 204, 255};    // Blue
constexpr Color BUTTON_SECONDARY = {58, 58, 58, 255};     // Dark gray
constexpr Color HOVER_BG         = {44, 44, 44, 255};     // Slight highlight
constexpr Color SELECTED_BG      = {4, 57, 94, 255};      // Deep blue selection
constexpr Color FOCUS_RING       = {0, 122, 204, 255};    // Blue focus

// Status bar
constexpr Color STATUS_BAR_BG    = {0, 122, 204, 255};    // Blue (like VS Code)
constexpr Color STATUS_BAR_TEXT  = {255, 255, 255, 255};   // White
}
```

---

## Missing P0 Specs — Menu Bar, Toolbar, and Repo Open Flow

### Menu Bar Item Definitions

Each menu bar header opens a dropdown menu (built on the app-local Dropdown Menu primitive).

**File:**
- Open Repository... (Cmd+O) — native directory picker
- Open Recent > (submenu with recent repos)
- Close Tab (Cmd+W)
- Separator
- Settings... (Cmd+,)
- Separator
- Quit (Cmd+Q)

**Edit:**
- Copy (Cmd+C) — copies selected diff text, file path, or commit hash depending on context
- Select All (Cmd+A) — context-dependent
- Separator
- Find... (Cmd+F) — opens search in current context

**View:**
- Toggle Sidebar (Cmd+B)
- Toggle Command Log (Cmd+Shift+L)
- Separator
- Inline Diff (Cmd+Shift+I)
- Side-by-Side Diff (Cmd+Shift+D)
- Separator
- Changed Files View
- Tree View
- All Files View
- Separator
- Zoom In (Cmd+=)
- Zoom Out (Cmd+-)
- Reset Zoom (Cmd+0)

**Git:**
- Stage File (Cmd+Shift+S)
- Unstage File (Cmd+Shift+U)
- Separator
- Commit... (Cmd+Enter)
- Amend Last Commit
- Separator
- New Branch... (Cmd+Shift+B)
- Checkout Branch... (Cmd+Shift+O)
- Separator
- Push (Cmd+Shift+P) — P1
- Pull (Cmd+Shift+L) — P1
- Fetch — P1

**Help:**
- Keyboard Shortcuts (Cmd+?)
- Command Log
- About floatinghotel

### Toolbar Buttons (Left to Right)

```
[Refresh] [Stage All] [Unstage All] | [Commit] [Push ▾] [Pull] [Fetch] | [Branch: main ▾]
```

- **Refresh:** Re-run `git status` + `git log`
- **Stage All:** `git add -A` with confirmation if >20 files
- **Unstage All:** `git restore --staged .`
- **Separator:** Visual divider
- **Commit:** Opens/focuses commit editor in sidebar
- **Push ▾:** Push with dropdown (Push, Force Push with warning)
- **Pull:** `git pull`
- **Fetch:** `git fetch --all`
- **Separator:** Visual divider
- **Branch selector:** Shows current branch name, dropdown to switch branches

Toolbar buttons use `button()` with `ComponentConfig{}.with_label("").with_debug_name("btn_name")` and icon overlays (following wordproc's `ToolbarOverlayRenderSystem` pattern).

### First Launch / Repo Open Flow

1. **CLI launch (`fh .` or `fh /path/to/repo`):** App opens with the specified repo loaded. Skip any open dialog.
2. **App launch with no arguments and no previous state:** Show centered empty state: "Open a repository to get started" with [Open Repository] button. Button triggers native directory picker (`NSOpenPanel` on macOS).
3. **App launch with saved state:** Restore all previously open repo tabs and last active tab.
4. **"Open Repository" flow:**
   - Native directory picker (not in-app file browser)
   - Validate selected directory: run `git rev-parse --git-dir` to verify it's a git repo
   - On failure: show error toast "Not a git repository" and re-show the directory picker
   - On success: load repo data and populate sidebar

Use wordproc's deferred dialog pattern: set a `PendingDialog` flag in the ECS system, execute the native dialog at the top of `app_frame()` (before `systemManager->run(dt)`) to avoid blocking inside ECS.

---

## Build System

### Makefile Template

Follow wordproc's makefile pattern:

```makefile
UNAME_S := $(shell uname -s)
CXX := clang++
CXXSTD := -std=c++23

# macOS: Metal + Sokol
FRAMEWORKS := -framework CoreFoundation \
    -framework Metal -framework MetalKit -framework Cocoa -framework QuartzCore

# Include paths
INCLUDES := -I vendor/ -I vendor/afterhours/src -I src/

# Sources: all .cpp in src/ (recursively)
SRCS := $(shell find src/ -name '*.cpp') $(shell find src/ -name '*.mm')
OBJS := $(SRCS:%.cpp=output/%.o)

# Targets
all: output/floatinghotel

output/floatinghotel: $(OBJS)
	$(CXX) $(CXXSTD) $(CXXFLAGS) -o $@ $^ $(FRAMEWORKS)

# Emscripten target for web build
web: ...
```

### Vendor Dependencies

| Library | Purpose | Source |
|---------|---------|--------|
| afterhours | ECS + UI framework | git submodule (~/p/wm_afterhours) |
| nlohmann/json.hpp | JSON settings persistence | Single header, vendor/ |
| argh.h | CLI argument parsing (`fh .`) | Single header, vendor/ |
| sago/ | Platform folder paths (`~/.config/floatinghotel/`) | vendor/ (from wordproc) |
| backward/ | Stack traces for crash debugging | vendor/ (from wordproc) |

No additional dependencies needed for the git backend (shells out to git CLI).

---

## Subprocess Execution (Git Backend)

### Implementation: `posix_spawn` + pipe

Use `posix_spawn()` with pipes for stdout/stderr capture. This is non-blocking, fork-safe, and works on all POSIX platforms.

```cpp
// src/util/process.h
struct ProcessResult {
    std::string stdout_str;
    std::string stderr_str;
    int exit_code;
    bool success() const { return exit_code == 0; }
};

// Synchronous — for fast git operations (<100ms: status, diff, log, add, commit)
ProcessResult run_process(const std::string& working_dir,
                          const std::vector<std::string>& args);

// Asynchronous — for slow git operations (push, pull, fetch, clone)
std::future<ProcessResult> run_process_async(
    const std::string& working_dir,
    const std::vector<std::string>& args,
    std::function<void(const std::string&)> on_output = nullptr);
```

### Threading Model

| Operation | Threading | Reason |
|-----------|-----------|--------|
| `git status` | Main thread (sync) | Fast (<50ms typical), UI needs result immediately |
| `git log` | Main thread (sync) | Fast for 100 commits |
| `git diff` | Main thread (sync) | Fast for typical diffs |
| `git add/restore` | Main thread (sync) | Fast, UI updates immediately after |
| `git commit` | Main thread (sync) | Fast, but UI blocks briefly (acceptable) |
| `git push/pull/fetch` | Background thread (async) | Slow (network), must not block UI |
| `git clone` | Background thread (async) | Slow (network + disk) |

For async operations: use `std::future<ProcessResult>` returned by `run_process_async()`. The `GitRefreshSystem` checks for completed futures each frame and updates `RepoComponent` when done.

---

## Architectural Patterns (from wordproc)

### Patterns to Follow

1. **Single-entity ECS for app state:** One main entity holds all components (`RepoComponent`, `LayoutComponent`, `CommitEditorComponent`, etc.). Systems query for this entity via `EntityQuery({.force_merge = true}).whereHasComponent<RepoComponent>().gen()`.

2. **Callback-driven app lifecycle:** Use `afterhours::graphics::run(cfg)` with `app_init()`, `app_frame()`, `app_cleanup()` callbacks. The framework owns the event loop.

3. **`app_state` namespace for shared pointers:** Store raw pointers to the entity and its components in an `app_state` namespace for quick access in frame callbacks, avoiding repeated ECS queries.

4. **System registration order is critical:**
   ```
   1. ui_imm::registerUIPreLayoutSystems(sm)     — clear children, begin context
   2. App UI-creating systems                     — MenuBarSystem, ToolbarSystem,
                                                    SidebarSystem, MainContentSystem,
                                                    StatusBarSystem
   3. ui_imm::registerUIPostLayoutSystems(sm)     — entity mapping, autolayout
   4. Update systems                              — GitRefreshSystem, InputSystem, etc.
   5. ui_imm::registerToastSystems(sm)            — toast notifications
   6. ui_imm::registerModalSystems(sm)            — modal input blocking
   7. Render systems                              — MainRenderSystem, UI render,
                                                    modal render
   8. Validation systems (dev only)               — layout/accessibility validators
   ```

5. **Preload singleton pattern:** Create a `Preload` singleton that initializes `files::init()`, creates the afterhours singleton entity with input mappings, window manager, UI plugin, and loads fonts. Run before system registration.

6. **Deferred native dialogs:** Native file pickers (`NSOpenPanel`) block the thread. Set a `PendingDialog` flag in ECS systems, execute the dialog at the top of `app_frame()` before `systemManager->run(dt)`.

7. **Background preloading:** Launch repo data loading on a background thread before `graphics::run()`. Collect the result in `app_init()` after the ~300ms window creation time.

8. **ActionMap for keyboard shortcuts:** Define an `Action` enum and `ActionMap` class that maps `KeyBinding` structs (key + modifiers) to actions. Systems check `actionMap.isActionPressed(Action::Commit)`.

9. **Theme namespace for constants:** Define all colors, layout constants (heights, paddings), and font sizes in a `theme` namespace. Reference these constants everywhere instead of magic numbers.

10. **SCOPED_TIMER for startup profiling:** Use wordproc's `SCOPED_TIMER("label")` macro to measure each initialization step. Target: under 300ms total.

### Patterns to Diverge From

1. **Win95 theming:** floatinghotel uses a modern dark theme, not Win95 bevels. Use `with_roundness(0.04f)` for subtle rounding instead of `with_roundness(0.0f)`.
2. **Document model:** wordproc has a TextBuffer/gap buffer for text editing. floatinghotel has no editable text content for MVP — diffs are view-only.
3. **Custom rendering:** wordproc's `EditorRenderSystem` does extensive custom text rendering. floatinghotel should rely more heavily on afterhours UI primitives for the sidebar/log and only do custom rendering for the diff viewer.

---

## Open Questions / Follow-ups

1. **Visual design:** Generate concept art to finalize the visual direction before building the theme
2. **Scrolling behavior:** Verify afterhours smooth scrolling matches native macOS feel; file upstream issue if not
3. ~~**Code editor features:**~~ DEFERRED to P3. Not needed for MVP.
4. ~~**Minimap/scrollbar:**~~ DEFERRED to P3.
5. **Staging dialog UX:** "Commit only staged changes?" dialog with "remember" checkbox needs design mockup
6. **Image diff modes:** Slider reveal and onion skin need custom rendering — investigate afterhours capabilities. DEFERRED to P1.
7. **~~Afterhours primitives availability~~** — RESOLVED: Documented above. 6 primitives need building for P0/P1 (Draggable Divider, Split Pane, Tree Node, Context Menu, Dropdown Menu, Anchored Popup)
8. ~~**git status refresh performance:**~~ RESOLVED: Run synchronously on main thread for MVP. If >100ms on large repos, move to background thread and show stale indicator. Benchmark during Phase 2.
9. ~~**Hunk staging via git apply:**~~ RESOLVED: `git apply --cached` with extracted hunks is the correct approach (used by VS Code, GitKraken, etc.). Test edge cases during Phase 3 implementation.
10. ~~**Commit editor placement:**~~ RESOLVED: Bottom-of-sidebar for MVP. Can revisit if UX testing reveals issues.
11. ~~**Afterhours primitives — build upstream or app-local?**~~ RESOLVED: Start app-local in `src/ui/`, extract to afterhours when stable.
12. **Diff viewer virtualization:** Large diffs (>10k lines) need virtualized rendering. Use `scroll_view()` with only-render-visible-lines optimization. Spec the approach during Phase 2.
13. **Clipboard shortcuts in text_input:** Afterhours `text_input()` doesn't wire Ctrl+C/V/X. Need to wire manually via ActionMap (same issue wordproc has). Handle in InputSystem.
