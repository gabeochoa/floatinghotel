# Step 16: Escape dismisses temporary UI

Escape dismisses one temporary control and leaves the current document open.
The semantic focus stack records opening order; menus take precedence over
panels underneath. Dismissal covers Quick Open, Find, repository search and its
preview, commit search, file history, feedback, comment composition, options,
saved-review comparison, and the comparison editor. Closing a composer retains
its draft. Closing a comparison editor detaches its pending result.

View now has Collapse reading panel and Expand reading panel actions. They
retain document identities and history. Expansion restores the saved logical
anchor after the scroll view is rebuilt. Cmd+W closes documents. Shortcut help
reflects these actions and the existing code-only font shortcuts.

Nine focus unit tests passed. `tests/escape_dismissal.py` passed at 100%, 140%,
and 200%, including nested picker/Find dismissal, search preview before search,
comment draft reopening, feedback closure, retained source/review/comparison
documents, explicit collapse/expand, scrolled source restoration, and Cmd+W.
It verifies that neither staged nor unstaged contents change. Modal and Close
button bounds passed at 1800×1100 and 900×800 for all three zooms.

Focus return, shortcut ownership, all seven navigation scenarios, persisted
window dimensions, and the legacy shortcut dialog replay passed. All 13
logical-anchor layout comparisons passed with errors below 0.001 pixels.
The hidden native window probe passed at DPI 1 and 2 with six coalesced resizes
and 28 matching Metal frames per run. It tests the existing resize mechanism;
the app-level collapse action is exercised by the headless replay. Every
recorded frame p99 gate was below 20 ms. Values are in `frame-metrics.json`.

The baseline in `output/step16-baseline` shows repeated Escape replacing a
selected commit. The first visual replay found the shortcut modal partly
offscreen at 200%: framework centering used physical dimensions and then
scaled its position again. The app corrects the final position and sizes the
dialog from the logical viewport. The Afterhours gap records the cause.

The first scrolled collapse replay in `output/step16-verified-escape` retained
line 24 while hidden, then reset to line 1 on expansion. The final replay in
`output/step16-final-escape` verifies the restored source line and its rendered
viewport fraction. `output/step16-verification.log` retains that failed attempt
alongside the passing regressions; `output/step16-final.log` records the final
checks. Persisting an explicit collapsed-panel override with restored tabs
remains part of the later dock work.

Evidence: `docs/reading-navigation-evidence/step16`.
Final binary SHA-256: `65b2c73b3278221a861c53c0b220f991136ac586947c39c1a1e1c51f70f83f73`.
