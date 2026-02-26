# Afterhours Gaps & Missing Primitives

Tracking afterhours features/bugs that floatinghotel needs but are not yet implemented upstream. Each entry includes what's missing, what floatinghotel needs it for, and the workaround.

**Policy:** Never edit `vendor/afterhours/` directly. Build workarounds in `src/ui/` and document gaps here so the afterhours maintainer can address them upstream.

---

## Missing Primitives

### 1. Draggable Divider
- **What's missing:** No draggable divider/splitter widget for resizing adjacent panels.
- **What floatinghotel needs:** Sidebar/main content resize, commit log/files split within sidebar, side-by-side diff panel split.
- **Criticality:** BLOCKER for P0
- **Workaround:** Built app-local in `src/ui/split_panel.h` using `div()` + `HasDragListener`. Tracks drag state per divider ID, reports position delta, clamps to min/max constraints.
- **Upstream request:** Add `draggable_divider()` to afterhours UI plugin with orientation, min/max, and cursor change support.

### 2. Split Pane
- **What's missing:** No split pane container that manages two child regions with a resizable divider.
- **What floatinghotel needs:** Sidebar + main content layout, sidebar internal split (files/log), side-by-side diff view.
- **Criticality:** BLOCKER for P0 (depends on Draggable Divider)
- **Workaround:** Built app-local in `src/ui/split_panel.h` using `div()` containers + `draggable_divider()`. Supports vertical and horizontal orientations with configurable min sizes.
- **Upstream request:** Add `split_pane()` to afterhours UI plugin.

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

### 5. Context Menu
- **What's missing:** No right-click context menu widget.
- **What floatinghotel needs:** Right-click menus on files (stage/unstage/discard), commits (copy hash/cherry-pick/revert), branches (checkout/delete/rename).
- **Criticality:** HIGH for P1
- **Workaround:** Built app-local in `src/ui/context_menu.h` using absolute-positioned `div()` at cursor location. Manages global state (only one context menu at a time), window edge flipping, click-outside-to-close.
- **Upstream request:** Add `context_menu()` to afterhours UI plugin with right-click trigger, auto-positioning, and same item format as dropdown menu.

### 6. Anchored Popup / Popover
- **What's missing:** No anchored popup that appears relative to a trigger element (above, below, left, right).
- **What floatinghotel needs:** Commit button dropdown (amend/fixup options), branch selector popover, tooltips for toolbar buttons.
- **Criticality:** MEDIUM for P0
- **Workaround:** Reuse dropdown menu approach from `src/ui/menu_setup.h` with manual position calculation relative to the trigger element's bounds.
- **Upstream request:** Add `popover()` to afterhours UI plugin with anchor element reference, placement preference, and auto-flip when near window edges.

---

## Styling & Layout Gaps

### Text Overflow / Ellipsis — RESOLVED (120a9ed) — **BUG: hangs with expand() sizing**

**Was:** Text clipped silently or showed debug indicator. Manual truncation with `substr()` was fragile.

**Now:** `with_text_overflow(TextOverflow::Ellipsis)` on `ComponentConfig`. Renderer binary-searches for longest fitting prefix and appends "...".

**Bug found:** Using `with_text_overflow(Ellipsis)` on elements sized with `expand()` (or `children()`) causes an infinite hang on launch. The binary search in the truncation code likely runs against a 0-width container before layout resolves, creating an infinite loop. Only safe to use on elements with fixed pixel widths.

---

### No Font Weight Support — OPEN

**Problem:** `ComponentConfig` has `with_font()` and `with_font_size()` but no `with_font_weight()`. The mockup uses `font-weight: 600` on diff file headers and status letters. The only way to approximate bold text is to load a separate bold font file and switch font names per-component, which is cumbersome.

**Impact:** Diff file headers and status badge letters should be semi-bold but render at normal weight.

**Suggested fix:** Add a `FontWeight` enum and a `with_font_weight(FontWeight)` method. The text rendering path would select the appropriate font variant at render time.

---

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

### `with_font_tier()` Only Supports `h720()` Scaling — OPEN

**Problem:** `with_font_tier(FontSizing::Tier)` hardcodes `h720()` for the font size. There is no way to use the tier lookup system with fixed `pixels()` sizing.

**Impact:** Adopting `with_font_tier()` forces proportional font scaling.

**Suggested fix:** Add a scaling mode parameter or a separate `with_font_tier_px(FontSizing::Tier)` method.

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

### Synchronized scroll views
- **What's missing:** No built-in way to synchronize scroll position between two `scroll_view()` containers.
- **What floatinghotel needs:** Side-by-side diff view with synchronized scrolling.
- **Workaround:** Track scroll offset manually and apply to both scroll views each frame.

### Virtualized list rendering
- **What's missing:** No virtualized list that only renders visible items (for performance with 1000+ items).
- **What floatinghotel needs:** Large commit logs (10k+ commits), large file lists.
- **Workaround:** Manually implement windowed rendering inside `scroll_view()` — only create `div()`/`button()` entities for visible rows based on scroll offset and container height.
