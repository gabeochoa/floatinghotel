# Change Review & Copy UX Design

Based on competitive audit of 5 tools (gitui, GitButler, Gittyup, delta, VS Code Git Graph).
See `resources/inspiration/multi-audit-2026-02-15.md` for full analysis.

---

## The Insight

Every existing git GUI is built for DOING git operations. None are built for the workflow: "let me see what changed so I can review it and share snippets with someone." Copying code from a diff is manual text selection in every tool tested. This is floatinghotel's biggest opportunity.

## Core Use Case

> "I need to see what changed, review the diffs, and copy code snippets to paste into my terminal or chat app for comments."

This means the app is primarily a **reader**, not a **writer**. The diff view is the hero. Copy is the primary action.

---

## Layout

```
+--[Menu Bar]------------------------------------------+
|  File  Edit  View  Repository  Help                   |
+--[Toolbar]-------------------------------------------+
|  [Refresh] [Stage All] [Unstage All]  |  branch: main |
+------------+-----------------------------------------+
| Files      | Diff View (HERO)                        |
|------------|                                          |
| Unstaged 3 |  src/main.cpp                           |
|  M main.cpp|  @@ -135,8 +135,10 @@  [Copy Hunk]     |
|  M theme.h |   135 : 135 |     while(1) {            |
|  + new.txt |   136 : 136 |         char c;           |
|------------|  +     : 138 |         ssize_t ret;      |
| Staged 1   |  -139 :     |  if (read(...) == -1) {   |
|  M foo.cpp |  +     : 140 |  ret = read(fd,&c,1);    |
|            |  +     : 141 |  if (ret == -1) {         |
|            |                                          |
|            |  [Status: 3 files, +45 -12 lines]        |
+------------+-----------------------------------------+
|  [Shortcuts: s=stage  u=unstage  c=commit  ⌘C=copy]  |
+------------------------------------------------------+
```

Key layout decisions:
- **Diff view gets ~70% of width** — it's the hero, not an equal panel
- **File list is compact** — scannable but not dominant
- **Keyboard shortcut bar** at bottom (learned from gitui)
- **Copy actions are visible** in the diff view, not hidden in menus

---

## Diff Rendering (Steal from delta)

### Sub-line change highlighting
Within a changed line, highlight the specific characters that changed. Red background for removed characters, green for added. The unchanged portions of the line stay at normal contrast.

### Dual line numbers
Two columns: old line number on the left, new line number on the right, with a separator. Added lines show only the new number. Deleted lines show only the old number. Context lines show both.

### Function/class navigation headers
When a hunk is inside a function or class, show the enclosing scope as a dimmed header above the hunk:

```
  class MyClass:                          (dimmed, navigation context)
  ─────────────────────────────────────
  @@ -42,6 +42,8 @@                      (hunk header, blue)
     42 : 42 |     def process(self):     (context, normal)
  +     : 43 |         validate()         (added, green bg)
     43 : 44 |         return result      (context, normal)
```

### File path header
Above each file's diff section, show the full path with status badge and change stats:

```
  M  src/main.cpp  (+12 -3)              [Copy File Diff] [Copy Path]
```

---

## Copy-to-Share Workflow (The Novel Feature)

### Hunk-level copy
Each hunk header shows a copy icon on the right:
```
  @@ -135,8 +135,10 @@  cliReadLine         📋 ▾
```
- **Click copy icon:** copies changed lines (additions only) as plain code
- **Dropdown (▾):** format options

### Line-range selection
- Click a line number → selects that line (blue highlight)
- Shift+click another line number → selects the range
- A floating action bar appears near the selection: `[Copy Code] [Copy Diff] [Copy Markdown]`
- Cmd+C copies in the last-used format
- Escape clears selection

### Copy format options

| Format | What it produces | When to use |
|--------|-----------------|-------------|
| Copy code | Just the code, no markers | Pasting into terminal to run/test |
| Copy diff | With +/- markers and line numbers | Sharing in a PR comment or code review |
| Copy as markdown | Code fence with language and file:line ref | Pasting into chat (Slack, Discord, etc.) |
| Copy file path | Just `src/main.cpp` | Quick reference |

### Toast feedback
After any copy action:
```
  ✓ Copied 8 lines as markdown
```
Toast appears bottom-center, auto-dismisses after 2 seconds.

---

## File List Design (Steal from GitButler)

### File row format
```
  [M]  main.cpp   src/          +12 -3
  [A]  new.txt    src/tests/    +45
  [D]  old.txt    lib/          -28
```

- Status badge: colored character in a small pill (M=yellow, A=green, D=red, R=blue)
- Filename bold, directory dimmed
- Change stats right-aligned

### Section headers
```
  ▾ Unstaged Changes (3)                 [Stage All]
  ▾ Staged Changes (1)                   [Unstage All]
  ▸ Untracked Files (2)                  [Add All]
```

- Collapsible sections with count badges
- Batch action button right-aligned in header

### Selection
- Click a file → shows its diff in the main panel
- Selected file row gets a blue highlight background
- Cmd+click for multi-select (for batch staging)

---

## Keyboard Shortcuts

### Bottom bar (always visible)
```
  s stage  u unstage  d discard  c commit  ⌘C copy  / search  ? help
```

### Full shortcut map
| Key | Action |
|-----|--------|
| j/k or ↑/↓ | Navigate file list |
| Enter | View diff for selected file |
| s | Stage selected file |
| u | Unstage selected file |
| d | Discard changes (with confirmation) |
| c | Open commit editor |
| Tab | Switch focus between file list and diff view |
| ⌘C | Copy selected lines / hunk |
| ⌘⇧C | Copy as markdown |
| n/p | Next/previous hunk in diff |
| f | Next file |
| F | Previous file |
| / | Search in diff |
| r | Refresh |
| ? | Show all shortcuts |

---

## Visual Design Notes

### From the design guidelines:
- Minimum 44x44px click targets for copy buttons and file rows
- 4.5:1 contrast ratio for all text
- Color is never the sole indicator (status badges use color + character)
- Text-first actions (copy buttons have labels, not just icons)
- Consistent spacing on 4/8px grid

### From the muskox audit:
- Diff view is the clear visual dominant — largest panel, highest contrast
- File list is supporting — compact, lower visual weight
- No container nesting beyond necessary — use spacing and hierarchy, not nested boxes
- Maximum 4-5 type sizes on screen
- Copy buttons are prominent, not hidden

### Type scale (from theme.h, already defined):
- FONT_HERO (18px): Empty state titles
- FONT_HEADING (15px): Diff file headers, section titles
- FONT_BODY (14px): File names, commit detail
- FONT_CHROME (13px): Menu bar, toolbar, shortcuts
- FONT_CODE (13px): Diff code content, line numbers
- FONT_META (12px): Diff stats, status bar
- FONT_CAPTION (11px): Section headers, badges

---

## What to Build (Ordered)

1. **Line-range selection in diff view** — click line numbers to select ranges
2. **Copy button on hunk headers** — visible, accessible, with format dropdown
3. **Copy keyboard shortcut (⌘C)** — copies selected range or current hunk
4. **Toast feedback on copy** — "Copied N lines"
5. **Sub-line change highlighting** — word-level diffs within changed lines
6. **Dual line number columns** — old:new with separator
7. **Function/class navigation headers** — context for where you are in the file
8. **File list with stats** — filename + directory + change counts
9. **Keyboard shortcut bar** — bottom of screen, always visible
10. **Side-by-side diff toggle** — alternative to inline view

---

## Open Questions

1. **Copy format default:** Should the default copy be "code only" or "diff with markers"? The use case suggests "code only" for terminal pasting, "markdown" for chat. Maybe remember last-used format?
2. **Multi-file copy:** Should you be able to select multiple files and copy all their diffs at once? Useful for sharing a full changeset in chat.
3. **Diff generation:** Currently using `git diff`. Should we also support `git diff --cached` (staged), `git diff HEAD` (all), and `git show <commit>` (specific commit)?
4. **Search in diff:** How should Cmd+F / `/` search work? Highlight matches across all file diffs, or only the currently visible file?
