# FloatingHotel Unified Design Language — Quiet Reading Cockpit

This document defines the single design system governing every FloatingHotel surface: review cockpit (unified and split diffs), commit detail, full-file source/edit view, sidebar (Changes, Refs, Agent), feedback basket, modals, overlays, welcome state, and both Dark and Light themes.

## 1. Aesthetic Intent & Product Domain

- **Domain:** An architectural proofing desk for reading code diffs, judging changes, writing crisp review feedback, and collaborating with an inline ACP coding agent.
- **Feel:** Calm, surgical, reading-first. Code is the protagonist; chrome recedes into quiet layered surfaces separated by 1px hairlines.
- **Anti-defaults:** Reject generic "dark dev tool" neon blue banners, heavy filled pill soup, inconsistent focus rings, redundant stacked toolbars, and raw debug-panel layouts in new features (such as the Agent sidebar).
- **Signature:** High-density, zero-jitter native reading canvas where diffs read like printed pages—hairline cards, calm slate hunk dividers, inline review marks, and an integrated companion sidebar that shares the exact same surface grammar.

## 2. Committed Depth & Surface Hierarchy

One depth strategy everywhere: **borders-only + whisper-quiet tonal elevation**. No dramatic drop shadows, no bright colored toolbars, no random white borders.

| Surface Role | Dark (`theme.h`) | Light (`theme.h`) | Usage |
|---|---|---|---|
| Canvas (`WINDOW_BG`, `PANEL_BG`) | `#15171B` | `#F8F9FB` | Reading viewport, code canvas |
| Rail (`SIDEBAR_BG`, `TOOLBAR_BG`, `STATUS_BAR_BG`) | `#1B1D22` | `#F1F3F6` | Sidebar, tab strip, status bar, feedback dock |
| Card / Raised (`INPUT_BG`, `BUTTON_SECONDARY`, file card header) | `#202329` | `#FFFFFF` | Cards, secondary buttons, inputs, segmented track |
| Active / Elevated (`SELECTED_BG`, `SEGMENT_SELECTED_BG`) | `#28313E` / `#333740` | `#E3ECF8` / `#FFFFFF` | Selected rows, active segment, hover lift |
| Hairline Border (`BORDER`, `ROW_SEPARATOR`) | `#2C2F36` | `#DCDFE6` | 1px structural separation; findable, never loud |

## 3. Color Restraint & Semantic Tokens

- **One Calm Accent:**
  - **Primary CTA (max 1 per viewport):** Dark uses soft ice blue `#C1D6EF` with deep ink `#182231` (hover `#D4E4F7`). Light uses deep slate-blue `#1E4E8C` with white ink `#FFFFFF` (hover `#184075`). Both themes feel calm and editorial—no electric `#007ACC` cyan.
  - **Accent Text / Active Indicator (`TEXT_ACCENT`, `SELECTED_ACCENT`, `FOCUS_RING`):** Dark `#B9CFEF`, Light `#1E4E8C`. Focus rings use the dedicated calm slate-ice `FOCUS_RING` token (never stark pure white on dark).
- **Diff & Hunk Hierarchy:**
  - Code text stays primary foreground with subtle syntax tokens; add/del uses soft wash + 2px left edge bar + muted gutter.
  - **Hunk Header (`DIFF_HUNK_BG`, `DIFF_HUNK_HEADER`):** Calm slate wash (`#1D252E` dark / `#EBF1F8` light) with readable blue-slate mono text (`#9CAFC6` dark / `#3D5B80` light).
  - **Focused/Cursor Hunk (`HUNK_CURSOR_BG`):** Subtle tonal lift (`#253243` dark / `#DCE8F6` light) + 2px accent left bar—NEVER a saturated cobalt banner that ruins text contrast.
- **Semantic Status (WCAG AA >= 4.5:1 on actual surface):**
  - Success / Added / Approved / Ready: Dark `#8CD1A4`, Light `#1F7A43`.
  - Destructive / Deleted: Dark `#F08A8A` on dark button bg (readable >= 5:1, never dim dark-red on charcoal), Light `#C0322E`.
  - Warning / Modified / Permission Pending: Dark `#E3B341`, Light `#9A6700`.

## 4. Typography & Spatial Grid

- **Grid:** 4px base rhythm (`4 / 8 / 12 / 16 / 20 / 24`). No off-grid gaps (`5px`, `7px`, `13px`, `19px`).
- **3 Strict Tiers (`preload.cpp`):**
  - `12px` Caption / Metadata (`mono` for SHAs, `+N -M`, ages, shortcuts, file-type tags; `sans` for secondary captions).
  - `14px` Body / Controls (`Roboto-Regular` for labels, prose, feedback comments—never monospace for prose; `Roboto-Bold` for repo title, primary CTAs, active headings; `JetBrainsMono` for code).
  - `16px` Section / Modal Title (`Roboto-Bold`).
- **Corner Radius Scale:**
  - `5px` (`RADIUS_CONTROL`) — buttons, inputs, segmented pills, selected rows, keycaps.
  - `7px` (`RADIUS_CARD`) — diff file cards, feedback cards, permission cards, welcome card.
  - `8px` (`RADIUS_MODAL`) — modals, command palette, toasts.
  - Pill (`0.5f` roundness) — single-line status/HEAD badges only.
- **Padding & Clearance Rules:**
  - Every text input / text area has >= `8px` horizontal and `6px` vertical inner padding; cursor and placeholder never touch borders.
  - Every scrollable list row reserves `>= 14px` right clearance so right-aligned badges/ages (`3h`, `+4`, viewed dot) never collide with the scrollbar thumb.
  - Every selectable modal/list row has `10px` horizontal inner padding so text never starts flush at x=0 of its highlight rect.

## 5. Surface-Specific Rules

1. **Welcome / Empty Tab:** Hide repo-only chrome in the sidebar when no repository is open (no dead `Changes 0`, `Review working changes`, `COMMIT HISTORY` + `Scope v`). Center the Welcome card sized cleanly to its content (no giant empty void below 1–2 recent repos).
2. **Sidebar Chrome & Navigation:**
   - Repo header aligns icon, bold repo name, muted inline branch badge (not a broken-looking empty input box), and `...` overflow button cleanly on one visual baseline.
   - `Changes / Refs / Agent` uses a cohesive recessed segmented switcher (matching `Unified / Split`), with clean horizontal padding.
   - Secondary git actions (`Push / Pull / Fetch / Stash`) read as quiet compact ghost/outline utility buttons, visually subordinate to `Changes / Refs / Agent` and file list.
   - Commit box has comfortable inner padding, subtle border, clear active/disabled state for `Commit`, and clean separation from `COMMIT HISTORY`.
3. **Reading Canvas & Diff Header:**
   - Eliminate duplicate headers: no redundant `"Selected file"` orphan row, no triple-stacked file paths, no noisy `"Complete file"` banner bar.
   - Top context bar cleanly aligns breadcrumb left and secondary view toggles (`Outline`, `Blame`, `Options`) right with consistent quiet button chrome.
   - Hunk action buttons (`Expand`, `Approve`, `Comment`) use crisp uniform spacing (4px–8px gaps) and never crowd or collide.
4. **Feedback Basket:**
   - Clean header row: `"Feedback (N)"` left, quiet `"Show resolved"` toggle + close `×` right on the same row (never floating centered on its own dead row).
   - Scope label in plain human copy (`"Working tree"`, not `"working tree (uncommitted)"`).
   - Comment body in readable UI sans (`14px`), not monospace; cut redundant `"Location: Current"` metadata line; `Delete` button uses AA-contrast coral ink.
   - Footer caption concise (`"Unsaved draft"`, not `"Local draft pending save"`).
5. **ACP Agent Companion Panel:**
   - **Layout order:** Compact header (segmented `Muse | Claude | Codex` + status dot/label + session actions + compact inline `Model` / `Mode` / `MCP` chips) → **Scrollable transcript body taking remaining height** → **Permission card** (if pending, amber left accent, side-by-side compact `Allow` / `Reject`) → **Bottom-docked composer** (padded multiline input + inline `File` / `Image` / `Send`/`Stop` row).
   - **Wide / Full-Window Guard:** When no document pane is open beside the sidebar, cap the Agent column width (`<= 760px`, centered) so buttons and inputs never stretch across 1280px!
   - **Transcript Styling:** Clear visual differentiation between user turns (subtle raised card/tint), agent prose (clean UI sans), thinking state (muted italic/tertiary), tool executions (compact monospace status pills), and written-file diff links.
6. **Modals (`Quick Open`, `Shortcuts`) & Toasts:**
   - Dynamic height bounded to content (no half-empty void).
   - `Quick Open`: clear spacing between title and scope badge, quiet accent focus ring, padded result rows with crisp filename + muted dir + right-aligned status tag.
   - `Shortcuts`: section groupings, monospace keycap chips (`kbd`), full scroll clearance above footer so the last row is never half-clipped.
   - Toasts: compact, subtle hairline + semantic left bar, crisp text, clean close glyph.
7. **Status Bar & Narrow Viewports (`<= 900px`):**
   - Never display stale `"Loading repository"` once loaded; show clean repo/branch/ready indicator.
   - At narrow widths (`<= 900px`), never let docked panels (Feedback Basket / Outline) crush the code editor below readable width or overlap the file header bar—constrain or auto-stack/narrow gracefully.
8. **Clean Copy Discipline:**
   - Proper singular/plural (`"1 comment"`, `"1 file changed"`).
   - Cut filler, internal debug strings, and redundant captions that restate adjacent labels.
