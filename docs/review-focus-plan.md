# Review-focus implementation

Approved target: `docs/mocks/review-focus.html`.

User amendments: changed files above commit history, single-line sidebar commit
rows without authors, and no sidebar width change during the panel slide-out animation.

- [x] Ground: capture the current native app and trace layout, review state, zoom, and footer ownership.
- [x] Sketch: compare two native integration shapes against the approved visual target.
- [x] Agree: synthesize the smallest design that preserves existing review behavior.
- [x] Implement: build and verify shell, review navigation, and code presentation in separate units.
- [x] Scrap/check: remove any superseded presentation path, then compare matching screenshots and run regressions.

Design comparison phases: frame, fan out, cross-judge, pick, graft, verify.

Success means the native app resembles the approved mock, preserves fast commit and file navigation, exposes existing review tools without the full-width toolbar stack, fits within the window at increased zoom, and keeps the footer visible while content scrolls.

Constraints: run shell commands under nice; no added source comments; no vendor edits; no pushes; preserve the existing mock and TODO edits. Record confirmed afterhours limitations in `docs/afterhours-gaps.md`.

## Design decision

Use logical shell rectangles. Keep OS resizing, pointer input, and scroll offsets physical at their boundaries. Candidate A keeps that contract explicit without adding a second renderer. Candidate B's physical shell would leave conversion rules in each content caller. The independent gpt-5.5 review also chose A.

Retain one source path and revision in the existing reader, with a separate active content selection. Switching to the commit does not erase the source tab. Preserve async patch loading, review scope identity, and virtualized history. Secondary filters remain reachable through Options. Narrow toolbars use two rows.

The virtual-list adapter needs live middle/end-of-list checks before acceptance because the framework creates logical pixel wrappers from physical row metrics. No vendor code changes.

## Verification

The native capture, file-folding, retained-source-tab, and zoom scenarios pass.
All 29 unit suites and 74 legacy UI flows pass. The 50 feature scenarios pass their functional checks,
including Markdown and hex navigation at 140% zoom. Item 15 still misses one
p99 timing limit: 26.89 ms against a 20 ms budget. Its cache bounds and functional
checks pass; the performance suite is not green.
The feedback basket also emits a `basket_title` height-overflow warning. This
is an app layout issue, separate from the framework text-area zoom issue.

Startup readiness passes for restored, empty, and invalid repositories. Two
launches against the real saved workspace accepted the first commit click in
834 ms and 737 ms, with the window hidden until ready.

The Files sidebar's upstream text-area zoom-width issue remains documented in
`docs/afterhours-gaps.md`. The review viewer uses the app-local virtual-list
adapter; no framework implementation was copied or changed.

Text-selection, search, and intraline highlights now share the rendered text
origin. The focused native check covers both drag directions, unified and split
diffs, 140% zoom, and selection at the bottom of a source file.
`output/text-highlight/after-expanded/native.log` contains the passing run.

The Find-toggle overflow is fixed. The closing frame rendered the Find bar,
then recalculated the viewport as if that bar were already gone. Keeping the
height accumulated from the rendered controls removes the extra 34 pixels.
The native regression now checks viewport bounds with Find open at 100% and
140%, plus every frame's overflow diagnostics while opening and closing it.
Passing evidence is in `output/layout-followup/find-after/native.log`.
