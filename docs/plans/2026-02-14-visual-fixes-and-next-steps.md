# Visual Fixes & Next Steps

Current state: the app builds and opens a window, but the UI scale is too small and the visual design needs work. This doc covers what to fix immediately, what to validate, and what to build next.

---

## 1. Immediate: UI Scale & Font Fixes

The app looks tiny because of undersized constants and missing fonts.

### Font Sizes Are Too Small

`src/ui/theme.h` defines:
```
FONT_SIZE_UI   = 13.0f   // UI chrome
FONT_SIZE_MONO = 14.0f   // Code/diffs
FONT_SIZE_SMALL = 11.0f  // Small labels
```

`main.cpp` sets the default to 16px via `UIStylingDefaults`, but the theme constants override this everywhere they're used directly. These need to agree.

**Fix:** Bump theme constants to match the intent:
- `FONT_SIZE_UI` -> 15.0f (minimum)
- `FONT_SIZE_MONO` -> 14.0f is OK for code, but test 15.0f
- `FONT_SIZE_SMALL` -> 12.0f (floor)
- Audit every system file to ensure they use `theme::layout::FONT_SIZE_*` and not hardcoded numbers

### No Monospace Font Loaded

`preload.cpp` loads Roboto for both `DEFAULT_FONT` and `SYMBOL_FONT`. The Nerd Font is in `resources/fonts/` but never loaded. Diffs and code will look wrong without a monospace font.

**Fix:**
- Add a monospace font to `resources/fonts/` (e.g. JetBrains Mono, Fira Code, or SF Mono)
- Load it as a named font in `preload.cpp` (e.g. `"mono"`)
- Use it in `diff_renderer.h` and anywhere showing code/paths/hashes
- Load `SymbolsNerdFont-Regular.ttf` as the `SYMBOL_FONT` instead of Roboto

### Layout Constants Are Too Compact

```
MENU_BAR_HEIGHT    = 28   // cramped
TOOLBAR_HEIGHT     = 36
STATUS_BAR_HEIGHT  = 24   // cramped
FILE_ROW_HEIGHT    = 24   // cramped
COMMIT_ROW_HEIGHT  = 40
PADDING            = 8
SMALL_PADDING      = 4
```

On a retina Mac these will feel very tight.

**Fix:** Increase across the board:
- `MENU_BAR_HEIGHT` -> 32-36
- `STATUS_BAR_HEIGHT` -> 28-32
- `FILE_ROW_HEIGHT` -> 28-32
- `PADDING` -> 10-12
- `SMALL_PADDING` -> 6
- Test at 1x and 2x to make sure it feels comfortable

### DPI / Retina Awareness

Check whether Sokol's Metal backend reports logical vs physical pixels via `sapp_dpi_scale()`. If layout math uses physical pixels on retina, everything will be half-size.

**Fix:** Verify `get_screen_width()`/`get_screen_height()` return logical pixels. If not, apply `sapp_dpi_scale()` factor to all layout constants.

---

## 2. Immediate: Visual Design Pass

The colors are fine (VS Code dark palette) but the components likely look flat and unpolished.

### Spacing & Breathing Room

- Add padding inside sidebar panels (files list, commit log)
- Add margin between file rows, commit rows
- Add vertical padding in the status bar text
- Ensure toolbar buttons have enough horizontal spacing

### Roundness

REQUIREMENTS.md says `with_roundness(0.04f)` for subtle rounding. Check if any components actually apply this. Buttons, panels, badges, and the status bar should all have slight rounding.

### Borders & Separators

- Add visible border between sidebar and main content (1px `theme::BORDER` color)
- Add separator lines between sidebar sections (files vs commits)
- Menu bar should have a bottom border
- Toolbar should have a bottom border

### Color Refinement

- `TEXT_ACCENT = {78, 154, 6, 255}` is a muddy green -- consider a cleaner accent
- Diff backgrounds (`DIFF_ADD_BG`, `DIFF_DEL_BG`) may be too dark to distinguish from `WINDOW_BG`
- Status badges might need more saturation to pop against the dark sidebar
- Hover/selection states need testing -- `HOVER_BG = {44, 44, 44}` might be too subtle

### Component Styling Checklist

- [ ] Buttons: rounded corners, visible border, hover/active state transitions
- [ ] File rows: hover highlight, selected state with accent color
- [ ] Commit rows: two-line layout with clear hierarchy (hash bold, message normal, time dimmed)
- [ ] Status badges: colored text on transparent background, monospace font
- [ ] Branch badges in commit log: pill-shaped with background color
- [ ] Menu dropdowns: background, shadow/border, hover highlight on items
- [ ] Toolbar buttons: icon-sized, consistent spacing, disabled state
- [ ] Status bar: text vertically centered, items spaced evenly
- [ ] Dividers: visible on hover, subtle at rest

---

## 3. Validate: What Actually Works

Before building new features, click through everything and log what's broken.

### Checklist

- [ ] **Sidebar populates**: Does `git status` data show up as file rows?
- [ ] **Commit log populates**: Does `git log` data show as commit entries?
- [ ] **File selection**: Does clicking a file in the sidebar show a diff in the main panel?
- [ ] **Diff rendering**: Are added/deleted lines colored correctly? Are hunk headers shown?
- [ ] **Branch name**: Does the status bar show the current branch?
- [ ] **Dirty indicator**: Does the status bar show clean/dirty state?
- [ ] **Menu bar**: Do dropdowns open and close correctly?
- [ ] **Menu hover-switch**: Can you hover between adjacent menu headers while a dropdown is open?
- [ ] **Toolbar buttons**: Do Refresh, Stage All, Unstage All respond to clicks?
- [ ] **Split panel divider**: Can you drag the sidebar divider to resize?
- [ ] **Sidebar internal divider**: Can you drag the files/commits divider?
- [ ] **Scroll**: Do file list and commit log scroll when they overflow?
- [ ] **Window resize**: Do all panels reflow correctly?
- [ ] **Staging**: Does clicking stage on a file actually run `git add`?
- [ ] **Unstaging**: Does unstage run `git restore --staged`?
- [ ] **Commit editor**: Does the commit message input appear and accept text?
- [ ] **Commit**: Does the commit button run `git commit`?
- [ ] **Async refresh**: After a git operation, does the sidebar update?
- [ ] **Empty state**: With no repo, does it show the "Open repository" message?
- [ ] **CLI path**: Does `./output/floatinghotel.exe /path/to/repo` open that repo?

Log results as: WORKS / BROKEN (describe) / UNTESTED

---

## 4. Next Features: Prioritized Work Plan

Based on the TASKS_BREAKDOWN.md, here's what to tackle after visuals are fixed and validation is done. Organized by what will make the app usable fastest.

### Tier 1: Make It Usable Daily (P0 gaps)

These are the remaining P0 items that aren't yet validated as working:

1. **File-level staging/unstaging** (TASK-20) -- The git commands exist in `git_commands.h` but the UI wiring (sidebar buttons, refresh after action) needs validation and likely fixing.

2. **Hunk-level staging** (TASK-21) -- `build_patch()` exists and has tests, but the diff renderer hunk buttons and the `git apply --cached` flow need to be wired end-to-end.

3. **Commit workflow** (TASK-23) -- Commit editor component exists, but the actual `git commit` invocation, editor clear on success, and post-commit refresh need validation.

4. **Branch operations** (TASK-26) -- Branch list parsing exists, but create/delete/checkout UI (modals, sidebar refs view toggle) likely needs building.

5. **Confirmation dialogs** (TASK-24) -- Modal plugin is registered but dialogs for destructive ops (discard, delete branch) need to be wired.

### Tier 2: Essential Polish (P1 high-value)

6. **Keyboard navigation** (TASK-35) -- Tab between panels, arrow keys in lists, `s`/`u`/`d` for stage/unstage/discard in diff view. This is what makes it faster than the terminal.

7. **Side-by-side diff view** (TASK-29) -- Toggle between inline and split diff. The split panel widget exists, just needs a second diff rendering mode.

8. **Commit detail view** (TASK-30) -- Click a commit in the log to see its full diff. Reuses the diff renderer.

9. **Command log panel** (TASK-33) -- The `CommandLogComponent` already records all git commands. Just needs a toggleable panel to display them.

### Tier 3: Daily Driver Features (P1 remaining)

10. **Context menus** (TASK-31, 32) -- Right-click on files, commits, branches. Widget exists in `context_menu.h`, needs integration.

11. **Git refresh system** (TASK-38) -- Auto-refresh on focus, after operations, manual Cmd+R.

12. **Push/pull/fetch** (TASK-41) -- Async runner exists, needs UI buttons + progress indicator.

13. **Settings persistence** (TASK-36) -- Settings module exists but needs window size/position save/restore.

---

## 5. Suggested Session Order

For the next few work sessions:

**Session 1: Visual Foundation**
- Fix font sizes and load monospace font
- Bump layout constants
- Check DPI scaling
- Quick spacing/roundness pass on all components
- Goal: app looks decent enough to use

**Session 2: Validate & Fix**
- Run through the validation checklist above
- Fix broken features (expect sidebar, diffs, and staging to need fixes)
- Goal: file selection -> diff view -> stage -> commit flow works end-to-end

**Session 3: Core Workflow Completion**
- Wire up hunk staging, branch operations, confirmation dialogs
- Goal: all P0 write operations functional

**Session 4: Keyboard & Polish**
- Add keyboard navigation
- Side-by-side diff toggle
- Commit detail view
- Goal: faster than command line for daily git workflow

---

## Open Questions

1. **Monospace font choice**: JetBrains Mono (open source, ligatures), Fira Code, or something else?
2. **afterhours commit**: The submodule is on latest (`a7485b4`) but the previous local copy had unreleased commits. Any afterhours features that need to be pushed upstream first?
3. **Retina testing**: Need to verify DPI behavior on both retina and non-retina displays.
4. **Target polish level**: "Good enough to use daily" or "polished enough to show people"? This affects how much time to spend on visual refinement vs. features.
