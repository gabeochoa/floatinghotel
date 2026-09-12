# Review UI spacing and feedback

- [x] Read the working principles and inspect layout ownership.
- [x] Frame: capture current screens and quantify spacing mismatches.
- [x] Verification: add a JSON dump of rendered UI bounds and spacing.
- [x] Sidebar: reduce commit-row and graph insets, right-align ages, extend the divider, and top-align empty files.
- [x] Menus: compare native macOS integration with an in-window fallback before choosing the implementation.
- [x] Toasts: verify wrapping, dismissal, lifetime, stacking, and viewport fit.
- [x] Review feedback: compare two ballroom layouts, then simplify the chosen workflow.
- [x] Spacing: audit primary screens at normal and enlarged zoom using screenshots and JSON.
- [x] Graph: exercise branches and merges in a disposable repository.
- [x] Final: run regressions, inspect screenshots, and obtain an independent trail review.

Success means commit rows remain single-line with a stable right-aligned age,
the sidebar boundary reaches the top of its column, empty file lists start at
the top, and the primary screens use shared spacing values without clipped
controls. Toasts must remain readable and dismissible within the viewport.
Feedback must have clear scope, comment actions, and an explicit local export.
The JSON dump must describe the same rendered geometry shown in screenshots.

This is roughly six implementation units plus baseline and final verification.
Each unit gets a local commit after its own checks. No pushes or source comments.
Native menu support and toast customization are the main framework unknowns.
Use the native headless runner for screenshots and preserve user settings.

The first throughput checkpoint is the JSON dump plus baseline screenshots.
If framework access prevents that, report the exact gap before broad edits.
Mechanical sidebar changes need no competing architecture. The menu and feedback
choices get concrete sketches before implementation. The visual audit is
read-only and can run alongside the JSON work.

Decisions and evidence are recorded in `docs/spacing-audit.tsv`.
