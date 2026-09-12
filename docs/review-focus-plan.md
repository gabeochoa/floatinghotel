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
The initial verification passed all 29 unit suites, 74 legacy UI flows, and the
50 feature scenarios' functional checks, including Markdown and hex at 140%.
It missed one Item 15 p99 limit, at 26.89 ms against a 20 ms budget.

The continuation's final build passes all 74 legacy flows again and all 40
focused highlight/viewport assertions. Three performance runs each pass all
eight unchanged p99 limits, with values from 2.01 to 10.24 ms. Search-cap checks
also pass. The last two runs use the final line-height build. These reruns do
not identify the cause of the earlier timing spike.
Evidence is in `output/layout-followup/final-flows.log`, `final-highlight`, and
`perf-1.log` through `perf-3.log`.

The broad legacy run has no skipped-entity query diagnostics, but still logs
165 layout-overflow lines outside the focused checks, including sidebar mode
tabs. Passing functional tests does not mean all layout validation is clean.
The feedback basket uses logical pixel heights throughout. Its former
`basket_title` overflow came from mixing screen-relative controls with a
logical-height scroll area. `output/layout-followup/basket-final/native.log`
verifies the title, footer, resize, comment resolution, and editing at 140% zoom
without basket-overflow warnings.

Startup readiness passes for restored, empty, and invalid repositories. Two
launches against the real saved workspace accepted the first commit click in
834 ms and 737 ms, with the window hidden until ready.

The Files sidebar's upstream text-area zoom-width issue is handled by the
app-local text-area adapter documented in `docs/afterhours-gaps.md`.
The review viewer uses the app-local virtual-list adapter. No framework
implementation was copied or changed.

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

Selected-file diffs also use the current panel height during resize instead
of falling back to the previous frame's computed height.
`output/layout-followup/resize-after/native.log` passes the basket/adjacent-diff
overflow check through zoom, window resize, and comment editing.

The performance rerun also exposes a separate transient warning when opening
Options in commit view. The `commit_find_host` keeps its 60-pixel height for
the frame that renders 150 pixels of controls. See
`output/layout-followup/perf-1.log`. This app layout issue remains open.
