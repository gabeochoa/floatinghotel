# Step 06: tab overflow

Document tabs keep a readable width and scroll inside their own viewport.
Left/right controls and horizontal wheel input move the strip. Opening or
activating a document, resizing, and zoom changes reveal the active tab without
moving the main content. A tab never exceeds the strip viewport width, so its
close control remains reachable in narrow windows. The strip uses buttons and
wheel input instead of an overlapping scrollbar.

The open-tabs menu lists every document, including path/revision labels,
previews, and a Current marker. Mouse selection and Up/Down/Enter activate the
chosen document through the navigation boundary. Long menus stay inside the
window, render only visible rows, and scroll to the keyboard selection. Escape
dismisses the menu without changing documents. Menu scrolling does not scroll
the reader beneath it. Menu callbacks verify the owning repository.

Selecting an empty working-changes tab keeps the reader and strip visible while
other documents are open. This avoids losing access to retained tabs merely
because the selected review has no files.

## Verification

Four strip geometry tests and 14 menu-state tests passed. The 25-tab native
replay passed at 100%, 140%, and 200%, including long names, narrow resize,
horizontal wheel input, scroll controls, keyboard and mouse menu selection,
reselecting an active tab that was scrolled away, and menu dismissal. Checks
cover exact document identities, inactive payloads, unchanged main-content
geometry, code scroll position, visible close controls, menu bounds, dropdown
pixels, and keyboard highlight colors. Render p99 was 2.49/3.40/1.88 ms.

The six-tab, preview/keep, close-hover, and title-fit journeys passed at all
three zooms. All seven existing navigation regressions passed, including the
12-checkpoint navigation boundary geometry replay.

The binary SHA-256 is
`9d21122bff6d443bbbad771bba3cf0879947ec78d74184d74f024f98bb44a0bf`.
The runner is `tests/tab_overflow.py --output NEW_DIRECTORY`. Evidence lives in
`docs/reading-navigation-evidence/step06`; full runs remain in
`output/step06-verified` and the other `output/step06-*` directories.

## Failures corrected

The first build needed explicit UI type namespaces. The first native replay
caught the strip disappearing on an empty working-changes tab and scroll
buttons snapping back because layout clamping reset the framework's direct-write
detection. Explicit scroll jumps now assign both current and target offsets.

The next replay caught Escape navigating away from the source while its menu
was open. Menu dismissal now consumes that Escape. Screenshot inspection then
caught missing font glyphs for the dropdown and current-tab mark, plus default
hover styling competing with keyboard selection. The final controls use a
vector chevron, a Current label, and explicit hover colors. The replay includes
pixel checks for these failures. Earlier failure evidence is retained.
