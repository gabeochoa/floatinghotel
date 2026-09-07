# Afterhours Gaps & Missing Primitives

Tracking afterhours features/bugs that floatinghotel needs but are not yet implemented upstream. Each entry includes what's missing, what floatinghotel needs it for, and the workaround.

**Policy:** Never edit `vendor/afterhours/` directly. Build workarounds in `src/ui/` and document gaps here so the afterhours maintainer can address them upstream.

**Reviewed against the cf3e0f1 bump.** Several entries here were stale — the
API had shipped and nobody came back to the doc. Anything below still marked
OPEN was checked against that revision.

Adopted in that pass: `with_corner_radius`, `imm::divider`,
`ctx.is_right_click` + a real context menu, `afterhours::shutdown()`,
`ui::measure_text_line`, trackpad pinch and `Theme::ui_scale` zoom.

Deliberately not adopted, with reasons in the entries below:
`with_styled_label` (child divs already do it better here),
`hsplit_pane`/`vsplit_pane` (panels are absolutely positioned, no container to
wrap), `animation::set_instant` (the animation plugin is not registered — the
toast plugin does not use it), `with_font_weight` (needs bold font files).

---

## Missing Primitives

### 1. Draggable Divider — RESOLVED upstream (48f808d), adopted
`imm::divider(ctx, mk(...), Axis)` is truthy on the frames it moved, and
`.as<float>()` is that frame's travel in `rect()` space.

`render_sidebar_divider` uses it. The hand-rolled version polled the raw
backend mouse position and undid letterboxing itself
(`mouseX * 1280.0f / sw`), and jumped whenever you grabbed the bar off centre,
because it set the width from the cursor position instead of accumulating a
delta. Covered by `flow_sidebar_resize.e2e`.

### 2. Split Pane — RESOLVED upstream (48f808d), not needed
`imm::hsplit_pane` / `vsplit_pane` wrap a divider around two regions, taking a
`float&` ratio the drag updates in place. floatinghotel positions its panels
absolutely from `LayoutComponent` rather than nesting them, so there is no
container to hand to a split pane. The divider alone is the part it needed.

### 3. Tree Node
- **What's missing:** No collapsible tree node widget for hierarchical list views.
- **What floatinghotel needs:** File tree view in sidebar (grouped by directory), branch list with expandable remote sections.
- **Criticality:** BLOCKER for P1 (tree view of changed files)
- **Workaround:** Built app-local in `src/ui/tree_view.h` using `div()` + `button()` with indent levels and expand/collapse state tracked in a static map.
- **Upstream request:** Add `tree_node()` to afterhours UI plugin with arbitrary nesting, expand/collapse animation, and arrow icon rotation.

### 4. Dropdown Menu
- **What's missing:** No dropdown menu widget (click to open a list of items below a trigger element).
- **What floatinghotel needs:** Menu bar dropdowns (File, Edit, View, Git, Help), commit button dropdown (amend/fixup), template picker, branch selector.
- **Criticality:** HIGH for P0 (needed for menu bar)
- **Workaround:** Built app-local in `src/ui/menu_setup.h` using `div()` + `button()` with absolute positioning. Manages open/close state, hover-to-switch between adjacent menus, and click-outside-to-close.
- **Upstream request:** Add `dropdown_menu()` to afterhours UI plugin with configurable items (label, shortcut text, separator, disabled state, callback).

### 5. Context Menu — RESOLVED (176ea8f upstream + app), adopted
`ctx.is_right_click(id)` answers "a secondary click finished over this element
or something inside it". The target needs a click or drag listener, since that
is what hit-testing resolves against; asking about a plain `div` warns rather
than silently never firing.

`src/ui/context_menu.{h,cpp}` had sat with no callers since there was no
right-click to wire it to. It now renders (`render_context_menu`, called last
from `MenuBarSystem` so it lands above the menu dropdowns) and file rows open
it with Stage/Unstage plus Copy Path. Covered by
`flow_context_menu_file.e2e`, which clicks the item and checks the file was
actually staged.

Still to do: menus on commits (copy hash, cherry-pick, revert) and branches.
No Discard yet — there is no `discard_file` git command, and a destructive one
wants a confirmation step.

### 6. Anchored Popup / Popover
- **What's missing:** No anchored popup that appears relative to a trigger element (above, below, left, right).
- **What floatinghotel needs:** Commit button dropdown (amend/fixup options), branch selector popover, tooltips for toolbar buttons.
- **Criticality:** MEDIUM for P0
- **Workaround:** Reuse dropdown menu approach from `src/ui/menu_setup.h` with manual position calculation relative to the trigger element's bounds.
- **Upstream request:** Add `popover()` to afterhours UI plugin with anchor element reference, placement preference, and auto-flip when near window edges.

---

## Styling & Layout Gaps

### Text Overflow / Ellipsis — RESOLVED (120a9ed)

`with_text_overflow(TextOverflow::Ellipsis)` on `ComponentConfig`. The renderer
binary-searches for the longest fitting prefix and appends "...".

The hang this entry reported with `expand()`/`children()` sizing is fixed
upstream, so the "fixed pixel widths only" restriction no longer applies.

---

### Div backgrounds render opaque — no alpha blend for overlays — RESOLVED, adopted

A `div` background alpha-blends now, so `with_custom_background(Color{r,g,b,a})`
with a low `a` tints what is underneath instead of hiding it. `with_opacity`
also scales a colour's existing alpha rather than replacing it.

The diff drag-to-select highlight (`src/ui/diff_renderer.h`, `diff_sel_hl`) is
a plain translucent box over the already-rendered line — the opaque-box-plus-
re-drawn-substring workaround this entry described is gone.

---

### No Rich Text / Multi-Color Text in a Single Label — RESOLVED, and moot

`with_styled_label({{"M ", STATUS_MODIFIED}, {"theme.h ", TEXT_PRIMARY}, ...})`
is on `ComponentConfig` (`component_config.h:658`).

Deliberately not adopted: `render_file_row_impl` already solved this with three
sized child divs (status glyph, filename, dir), which is what gives each column
its own width and ellipsis. A styled label would trade that away to save two
entities. The workaround described below — baking everything into one string
and losing the coloured status letter — has not been the code for a while.

<details><summary>original entry</summary>

### No Rich Text / Multi-Color Text in a Single Label — OPEN

**Problem:** Each `div` or `button` can only have one text color. To show a filename in white and its directory path in gray on the same row, you need two separate child `div` elements.

**Workaround:** Bake status letter and filename into a single label string (e.g. `"M  README.md"`). Colored status letters are sacrificed.

**Suggested fix:** Support a `StyledText` API:
```cpp
.with_styled_label({
  {"M ",        theme::STATUS_MODIFIED},
  {"theme.h ",  theme::TEXT_PRIMARY},
  {"src/ui",    theme::TEXT_SECONDARY}
})
```
</details>

---

### `with_font_weight` — BLOCKED on font files, not on API

`with_font_weight` is on `ComponentConfig`. It looks up a font registered as
`"<font>@bold"` and falls back to the base font when there is none, which is
why this read as "no font weight support"; since `90f8ae8` that fallback warns
once instead of being silent.

Adopting it needs `Roboto-Bold.ttf` and `JetBrainsMono-Bold.ttf` in
`resources/fonts/` and two more `fontMgr.load_font(... "@bold")` calls in
`preload.cpp`. That is a licensing/asset decision, so it is left alone here.

---

### Row Flex Layout Broken with expand() Children — RESOLVED (0c67090), adopted

`expand()` in a Row now takes the remaining width after fixed-size siblings,
which is what CSS `flex: 1` does and what this entry asked for. Verified at
several sidebar widths: `[status(20px) | filename(expand) | dir(90px)]` keeps
all three on one line and gives the name the slack.

The sidebar file row was computing `nameW = totalW - dirW - GAP*2 - STATUS_W`
in app code to work around it. That arithmetic is gone; only the dir column
carries a width now.

---

### Custom Colors Bypass Disabled Dimming — RESOLVED

`resolve_background_color()` now routes a custom colour through
`theme.disabled_variant()` when `disabled` is set, which mixes toward the
background by `Theme::disabled_opacity` (0.3) and scales alpha to match. So
`with_disabled(true)` changes the appearance of a `with_custom_background()`
element, not just its hit-testing.

The presets still pick their own disabled bg/text rather than leaning on this,
which is deliberate — `theme::DISABLED_BG`/`DISABLED_TEXT` are chosen colours,
not a generic 30% mix.

---

### `with_font_tier()` is deprecated — use `with_font_size(FontSize::Small)`

The `h720()`-only complaint stands, but the method itself is now marked
`[[deprecated]]` in favour of `with_font_size(FontSize::...)`, which is what
floatinghotel already uses everywhere. Nothing to do.

---

## Resolved Styling Gaps

| # | Gap | Commit |
|---|-----|--------|
| 1 | Custom hover background | a0c2b03 |
| 3 | Text overflow ellipsis | 120a9ed |
| 4 | Flex gap | 37fe6f4 |
| 6 | Per-side border | 9eb0796 |
| 7 | Default transparent bg | 778f786 |
| 8 | Absolute child positioning | 1cb50a3 |
| 9 | Cursor changes | 27b535e |
| 10 | Letter spacing | bff4609 |
| 12 | Adaptive scaling mode | SUPERSEDED |

---

## App bugs found while adopting the above

### The last item of a menu dropdown does not respond to clicks

Clicking it closes the dropdown (so it looks like it worked) but never runs the
item's action. Reproduced with `View > Reset Zoom`: the identical click
sequence ending on `Zoom Out` (second to last) changes `ui_scale`, and ending
on `Reset Zoom` leaves it untouched. `MenuBarSystem` computes each item's hover
rect by accumulating `itemY`, and the same `itemY` positions the button, so the
two ought to agree -- they do not for the final entry.

It went unnoticed because the tests covering those items assert on a toast, and
toasts outlive the click: `flow_stub_toast_view_menu` clicked three items in a
row that all toast the same "not yet implemented" string, so the third
assertion matched the first item's toast. `flow_stub_toast_edit_menu` has the
same shape on `Find...`, which is the last Edit item -- suspect that one too.

## Known Vendor Bugs

### tab_container() position bug
- **Issue:** Tab strip renders at screen-absolute position, ignoring parent container bounds.
- **Impact:** Cannot use `tab_container()` for multi-repo tabs.
- **Workaround:** Build manual tab buttons in a row using `div()` + `button()`.

### toggle_switch() layout issue
- **Issue:** Creates sibling entities that consume extra layout space.
- **Impact:** Toggle switches misalign adjacent elements.
- **Workaround:** Use `with_no_wrap()` on parent, increase container height.

### text_input() requires InputAction enum values
- **Issue:** `text_input::text_input()` template expects `InputAction::TextBackspace`, `TextDelete`, `TextHome`, `TextEnd` enum values, which are not part of afterhours and must be defined by the host app.
- **Impact:** Cannot use `text_input()` without adding these to the app's `InputAction` enum and registering key mappings.
- **Workaround:** Added the required enum values to `src/input_mapping.h` and registered key mappings in `src/preload.cpp`.

### Clipboard shortcuts not wired in text_input — RESOLVED, adopted
- afterhours implements copy/cut/paste/undo/redo/word-motion/shift-selection and
  binds them in `ui::default_keymap<InputAction>()`, matched to the app's enum
  **by name**. A name the enum does not have compiles the feature out silently,
  which is how the commit box went without them for so long; the library warns
  about the missing names now.
- `src/input_mapping.h` carries all eleven, and `src/preload.cpp` takes
  `default_keymap` instead of hand-listing four keys.

---

### Draw capture is not implemented on the sokol/Metal backend — OPEN
- **Issue:** `capture::record()` is called from `backends/raylib` and
  `backends/none` only. `backends/sokol` never records, even though the UI
  renderer sets up `capture::Scope` attribution around every draw.
- **Impact:** `expect_drawn`, `expect_not_drawn`, `expect_drawn_at` and
  `dump_draws` are no-ops here — `dump_draws` reports `0 draws this frame`.
  `tests/e2e_scripts/flow_commit_graph_lines.e2e` is named for asserting that
  graph lines are drawn and can only take screenshots.
- **Suggested fix:** call `capture::record` from
  `backends/sokol/drawing_helpers.h` the way the other two backends do.

### `with_placeholder` is a text_field feature; text_area ignores it — OPEN
- **Issue:** `text_area()` never reads `config.placeholder`. Setting it
  compiles, runs, and shows nothing.
- **Impact:** A multi-line field cannot have a hint inside it. The commit box
  keeps its hint as a label stacked above the input, costing 16px of sidebar.
- **Suggested fix:** either honour `placeholder` in `text_area`, or reject it
  at the call so it fails loudly instead of silently.

### Font codepoint coverage helpers are raylib-only — OPEN
- **Issue:** `default_codepoints()` and `font_has_glyph()` live in
  `backends/raylib/font_helper.h`. The sokol backend rasterises on demand
  through fontstash and has neither.
- **Impact:** No way to ask whether a face covers a codepoint before using it,
  so the ASCII fallbacks in `sidebar_system.h` (`"> "` for ▶ at the review
  strip, no arrows on Push/Pull) stay guesswork.

---

## Feature Requests (Lower Priority)

### Synchronized scroll views — RESOLVED upstream (dd579a4), and not needed here
`HasScrollView::sync_group` — give two or more views the same non-zero id and
scrolling any one moves the rest, on their enabled axes only.

floatinghotel does not need it: `render_side_by_side_diff` puts both sides in
*one* scroll view as two cells per row, so they cannot desync. Reach for
`sync_group` only if the two panes ever become separate scroll views.

### Virtualized list rendering
- **What's missing:** No virtualized list that only renders visible items (for performance with 1000+ items).
- **What floatinghotel needs:** Large commit logs (10k+ commits), large file lists.
- **Workaround:** Manually implement windowed rendering inside `scroll_view()` — only create `div()`/`button()` entities for visible rows based on scroll offset and container height.
