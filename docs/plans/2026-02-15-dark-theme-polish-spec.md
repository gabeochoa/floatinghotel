# Dark Theme Polish Spec

Target look: VS Code dark theme structure + GitButler's clean file list.
This doc maps every visual gap to a specific code change.

---

## 1. Surface Hierarchy (The #1 Problem)

Right now `SIDEBAR_BG`, `PANEL_BG`, and `WINDOW_BG` are all `#252526`. Everything blends into one flat slab.

VS Code uses 4-5 tonal levels to create depth:

| Surface | VS Code hex | Current hex | Delta |
|---------|------------|-------------|-------|
| Title bar / menu bar | `#3C3C3C` | `#333333` (menu_colors) | OK |
| Sidebar | `#252526` | `#252526` | Keep |
| Editor (main content) | `#1E1E1E` | `#252526` | **Darken to `#1E1E1E`** |
| Panel (command log) | `#1E1E1E` | `#252526` | **Darken** |
| Status bar | `#007ACC` | `#007ACC` | OK |
| Section headers | `#1F1F1F` | `#252526` | **Darken to `#1F1F1F`** |

### Changes to `theme.h`:

```cpp
// Window chrome — tonal hierarchy (NOT all the same color)
inline Color WINDOW_BG       = {30, 30, 30, 255};     // #1E1E1E (editor/main)
inline Color SIDEBAR_BG      = {37, 37, 38, 255};     // #252526 (sidebar, lighter)
inline Color PANEL_BG        = {30, 30, 30, 255};     // #1E1E1E (matches editor)
inline Color SECTION_HEADER_BG = {31, 31, 31, 255};   // #1F1F1F (section headers)
```

The sidebar being lighter than the editor makes the content area feel recessed and focused. This is the single biggest polish win.

---

## 2. Sidebar-to-Content Border

The 2px vertical divider exists but is invisible because both surfaces are the same color. With the tonal fix above, the divider becomes naturally visible. But reinforce it:

```cpp
inline Color BORDER          = {48, 48, 48, 255};     // #303030 (slightly brighter)
```

The divider in `MainContentSystem` is already 2px at `theme::BORDER` — just needs the color contrast from the surface hierarchy fix.

---

## 3. Roundness Pass

Current: `with_roundness(0.0f)` on everything.
Target: Subtle rounding on interactive elements, sharp on structural containers.

| Element | Target roundness | Current |
|---------|-----------------|---------|
| Buttons (toolbar, dialog) | `0.04f` | `0.04f` (already OK) |
| Badges (branch, status) | `0.15f-0.3f` | `0.15f-0.3f` (OK) |
| File rows | `0.0f` | `0.0f` (correct — list items shouldn't round) |
| Section headers | `0.0f` | `0.0f` (correct) |
| Sidebar container | `0.0f` | `0.0f` (correct — structural) |
| Diff hunk headers | `0.0f` | `0.0f` (correct) |
| Modal dialogs | `0.04f` | inherits from modal plugin |
| Commit meta box | `0.04f` | `0.04f` (OK) |
| Tab buttons (Changes/Refs) | `0.04f` | `0.04f` (OK) |

Most roundness is already fine. The main issue is the flat surfaces, not the corners.

---

## 4. Row Hover States

Current: File rows and commit rows have no visible hover state. Clicks work but nothing highlights on mouseover.

VS Code highlights rows with `#2A2D2E` on hover, `#094771` on selection.

### Approach:

The `button()` primitive from afterhours already supports hover via its built-in interaction. But the file rows use a `div` wrapper + `button` with transparent BG. The hover needs to apply to the outer row container.

**Change file rows to use `button()` as the outer container** instead of `div()`:

In `sidebar_system.h`, `render_file_row()`:
- Change the outer `div` (rowContainer) to a `button`
- Set its background to `theme::SIDEBAR_BG`
- The framework's built-in hover will lighten it
- OR: add a new theme color `HOVER_BG` and use it explicitly

```cpp
// New hover color — visible but subtle
inline Color HOVER_BG         = {44, 47, 48, 255};     // #2C2F30 (VS Code hover)
inline Color SELECTED_BG      = {9, 71, 113, 255};     // #094771 (VS Code selection)
```

Same approach for commit rows.

---

## 5. File Row Typography

Current: filenames at `FONT_META` (12px). Too small.

VS Code sidebar uses ~13px for filenames, dimmer for directory paths.

### Changes:

```
File row: filename at FONT_BODY (14px), directory at FONT_META (12px) in TEXT_SECONDARY
Status badge: FONT_CHROME (13px) — slightly bigger than current FONT_META
```

Currently the label is composed as `fname + "  " + dir` in one string. To get different weights, either:
- (Simple) Keep one string but bump to `FONT_CHROME` (13px) — good enough
- (Better) Split into two child divs: filename bold + directory dimmed

**Recommended: bump to FONT_CHROME for the whole label** — simplest change, immediate improvement.

---

## 6. Section Header Styling

Current: section headers ("STAGED CHANGES 3") use `FONT_BODY` (14px) at 160,160,160 on `SIDEBAR_BG`.

VS Code uses smaller, uppercase section headers on a slightly different background:

```cpp
// Section headers: smaller, distinct background
.with_font_size(h720(theme::layout::FONT_CAPTION))  // 11px, not 14px
.with_custom_background(theme::SECTION_HEADER_BG)    // #1F1F1F (darker than sidebar)
```

This creates a visual "shelf" that separates sections. The uppercase text + smaller font + darker BG is the VS Code pattern.

---

## 7. Spacing Improvements

### File rows: add more vertical padding

Current: `h720(1)` top/bottom padding.
Target: `h720(3)` top/bottom — gives each row more breathing room.

### Section headers: add bottom margin

Current: headers flow directly into first row.
Target: `h720(2)` margin bottom to separate header from content.

### Commit log entries: consistent height

Current: `COMMIT_ROW_HEIGHT = 30`. The subject text + badge often overflows.
Target: Keep 30 but ensure single-line truncation with ellipsis.

### Toolbar buttons: more horizontal spacing

Current: buttons are tight.
Target: increase `TOOLBAR_BUTTON_HPAD` from 10 to 14.

---

## 8. Diff View Colors

Current diff colors are already good but the backgrounds are too similar to `PANEL_BG`:

```cpp
// Current:
inline Color DIFF_ADD_BG = {30, 42, 35, 255};    // Very close to #1E1E1E
inline Color DIFF_DEL_BG = {55, 30, 32, 255};    // Slightly more visible

// Improved — more visible tint while staying subtle:
inline Color DIFF_ADD_BG = {25, 50, 35, 255};    // #193223 — greener
inline Color DIFF_DEL_BG = {60, 25, 28, 255};    // #3C191C — redder
```

In `diff_renderer.h`, the detail colors should also update:

```cpp
constexpr Color DIFF_ADD_BG    = {20, 45, 30, 255};   // Visible green tint
constexpr Color DIFF_DEL_BG    = {55, 20, 25, 255};   // Visible red tint
constexpr Color GUTTER_ADD_BG  = {20, 60, 30, 255};   // Gutter green (slightly brighter)
constexpr Color GUTTER_DEL_BG  = {70, 20, 25, 255};   // Gutter red (slightly brighter)
```

---

## 9. Menu Bar + Toolbar Visual Continuity

Current menu bar is `#333333`, toolbar is `#37373A`. These are close but distinct — creates a subtle visual seam.

**Unify:** Make toolbar match the sidebar background (`#252526`) when it's inside the sidebar column. This way it reads as part of the sidebar rather than a separate band.

In `toolbar_system.h`, the `topChrome` div should use `theme::SIDEBAR_BG` instead of `theme::TOOLBAR_BG` when `inSidebar` is true. Then add a 1px bottom border in `theme::BORDER` color for separation.

---

## 10. Empty State

Current empty state is fine structurally (centered diamond + text). But:
- Diamond icon at 60,60,60 is too dim — bump to 80,80,80
- "Select a file or commit" should use `FONT_HEADING` (15px) not `FONT_HERO` (18px) — hero is too large for an instructional hint
- Add a subtle keyboard shortcut hint: "j/k to navigate, Enter to view"

---

## Implementation Order (10 changes, sorted by visual impact)

| # | Change | File(s) | Visual Impact |
|---|--------|---------|---------------|
| 1 | Surface hierarchy: darken WINDOW_BG + PANEL_BG to #1E1E1E | `theme.h` | **Massive** — instant depth |
| 2 | Brighten BORDER to #303030 | `theme.h` | **High** — dividers become visible |
| 3 | File row hover: HOVER_BG = #2C2F30, SELECTED_BG = #094771 | `theme.h`, `sidebar_system.h` | **High** — feels interactive |
| 4 | Section headers: darker BG + FONT_CAPTION | `sidebar_system.h` | **Medium** — sections become clear |
| 5 | File label font size: FONT_META -> FONT_CHROME | `sidebar_system.h` | **Medium** — readable names |
| 6 | Diff BG colors: more visible green/red tint | `theme.h`, `diff_renderer.h` | **Medium** — diffs pop |
| 7 | Row spacing: h720(1) -> h720(3) top/bottom | `sidebar_system.h` | **Medium** — breathing room |
| 8 | Toolbar: use SIDEBAR_BG when in sidebar + bottom border | `toolbar_system.h` | **Low** — cleaner chrome |
| 9 | Empty state: tune sizes and add hint | `layout_system.h` | **Low** — only seen once |
| 10 | Toolbar button hpad: 10 -> 14 | `theme.h` | **Low** — minor spacing |

**Changes 1-3 alone will make the app look dramatically better.** They address the "flat slab" problem that makes everything feel unfinished. The rest is incremental polish.

---

## Color Palette Reference (VS Code Dark+ mapped to floatinghotel)

```
Editor background:    #1E1E1E  (WINDOW_BG, PANEL_BG)
Sidebar background:   #252526  (SIDEBAR_BG)
Title/menu bar:       #333333  (menu_colors::BAR_BG)
Toolbar:              #252526  (SIDEBAR_BG when in sidebar)
Section header:       #1F1F1F  (SECTION_HEADER_BG)
Active tab:           #1E1E1E  (matches editor)
Inactive tab:         #2D2D2D
Border:               #303030  (BORDER — bumped from #3A3A3A)
Row hover:            #2C2F30  (HOVER_BG)
Row selected:         #094771  (SELECTED_BG)
Focus ring:           #007ACC  (FOCUS_RING)
Primary button:       #007ACC  (BUTTON_PRIMARY)
Status bar:           #007ACC  (STATUS_BAR_BG)
Status bar detached:  #CC6633  (STATUS_BAR_DETACHED_BG)
Text primary:         #CCCCCC  (TEXT_PRIMARY)
Text secondary:       #808080  (TEXT_SECONDARY)
Text dimmed:          #5A5A5A  (TEXT_TERTIARY)
```

---

## Open Question

Should we tackle this as one big visual pass, or break it into 2 sessions?
- **Session A:** Changes 1-5 (surface hierarchy + interactivity + typography) — the "it doesn't look ugly anymore" pass
- **Session B:** Changes 6-10 (diff colors + spacing + chrome details) — the "it looks polished" pass
