# VS Code UI Parity Plan

Visual comparison of VS Code's Source Control panel vs floatinghotel's current UI.
Reference screenshots taken 2026-02-15.

---

## Commit Area (Items 1–6)

### 1. Move commit input to the top of the changes panel
**Current:** Commit editor is at the bottom of the sidebar, hidden until activated.
**Target:** Commit message input is the first element at the top of the changes panel, always visible above the file list.
**Files:** `src/ecs/sidebar_system.h` (reorder render calls), `src/ecs/components.h` (CommitEditorComponent.isVisible default)

### 2. Full-width blue Commit button
**Current:** Small "Commit" toolbar button in the toolbar row.
**Target:** A prominent, full-width blue button with a checkmark icon, rendered directly below the commit input field.
**Files:** `src/ecs/sidebar_system.h` (new render function), `src/ui/theme.h` (button style)

### 3. Commit button dropdown split
**Current:** Commit button is a single action button. Amend/fixup options are separate buttons.
**Target:** Right edge of the Commit button has a visually separated dropdown chevron that opens a menu with Amend, Fixup, Commit & Push options.
**Files:** `src/ecs/sidebar_system.h`, possibly `src/ui/context_menu.h` or new anchored popup

### 4. Placeholder text shows branch name
**Current:** Separate subject/body labels and inputs.
**Target:** Single text input with placeholder text: `Message (⌘⏎ to commit on "main")` that dynamically includes the current branch name.
**Files:** `src/ecs/sidebar_system.h` (commit editor rendering)

### 5. Single-line commit input
**Current:** Separate subject `text_input` and body `text_area` fields with 50/72 character warnings.
**Target:** One single-line text input for the commit message. Body editing can be deferred to a expand/multiline mode triggered by clicking into the field or pressing enter.
**Files:** `src/ecs/sidebar_system.h`, `src/ecs/components.h`

### 6. Gear/settings icon next to commit input
**Current:** No settings icon near the commit input.
**Target:** Small gear or sparkle icon to the right of the commit message input that opens commit settings (conventional commit prefix, templates, amend mode).
**Files:** `src/ecs/sidebar_system.h`

---

## File List (Items 7–12)

### 7. File-type icons
**Current:** No file icons. Rows show filename + directory + status letter.
**Target:** Small file-type icon before the filename. At minimum: generic file icon, image icon (for .png/.jpg), and language icons (C, C++, header, etc). Could use a sprite atlas or simple colored letter badges as a first pass.
**Files:** `src/ecs/sidebar_system.h` (render_file_row, render_untracked_row), new icon atlas or badge rendering

### 8. Directory as separate dimmed element
**Current:** Directory is appended to the filename with double-space separator: `"main.cpp  src"`.
**Target:** Directory rendered as its own UI element with distinct gray/dimmed color (`TEXT_SECONDARY`), visually separated from the filename. Filename is primary weight, directory is clearly secondary.
**Files:** `src/ecs/sidebar_system.h` (render_file_row label construction)

### 9. Remove row separators between file entries
**Current:** `ROW_SEPARATOR` divider rendered between branch rows. File rows may also produce visual separation via background alternation.
**Target:** No visible divider lines between file rows. Rely on row height and hover state for visual separation.
**Files:** `src/ecs/sidebar_system.h` (remove separator divs in file list rendering)

### 10. Count badge as a rounded pill
**Current:** Section headers show count inline: `"▾ STAGED CHANGES  1"`.
**Target:** Section header text on the left ("Changes"), small rounded pill badge on the right with the count ("4"). The pill has a subtle background and rounded corners.
**Files:** `src/ecs/sidebar_system.h` (render_section_header — split into row with label left + badge right)

### 11. Simpler section header text
**Current:** All-caps with letter-spacing: `"▾ STAGED CHANGES"`, `"▾ UNSTAGED CHANGES"`, `"▾ UNTRACKED"`.
**Target:** Title case or just capitalized first word: `"Changes"`, `"Staged Changes"`. Drop the triangle character and use a proper chevron icon or rotation. Reduce letter-spacing.
**Files:** `src/ecs/sidebar_system.h` (render_section_header), `src/ui/theme.h` (FONT_CAPTION letter-spacing)

### 12. More compact file row height
**Current:** `FILE_ROW_HEIGHT = 34` pixels.
**Target:** ~26-28px row height for a denser, more scannable file list.
**Files:** `src/ui/theme.h` (layout::FILE_ROW_HEIGHT)

---

## Toolbar (Items 13–15)

### 13. Icon-only toolbar buttons
**Current:** Text-labeled buttons: "Commit", "Push", "Pull", "Stash" in the sidebar toolbar; "Refresh", "Stage All", etc. in full-width mode.
**Target:** Small icon-only buttons (no text labels). Icons for: copy/clipboard, search, branch graph, grid/tree view, overflow menu. Tooltips on hover for discoverability.
**Files:** `src/ecs/toolbar_system.h`, icon atlas or sprite rendering

### 14. No distinct toolbar background
**Current:** Toolbar has its own background color `TOOLBAR_BG` (#37373A) distinct from the sidebar.
**Target:** Toolbar buttons sit on the same background as the panel they belong to. Remove the distinct toolbar background band.
**Files:** `src/ui/theme.h` (TOOLBAR_BG usage), `src/ecs/toolbar_system.h` (background color), `src/ecs/layout_system.h` (toolbar rect calculation)

### 15. Smaller, subtler toolbar icons
**Current:** Full button shapes with padding (14px horizontal), roundness, and visible background color.
**Target:** Smaller icon hit targets (~24x24) with no visible background until hovered. Hover shows a subtle highlight. Active/pressed shows a slightly stronger highlight.
**Files:** `src/ecs/toolbar_system.h` (sidebarBtn lambda styling)

---

## Commit Graph (Items 16–20)

### 16. Vertical connecting line between commits
**Current:** Individual purple dots (`GRAPH_DOT`) rendered per commit row. No connecting line.
**Target:** A continuous vertical line (`GRAPH_LINE` color) connecting all dots in the commit log. The line runs through the center of each dot. May require custom render pass or absolute-positioned thin div spanning the full log height.
**Files:** `src/ecs/sidebar_system.h` (render_commit_log_entries, render_commit_row), possibly a new render overlay

### 17. Hollow circle for HEAD commit
**Current:** All commits render the same filled 8x8 circle.
**Target:** The HEAD commit (index 0 or matching `headCommitHash`) renders as a hollow circle (ring/outline only). All other commits remain filled.
**Files:** `src/ecs/sidebar_system.h` (render_commit_row — conditional dot style)

### 18. Author name shown inline in commit rows
**Current:** Commit rows show: dot + subject + badges + hash. No author.
**Target:** Author name appears after the commit message in dimmed gray text, on the same line. Format: `"add map canvas zoom, pan and rotati...  Gabe Ochoa"`.
**Files:** `src/ecs/sidebar_system.h` (render_commit_row — add author label), `src/ecs/components.h` (CommitEntry already has `author` field)

### 19. Badge icons inside branch pills
**Current:** Branch/tag badges show text only: `"main"`, `"origin/main"`, `"HEAD"`.
**Target:** Badges include a small icon prefix: branch icon for local branches, cloud/remote icon for remote branches, tag icon for tags. Could be Unicode characters or small sprite icons.
**Files:** `src/ecs/sidebar_system.h` (commit badge rendering section)

### 20. Remove hash from graph rows
**Current:** Each commit row shows the 7-char hash right-aligned.
**Target:** Remove the hash from the default graph view. Hash is available on click/selection or in commit detail view. Frees up horizontal space for longer commit messages.
**Files:** `src/ecs/sidebar_system.h` (render_commit_row — remove or hide hash div)

---

## Section Headers (Items 21–22)

### 21. Collapsible sections with proper chevron
**Current:** Section headers use a text triangle character `"▾"` that doesn't animate or change on collapse.
**Target:** Use a proper chevron icon (▸/▾ or >) that rotates 90° when collapsed. Sections should be collapsible on click, remembering their state.
**Files:** `src/ecs/sidebar_system.h` (render_section_header), `src/ecs/components.h` (add collapsed state per section)

### 22. Refined section header styling
**Current:** All-caps, 0.5 letter-spacing, `SECTION_HEADER_BG` (#202021), `FONT_CAPTION` (12px).
**Target:** Match VS Code's style: slightly larger text, less letter-spacing, no distinct background color (or very subtle). "CHANGES" and "GRAPH" styling from the screenshots.
**Files:** `src/ui/theme.h` (SECTION_HEADER_BG, FONT_CAPTION), `src/ecs/sidebar_system.h`

---

## General Styling (Items 23–27)

### 23. Remove or soften sidebar border
**Current:** Sidebar has `with_border_right(theme::BORDER)` — a visible right edge.
**Target:** Remove the hard border or make it much more subtle (lower alpha, thinner). VS Code's panels blend more seamlessly.
**Files:** `src/ecs/sidebar_system.h` (sidebarRoot config)

### 24. Tighter padding throughout
**Current:** File rows have 10px left padding, 4px right, 4px gap. Section headers have 10px horizontal padding, 7px top, 5px bottom.
**Target:** Reduce to ~8px left padding, 2-3px gaps. Tighter vertical padding on section headers (4-5px). Overall denser layout.
**Files:** `src/ecs/sidebar_system.h` (all padding values), `src/ui/theme.h` (layout constants)

### 25. Normalize font sizes
**Current:** Multiple font size tiers: FONT_BODY=28, FONT_META=24, FONT_CAPTION=12, FONT_CHROME=26. These are h720 reference values but create visible size jumps.
**Target:** More uniform sizing. Filenames, directories, and status letters should be close to the same size. Reduce the gap between body and meta sizes.
**Files:** `src/ui/theme.h` (layout font size constants)

### 26. Subtler hover state
**Current:** `HOVER_BG` is #323234 — a noticeable jump from `SIDEBAR_BG` (#252526).
**Target:** Lighter hover: ~5-8 alpha units above the base, e.g., #2A2A2C or use a white overlay at 4-6% opacity.
**Files:** `src/ui/theme.h` (HOVER_BG value)

### 27. Replace Changes/Refs toggle tabs with icon toolbar
**Current:** Two pill-shaped toggle buttons at the top of the sidebar: "Changes" and "Refs".
**Target:** Replace with small icon buttons in the toolbar area (like VS Code's top icon row). The view mode switch is an icon toggle, not a prominent tab bar.
**Files:** `src/ecs/sidebar_system.h` (render_sidebar_mode_tabs), `src/ecs/toolbar_system.h`

---

## Implementation Priority

### Phase A — Commit Area (highest visual impact)
Items 1, 2, 3, 4, 5, 6

### Phase B — File List Polish
Items 7, 8, 9, 10, 11, 12

### Phase C — Commit Graph
Items 16, 17, 18, 19, 20

### Phase D — Toolbar & Headers
Items 13, 14, 15, 21, 22

### Phase E — General Styling
Items 23, 24, 25, 26, 27
