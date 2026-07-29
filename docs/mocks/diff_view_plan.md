# Diff View — Mock → UI Library Plan

Companion to `diff_view_mock.html`. Goal: make the floatinghotel diff view a
first-class VS Code / GitHub-style diff, and in doing so push the afterhours UI
library toward "can build any game/app UI" parity with the web.

**Constraint:** afterhours is shared (wm_afterhours, wordproc, others). We build
everything **app-local first** in `src/ui/`, prove it here, then propose the
generalized primitive upstream as a *non-breaking add* where possible, flagged
`BREAKING` only where unavoidable.

---

## 1. What the mock shows (target)

Two diff modes over the same hunk data:

- **Inline** — single column, gutter (old/new line no.) + colored code rows.
  This is roughly what we ship today.
- **Side-by-Side** — two synced columns (old | new), add/del/empty rows aligned.
  **Currently a no-op** — `main_content_system.h:80` always calls
  `render_inline_diff` regardless of `layout.diffViewMode`. The menu toggles a
  flag that changes nothing on screen.

Both modes reuse: stats header, per-file header with copy button, hunk header
with copy button, gutter styling, and the theme.h palette.

---

## 2. Gap analysis — web CSS vs. afterhours (diff-view-specific)

| # | Web does trivially | afterhours today | Blocks in mock | Local-first plan | Upstream ask |
|---|---|---|---|---|---|
| G1 | `flex: 1` sizing in a row | `expand()` child eats 100% of row (gaps doc §"Row Flex Layout Broken") | gutter+code row, sidebar `[name | badge]` | Keep baking labels / fixed-px gutter | **Fix autolayout** `expand = parent - Σfixed` (behavior fix, low breakage) |
| G2 | multi-color spans in one line | one color per div (gaps doc §"Rich Text") | filename white + dir gray; `+4 -2` two-tone | Split into sibling divs | Add `with_styled_label({{text,color}...})` (additive) |
| G3 | `font-weight:600` | no `with_font_weight` (gaps doc) | file headers, badges look thin | Load a semibold font, swap by name | Add `FontWeight` enum + `with_font_weight` (additive) |
| G4 | two panes scroll together | no synced scroll (gaps doc §"Synchronized scroll") | side-by-side | Track offset, apply to both each frame | Add `link_scroll(a,b,axis)` (additive) |
| G5 | `overflow-x:auto` + wrap control | horizontal scroll for long code lines unclear | long lines in diff | Verify/hscroll container | Confirm `Overflow::Scroll, Axis::X` works in nested scroll |
| G6 | 10k rows cheap (browser virtualizes) | every row = entity (gaps doc §"Virtualized list") | huge diffs / 55+ commit log | Windowed render by scroll offset | Add `virtual_list(count, rowH, renderRow)` (additive, high value) |
| G7 | selectable text / copy | no text selection in diff body | can't select a few lines | Per-hunk / per-file copy buttons (have) | Add text selection primitive (large, later) |

Existing docs already track most of these: `docs/afterhours-gaps.md`,
`docs/afterhours_issues.md`. This table narrows them to the diff view and adds
the local→upstream split.

---

## 3. Build plan (app-local, no afterhours edits)

1. **Wire the mode switch** — in `main_content_system.h`, branch on
   `layout.diffViewMode`: `Inline` → `render_inline_diff` (today),
   `SideBySide` → new `render_side_by_side_diff`. Removes the dead-toggle.
2. **`render_side_by_side_diff`** in `diff_renderer.h` — align hunk lines into
   (old,new) pairs: context → both; `-` → left only; `+` → right only; pad the
   empty side. Reuse existing colors/gutter helpers.
3. **Synced scroll (G4)** — one scroll container driving both columns, or two
   columns in one scroll wrapper (simplest: single vertical scroll, both columns
   share it since rows are aligned).
4. **E2E** — extend `flow_v4_side_by_side_diff.e2e` to actually *select a file*
   then screenshot, so we capture real side-by-side output (today it screenshots
   the empty state). Add a long-line + long-diff case.
5. **Polish** — file-header weight (G3 workaround), two-tone stats (G2 workaround).

Every step verified headless via `./tests/run_e2e.sh` (works in this env with
`GIT_CONFIG_GLOBAL=/dev/null`).

---

## 4. Proposed upstream (afterhours) — after local proof

Ordered by value/effort. Only G1 is a behavior change; rest are additive.

- **G1 (behavior fix, mild breaking):** `expand()` in Row = remaining space.
  Anyone relying on the current "eats 100%" bug could shift — flag `BREAKING`,
  ship behind a layout-version note.
- **G6 `virtual_list` (additive, biggest game-UI win):** windowed rendering.
- **G4 `link_scroll` (additive).**
- **G2 `with_styled_label` / G3 `with_font_weight` (additive).**

Local workarounds stay until each lands upstream, so nothing breaks for other
afterhours users in the meantime.
