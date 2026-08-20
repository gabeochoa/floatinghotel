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

### Div backgrounds render opaque — no alpha blend for overlays — OPEN

**Problem:** A `div` background does not alpha-blend over already-drawn content.
Neither `with_custom_background(Color{r,g,b,45})` (low alpha in the color) nor
`with_opacity(0.32f)` produces a translucent overlay — both render fully opaque,
covering whatever is underneath. (Toast fade-in via `HasOpacity` works, but a
plain overlay div drawn on top of sibling text does not composite over it.)

**Impact:** The diff drag-to-select highlight (`src/ui/diff_renderer.h`,
`diff_sel`) is an overlay box drawn on top of the diff line's text. A
"translucent selection" is impossible this way — the box just hid the selected
text entirely.

**Workaround:** Draw an *opaque* selection box (`theme::SELECTED_BG`), then
re-draw just the selected substring of text on top of it (same mono font,
positioned at the selection's start x), so the text stays readable — editor
style (solid selection color, text on top) rather than a translucent wash.

**Suggested fix:** Honor `Color` alpha (and/or `with_opacity`) for div
backgrounds so overlays can be genuinely translucent, or provide a
selection/highlight primitive that renders behind text within an element.

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

### Row Flex Layout Broken with expand() Children — OPEN

**Problem:** When a `button` or `div` with `FlexDirection::Row` contains children, any child sized with `expand()` consumes the full parent width instead of the remaining width after fixed-size siblings.

**Impact:** Cannot create a row like `[status_letter(16px) | filename(expand)]` — the filename fills 100% and the status letter wraps below.

**Workaround:** Bake all content into a single label string on the parent element, avoiding child elements entirely.

**Suggested fix:** The autolayout engine should calculate `expand()` as `parent_content_width - sum(fixed_sibling_widths)` in Row flex, matching CSS `flex: 1` behavior.

---

### Custom Colors Bypass Disabled Dimming — OPEN

**Problem:** `resolve_background_color()` returns custom colors as-is when `disabled=true`. The disabled dimming only applies to `Theme::Usage`-based colors. Since real apps overwhelmingly use `with_custom_background(Color)`, `with_disabled(true)` blocks interactions but does NOT change the visual appearance.

**Workaround:** Manually check `enabled` in each preset factory function and set different bg/text colors.

- should be fixed (wm_afterhours added `disabled_opacity` to theme.h)

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

### Clipboard shortcuts not wired in text_input
- **Issue:** `text_input()` doesn't wire Cmd+C/V/X clipboard shortcuts — requires manual action binding.
- **Impact:** Copy/paste doesn't work in commit message editor without manual wiring.
- **Workaround:** Wire clipboard shortcuts manually in `InputSystem` via `ActionMap`.
- should be fixed (wm_afterhours implemented clipboard shortcuts in text_input phases 1-9)

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
