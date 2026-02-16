# Competitive Multi-Audit: Git Change Review Tools (2026-02-15)

**Purpose:** Evaluate competing tools for floatinghotel's primary use case: *reviewing current changes and copying code to paste into terminal or chat.*

**Tools audited:** 5 tools, 11 reference screenshots
**Auditors:** Apple HIG, Google Material Design 3, Sun Java L&F (1999), Muskox Design Pet Peeves

**User persona:** Developer who needs to:
1. See what changed (staged, unstaged, untracked)
2. Review diffs quickly
3. Copy code blocks to paste into terminal or chat app for comments
4. Does NOT need to edit code in-app

---

## Tools Analyzed

### 1. gitui (Terminal, Rust)
**File:** `gitui-main.png`
**What it is:** Terminal-based git UI with panel layout

### 2. GitButler (GUI, Tauri/Svelte)
**Files:** `gitbutler-app-preview.png`, `gitbutler-cli-preview.png`
**What it is:** Modern web-tech GUI focused on virtual branches and stacked PRs

### 3. Gittyup (GUI, Qt/C++)
**File:** `gittyup-dark.png`
**What it is:** Traditional cross-platform desktop Git GUI

### 4. delta (Terminal diff pager)
**Files:** `delta-diff-highlight.png`, `delta-line-numbers.png`, `delta-navigate.png`, `delta-blame.png`
**What it is:** Beautiful terminal diff renderer with syntax highlighting

### 5. VS Code Git Graph (Extension)
**Files:** `vscode-git-graph-demo.gif`, `vscode-git-graph.png`
**What it is:** VS Code extension for commit graph visualization

---

## Multi-Audit Results

### Agreed by 4/4 auditors

#### 1. Delta's Inline Diff Highlighting Is Best-in-Class
**Verdict:** STEAL THIS

**What they did well:**
- Sub-line change highlighting (within-line diff, not just line-level) makes it instantly clear WHAT changed on a line, not just THAT it changed
- Dual line number columns (old:new) with a visible separator — no guessing which side you're reading
- Function/class navigation headers (yellow-boxed labels like `class AbstractNameDefinition(object):`) provide context anchors so you know WHERE you are in the file
- Commit metadata at top (hash, author, date, message) gives full context without switching views
- Syntax highlighting preserved in diff — code remains readable, not just colored red/green blobs
- File path as a clickable/visible header above each file's diff section

**What they did poorly:**
- No copy affordance — terminal-only, requires manual text selection
- No line-range selection UI — can't click to select "lines 135-145" for copying
- Dark backgrounds only; no light theme option visible
- No panel structure — pure scroll-based; for multi-file diffs you scroll through everything linearly

**Relevance to floatinghotel:** The diff rendering quality is the gold standard. The sub-line highlighting, dual line numbers, and function navigation headers should all be adopted. But delta completely lacks the "copy this chunk" UX that floatinghotel needs.

---

#### 2. gitui's Panel Layout Is Clean But Lacks Diff Copy Affordance
**Verdict:** LEARN FROM, DON'T COPY

**What they did well:**
- Three-panel layout: Unstaged Changes (top-left), Staged Changes (bottom-left), Diff (right) — all visible simultaneously
- Keyboard shortcut bar at the bottom is excellent — every available action visible at a glance with key binding
- Status indicators using color AND character (M = modified, + = added, - = deleted) — redundant coding, not color-only
- File tree structure in the changes panel (src/components/textinput.rs shown with folder hierarchy)
- Tab-based top navigation (Status, Log, Stashing, Stashes) with number shortcuts [1][2][3][4]
- Branch name visible in panel title: "Unstaged Changes [w] - {master}"

**What they did poorly:**
- Diff panel is empty in the screenshot — no diff preview without file selection
- No visual affordance for copying diff content
- Monospace everything — no type hierarchy, everything reads at the same visual weight
- Panel borders are basic box-drawing characters — functional but visually flat
- No hover or selection states visible — pure keyboard-driven, but a GUI needs both
- Color palette is limited (basic ANSI colors) — green for added, red for deleted, yellow for modified, but no tonal depth
- The "more [.]" truncation in the shortcut bar suggests too many actions crammed into one bar

**Relevance to floatinghotel:** The three-panel layout is the right starting structure (changes list / staged list / diff preview). The keyboard shortcut bar is excellent for discoverability. But the visual design is purely terminal-functional — floatinghotel should have the same information density with much better visual hierarchy.

---

### Agreed by 3/4 auditors

#### 3. GitButler's File-Level Overview Is Excellent for Quick Triage
**Verdict:** STEAL THE FILE LIST UX

**What they did well:**
- File list with file type icons (TS, Svelte icons) makes it instantly scannable
- Path shown as `filename` + lighter `directory` — clear two-level hierarchy
- Count badges ("Unassigned 7", "Staged 7 +10 -239") give immediate scope
- Stat chips (+10 -239) in the staged header — you know the magnitude of changes at a glance
- Action buttons per file (the small icons on the right of each file row)
- Clean separation between "what's changed" (left) and "what to do about it" (right)
- "Start a commit..." prompt is prominent and contextual

**What they did poorly:**
- Three-column layout may be too wide for a focused review tool — center column (branch timeline) is interesting but not relevant to "review current changes"
- The branch/PR integration (Create PR, Push buttons) adds complexity for the simple review use case
- File type icons are tiny and may not be distinguishable at smaller sizes
- The "Unassigned" terminology is jargon specific to GitButler's virtual branch model
- Light theme only shown — needs dark mode for terminal-adjacent users
- No diff preview visible — you'd need to click a file to see changes, adding a step

**Relevance to floatinghotel:** The file list design is the best of the group. The filename + directory + stats + file-type-icon pattern is worth adopting. But strip the branch management complexity — floatinghotel's review use case is simpler.

---

#### 4. Gittyup's Traditional Layout Is Feature-Complete But Visually Dense
**Verdict:** LEARN FROM THE STRUCTURE, IMPROVE THE DENSITY

**What they did well:**
- Full traditional Git GUI layout: menu bar, repo switcher, commit log with graph, diff viewer, file tree
- Blame/Diff tab toggle on the diff panel — useful for understanding WHY a change was made
- Commit metadata (author, email, ID, parents, date) all visible at once
- Line numbers in diff view (both old and new columns: "31 31", "33 33")
- File tree in committed files panel with status badges (M indicators)
- Branch/tag badges inline with commits (colored pills: "origin/OpenWith", "gittyup_v1.0.0")
- "Collapse all" option for the file tree — reduces noise when reviewing large changesets
- Standard platform menu bar (File, Edit, View, Repository, Remote, Branch, etc.)

**What they did poorly:**
- Extremely dense — every pixel is used, no breathing room
- Font sizes appear small throughout, especially in the diff view and commit log
- The commit graph (colored lines) in the left panel creates visual noise that competes with commit messages
- Dark theme has low contrast in several areas (especially branch badge text)
- No visible copy affordance for code — you'd need to manually select text
- The diff view shows only a few lines of context — no full-file-with-highlights option
- Toolbar icons are small and lack text labels — violates "text-first actions" principle
- The layout tries to show everything at once, which creates the "information overload" problem

**Relevance to floatinghotel:** This is closest to what floatinghotel is building structurally. The lesson is: don't try to show everything at once. For the "review changes" use case, prioritize the diff and file list. The commit log and branch management can be secondary (toggleable, not always-visible).

---

### Agreed by 2/4 auditors

#### 5. Delta's Side-by-Side View Has Superior Readability
**Verdict:** IMPLEMENT AS A TOGGLE

From `delta-line-numbers.png`:
- Side-by-side diff with line numbers on each side
- File path as a header with clear visual separation
- Function headers as navigation anchors
- Green/red backgrounds subtle enough to not overwhelm, strong enough to see
- The contrast between changed and unchanged lines is well-calibrated

The side-by-side mode is strictly better for reviewing changes when horizontal space allows. But it doubles the required width. **Implement both inline and side-by-side as a toggle.**

#### 6. GitButler CLI's Structured Status Output
**Verdict:** INTERESTING REFERENCE FOR STATUS DISPLAY

From `gitbutler-cli-preview.png`:
- Tree-structured output: unstaged changes → stacked branches → upstream
- Short hash + status + filename on each line
- Branch names in brackets with clear nesting
- Upstream info at the bottom with "1 new commits" count
- Compact but complete — everything you need to know in one screen

This is how the status information should feel even in a GUI — structured, scannable, with clear hierarchy from "what's changed locally" at top to "what's happening upstream" at bottom.

---

## Per-Auditor Detailed Notes

### Apple HIG Auditor

| # | Finding | Severity | Affected Tools |
|---|---------|----------|----------------|
| 1 | No tool provides a visible "copy this code" affordance. All require manual text selection. | Critical | All 5 |
| 2 | gitui and delta provide no visual distinction between "selected" and "not selected" states for diff lines | Major | gitui, delta |
| 3 | Gittyup's toolbar icons lack text labels — ambiguous symbols | Major | Gittyup |
| 4 | GitButler's file type icons are too small to distinguish at 1x | Minor | GitButler |
| 5 | No tool shows a "copy" button on diff hunks or file sections | Critical | All 5 |
| 6 | Delta's function navigation headers are the only tool providing spatial context within long diffs | Major positive | delta |
| 7 | gitui's keyboard shortcut bar is the only tool making all available actions discoverable at once | Major positive | gitui |

### Google Material Design 3 Auditor

| # | Finding | Severity | Affected Tools |
|---|---------|----------|----------------|
| 1 | No tool implements proper tonal elevation to distinguish panels from background | Major | All except GitButler |
| 2 | gitui uses color as sole differentiator for file status (red=deleted, green=added) | Major | gitui |
| 3 | Gittyup's commit graph uses 5+ colors with no legend — cognitive overload | Major | Gittyup |
| 4 | Delta's sub-line highlighting (brighter highlight within a changed line) is the only tool providing word-level change detection | Major positive | delta |
| 5 | GitButler's count badges (+10 -239) use good hierarchy: bold count + colored +/- indicators | Minor positive | GitButler |
| 6 | No tool provides a loading/progress state for diff generation | Minor | All 5 |
| 7 | gitui's three-panel layout follows the "overview + detail" pattern correctly | Minor positive | gitui |

### Sun Java L&F Auditor

| # | Finding | Severity | Affected Tools |
|---|---------|----------|----------------|
| 1 | Only Gittyup implements a standard platform menu bar | Critical | gitui, GitButler, delta, VS Code Git Graph |
| 2 | No tool provides standard Edit > Copy or right-click > Copy Code for diff content | Critical | All 5 |
| 3 | Gittyup's Blame/Diff tab toggle uses standard tabbed pane pattern | Minor positive | Gittyup |
| 4 | gitui's panel titles include both the panel name AND the keyboard shortcut — excellent label convention | Minor positive | gitui |
| 5 | Delta provides no windowing, scrollbar, or navigation controls — pure stream output | Major | delta |
| 6 | VS Code Git Graph inherits VS Code's standard chrome, which is appropriate | Neutral | VS Code Git Graph |

### Muskox Design Pet Peeves Auditor

| # | Finding | Severity | Affected Tools |
|---|---------|----------|----------------|
| 1 | **Container overuse:** Gittyup nests panels within panels within panels — commit log inside repo panel inside main layout | Major (1.3) | Gittyup |
| 2 | **Lack of clear hierarchy:** gitui treats all panels equally — Unstaged, Staged, Diff are visually identical. The diff panel should be dominant. | Critical (1.5) | gitui |
| 3 | **Too many type sizes:** Gittyup uses at least 6 distinct type sizes on one screen | Major (2.1) | Gittyup |
| 4 | **Inconsistent visual language:** Git graph colors in Gittyup use random palette with no semantic meaning | Critical (1.7) | Gittyup |
| 5 | **Content bandaids:** None of the tools explain what you can DO with the diff. "Click to copy" or "Select lines to share" would be self-evident. | Critical (3.1) | All 5 |
| 6 | **Non-intuitive core functionality:** Copying code from a diff is THE core action for the review use case, yet NO tool makes this easy | Critical (3.10) | All 5 |
| 7 | **Missing system feedback:** No tool shows feedback after copying (no toast, no flash, no indication the copy succeeded) | Critical (3.12) | All 5 |
| 8 | **Slight misalignments:** Gittyup's file tree badges (M indicators) are not baseline-aligned with filenames | Major (1.2) | Gittyup |

---

## Consensus Findings

### What ALL tools do well:
1. **Three-zone layout** — file list + diff view is universal. This pattern works.
2. **Color coding for change type** — green=add, red=delete is universally understood.
3. **Line numbers in diffs** — every tool with diff display includes line numbers.
4. **File path as header** — every tool shows which file a diff belongs to.
5. **Monospace font for code** — all tools correctly use monospace for diff content.

### What ALL tools fail at:
1. **No "copy this code" affordance** — EVERY tool requires manual text selection to copy. No copy button, no click-to-select-hunk, no "copy as markdown" option. This is THE gap floatinghotel should fill.
2. **No copy feedback** — Even after manual copy, no confirmation that it worked.
3. **No paste-friendly formatting** — When you manually copy, you get raw text. No option to copy with line numbers, with diff markers, or as a code block for chat.
4. **No line-range selection** — Can't click line 10, shift-click line 25, and get that range copied.
5. **No share/export workflow** — No "copy this diff for review" or "share this hunk" feature.

### Best-of-breed features to adopt:

| Feature | Best Example | Adopt? |
|---------|-------------|--------|
| Sub-line change highlighting | delta | YES — critical for review |
| Dual line numbers (old:new) | delta, Gittyup | YES |
| Function/class navigation headers | delta | YES — helps orientation in large files |
| File list with type icons + stats | GitButler | YES |
| Keyboard shortcut bar | gitui | YES — critical for discoverability |
| Three-panel layout | gitui | YES — changes list / staged / diff |
| Blame/Diff tab toggle | Gittyup | MAYBE — nice for context but not P0 |
| Side-by-side diff toggle | delta | YES — as a toggle option |
| Commit metadata header | delta | YES — context for what you're reviewing |

---

## The Gap: Copy-to-Share Workflow

This is the single biggest opportunity. Every tool analyzed is built for DOING git operations (stage, commit, push). None are built for REVIEWING and SHARING changes with others.

### Proposed "Copy Code" UX for floatinghotel:

**Hunk-level copy:**
- Each diff hunk gets a small copy icon (top-right of the hunk header)
- Click: copies the changed lines (additions only) as plain code
- Shift+click: copies the full hunk with diff markers (+/-)
- Toast: "Copied 12 lines to clipboard"

**Line-range selection:**
- Click a line number to start selection
- Shift+click another line number to extend
- Selected range gets a blue highlight (like text selection but line-granular)
- A floating "Copy" button appears near the selection
- Keyboard shortcut (Cmd+C) copies selected range

**File-level copy:**
- In the file list, each file row has a "copy diff" icon
- Copies the entire file's diff in a paste-friendly format

**Format options (in a small dropdown on the copy button):**
- "Copy code" — just the code, no diff markers
- "Copy diff" — with +/- markers and line numbers
- "Copy as markdown" — wrapped in triple-backtick code fence with language
- "Copy file path" — just the path (for referencing in chat)

**Paste-friendly output examples:**

"Copy code" output:
```
ssize_t ret;
ret = read(fd,&c,1);
if (ret == -1) {
```

"Copy diff" output:
```
--- a/redis-cli.c
+++ b/redis-cli.c
@@ -135,8 +135,10 @@
+            ssize_t ret;
-            if (read(fd,&c,1) == -1) {
+            ret = read(fd,&c,1);
+            if (ret == -1) {
```

"Copy as markdown" output:
````
```c
// redis-cli.c:138-141
ssize_t ret;
ret = read(fd,&c,1);
if (ret == -1) {
```
````

---

## Muskox Quick Tally: How the Tools Score

| # | Category | gitui | GitButler | Gittyup | delta |
|---|----------|-------|-----------|---------|-------|
| 1 | Corner radii | 0 (N/A terminal) | 2 | 1 | 0 (N/A) |
| 2 | Alignment | 1 | 2 | 1 | 2 |
| 3 | Container nesting | 1 | 2 | 0 | 2 (N/A) |
| 4 | Visual hierarchy | 0 | 2 | 1 | 2 |
| 5 | Asset quality | 1 | 2 | 1 | 2 |
| 6 | Visual consistency | 1 | 2 | 1 | 2 |
| 7 | Typography | 0 | 2 | 1 | 2 |
| 8 | Copy quality | 1 | 1 | 1 | 1 |
| 9 | Error handling | 0 | 1 | 1 | 0 |
| 10 | Interactive states | 0 | 1 | 1 | 0 |
| 11 | Feedback | 0 | 1 | 1 | 0 |
| 12 | Undo / reversibility | 0 | 1 | 1 | 0 (N/A) |
| 13 | Loading / performance | 2 | 1 | 1 | 2 |
| 14 | Motion quality | 0 (N/A) | 1 | 0 | 0 (N/A) |
| 15 | Scroll performance | 1 | 1 | 1 | 2 |
| | **Total** | **7/30** | **22/30** | **13/30** | **17/30** |
| | **Rating** | Critical | Good | Needs work | Acceptable |

---

## Design Recommendations for floatinghotel

### Priority 1: The Diff View (Core Value)

Adopt delta's diff rendering quality:
- Sub-line change highlighting (word-level diffs within changed lines)
- Dual line number columns (old line : new line)
- Function/class headers as navigation anchors
- Syntax highlighting preserved in diff context
- File path header above each file section
- Toggle between inline and side-by-side modes

### Priority 2: The Copy-to-Share Workflow (The Gap)

This is the feature no competitor has. Build it as a first-class workflow:
- Hunk-level copy buttons (visible, not hidden)
- Line-range selection by clicking line numbers
- Multiple copy formats (code only, diff, markdown code block)
- Toast confirmation on copy
- Keyboard shortcut (Cmd+C on selection)

### Priority 3: The File Overview

Adopt GitButler's file list pattern:
- Filename bold + directory dimmed
- File type indicator (file extension or icon)
- Change stats (+lines / -lines) per file
- Status badge (M/A/D/R) with redundant coding (color + character)
- Count summary in section header ("Unstaged 3", "Staged 2 +45 -12")

### Priority 4: Layout and Navigation

Adopt gitui's three-panel structure with improvements:
- Left panel: file list (unstaged + staged sections, collapsible)
- Right panel: diff view (dominant, takes most width)
- Bottom bar: keyboard shortcuts visible (like gitui)
- Status bar: branch + dirty state + ahead/behind
- The diff panel should be visually dominant (largest, highest contrast)

### Priority 5: Information Hierarchy

Apply Muskox hierarchy principles:
- Diff view is the hero — it gets the most space and the highest contrast
- File list is supporting — scannable, compact, but not competing
- Commit log / branch info is contextual — available but not always-visible
- Copy actions are prominent — visible buttons, not hidden in menus

### What to AVOID (learned from competitors):

1. **Don't show the commit graph by default** (Gittyup) — it's visually noisy and irrelevant to "review current changes"
2. **Don't use color as the sole status indicator** (gitui) — always pair with text/icon
3. **Don't try to show everything at once** (Gittyup) — progressive disclosure
4. **Don't make copy a hidden/manual operation** (all tools) — make it first-class
5. **Don't use too many type sizes** (Gittyup) — stick to 4-5 from the theme scale
6. **Don't neglect the keyboard shortcut bar** — it's the fastest way to train users

---

## Summary

| Tool | Overall | Key Takeaway |
|------|---------|-------------|
| gitui | 7/30 | Great layout structure, terrible visual design |
| GitButler | 22/30 | Best file list UX, too much branch management complexity |
| Gittyup | 13/30 | Feature-complete but visually overwhelming |
| delta | 17/30 | Best diff rendering by far, no GUI features |
| VS Code Git Graph | N/A | Commit visualization only, not relevant to review UX |

**The winning formula for floatinghotel:**
- Delta's diff rendering quality
- GitButler's file list design
- gitui's panel layout and keyboard discoverability
- A novel copy-to-share workflow that no competitor offers
- Applied through the design guidelines (clear hierarchy, 44px targets, redundant color coding, proper contrast, text-first actions)
