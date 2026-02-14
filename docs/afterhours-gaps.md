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

## Known Vendor Bugs

### tab_container() position bug
- **Issue:** Tab strip renders at screen-absolute position, ignoring parent container bounds.
- **Impact:** Cannot use `tab_container()` for multi-repo tabs.
- **Workaround:** Build manual tab buttons in a row using `div()` + `button()`.
- **Status:** Known upstream issue.

### toggle_switch() layout issue
- **Issue:** Creates sibling entities that consume extra layout space.
- **Impact:** Toggle switches misalign adjacent elements.
- **Workaround:** Use `with_no_wrap()` on parent, increase container height.
- **Status:** Known upstream issue.

### text_input() requires InputAction enum values
- **Issue:** `text_input::text_input()` template expects `InputAction::TextBackspace`, `TextDelete`, `TextHome`, `TextEnd` enum values, which are not part of afterhours and must be defined by the host app.
- **Impact:** Cannot use `text_input()` without adding these to the app's `InputAction` enum and registering key mappings.
- **Workaround (T031):** Added the required enum values to `src/input_mapping.h` and registered key mappings in `src/preload.cpp`.
- **Status:** Design limitation — afterhours text_input assumes the host app defines these actions.

### Clipboard shortcuts not wired in text_input
- **Issue:** `text_input()` doesn't wire Cmd+C/V/X clipboard shortcuts — requires manual action binding.
- **Impact:** Copy/paste doesn't work in commit message editor without manual wiring.
- **Workaround:** Wire clipboard shortcuts manually in `InputSystem` via `ActionMap`.
- **Status:** Known upstream issue (same issue in wordproc).

---

## Feature Requests (Lower Priority)

### Cursor change on hover
- **What's missing:** No API to change the mouse cursor (e.g., resize cursor on divider hover).
- **Workaround:** Skip cursor changes for now. Visual highlight on hover is sufficient.

### Synchronized scroll views
- **What's missing:** No built-in way to synchronize scroll position between two `scroll_view()` containers.
- **What floatinghotel needs:** Side-by-side diff view with synchronized scrolling.
- **Workaround:** Track scroll offset manually and apply to both scroll views each frame.

### Virtualized list rendering
- **What's missing:** No virtualized list that only renders visible items (for performance with 1000+ items).
- **What floatinghotel needs:** Large commit logs (10k+ commits), large file lists.
- **Workaround:** Manually implement windowed rendering inside `scroll_view()` — only create `div()`/`button()` entities for visible rows based on scroll offset and container height.
