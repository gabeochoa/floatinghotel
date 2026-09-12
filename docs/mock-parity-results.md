# Review-focus parity results

The pass addressed the fifty discrepancies in `mock-parity-issues.md`, including
the exposed background strip, file tree, commit heading, tabs, review controls,
and diff-file presentation. Five implementation commits keep the changes grouped
by component. Nothing was pushed and no source comments were added.

The reference remains `docs/mocks/review-focus.html`. Later user choices take priority:
files above history, single-line commit rows without authors, native menus, and
16-pixel code. The native screenshot uses real commit data, not the mock's fake
identity or comment count.

## Compare

- Reference with the same title: `output/mock-parity/mock-same-title-1440.png`.
- Before: `output/mock-parity/baseline/commit.png`.
- After: `output/mock-parity/verified/mock_diff/diff_default.png`.
- Enlarged: `output/mock-parity/verified/mock_diff/diff_narrow.png`.
- Viewed and folded: `output/mock-parity/verified/mock_diff/diff_viewed.png` and `diff_folded.png`.
- Split: `output/mock-parity/verified/mock_diff/diff_split.png`.

Paths above are relative to the repository root. Captures are local build
artifacts, with measured UI JSON beside each image. The baseline already has
the separately verified background-color fix; its failing-before capture is
listed in the decision trail.

## Verification

The final build passed with 15 compiler warnings. All 29 unit suites passed.
The focused native parity suite passed at 1440×1000 and 1100×760 with 140% zoom.
It exercises file and folder folding, Viewed state, source-tab return, split
view, footer containment, and scrolling over the previously dead tree area.

The broader batch passed 73 of 74 flows. Its only failure was three stale
`Commit history` assertions after the heading changed to uppercase. The
corrected flow passed on rerun, giving coverage of all 74 flows. The batch log
is `output/mock-parity/regressions/legacy.log`; the corrected run is
`output/mock-parity/regressions/commit-rerun.log`.

Selection and find geometry, branching and merges, large commit messages,
feedback layout, toast layout, splitter behavior, and native-menu behavior
also passed. The text-stability check found identical text across 15 rendered
frames, each with 296 UI commands. Logs are under
`output/mock-parity/regressions/`.

Two folded files retain a measured 14-pixel gap. The feedback capture contains
a visible `1 unresolved` file-state label; the binary fixture contains a
visible `New file` label. Review state was not lost when paths and counts were
separated.

Run the focused comparison with an Afterhours checkout whose loaded history
contains `Exempt a deliberate pill from the corner radius lint` and
`Take the minimum instead of sorting for an ordered first`:

```sh
nice -n 10 bash tests/check_mock_parity.sh output/mock-parity/recheck /path/to/afterhours
```

Run the adjacent regression checks and unit suites:

```sh
nice -n 10 bash tests/check_mock_regressions.sh output/mock-parity/regressions-recheck
nice -n 10 bash tests/run_unit_tests.sh
```

The decision trail is `mock-parity.tsv`. Failed intermediate captures remain
under `output/mock-parity/final` and `output/mock-parity/clipping-check`; they
are not accepted final evidence.

## Remaining limits

Styled paths and captions clip without an ellipsis because Afterhours renders
the original colored runs after computing ellipsis text. The scroll-input
adapter depends on the framework's public post-update system list. Both are
documented in `afterhours-gaps.md`.

Compact folder chains and the full enclosing diff-card border remain outside
the selected fifty. Native font metrics differ from browser CSS, so nominal
sizes were calibrated against screenshots. Physical desktop menu appearance
still needs a normal macOS-session check; headless screenshots use test menus.

The gpt-5.5 audit found no blocking issue in the focused parity evidence.
Its visible-state concern was fixed, its documentation path correction was
applied, and the pending broader checks above have now completed. The audit
skill prompted these follow-up corrections and the retained failure evidence.
