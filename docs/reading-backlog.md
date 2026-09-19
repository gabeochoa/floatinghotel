# Reading backlog verification

## Pinned file headers

The original file header remains in normal scroll flow. After layout, its entire
subtree stays at the viewport top until the next file pushes it away. This retains
the same fold, path, status, Open file, Viewed, and Copy Diff controls and entity
identities. Folding a pinned file brings its header into view. Copy Diff builds
its payload only when activated.

Verification command: `nice -n 10 python3 tests/sticky_file_headers.py --output output/backlog-verified-sticky`.
The replay passes at 100%, 140%, and 200% zoom, including narrow windows, natural
handoff between files, collapse/expand, Viewed, source navigation and return,
unstaged review, and Find. It checks header and fold-button identity, dimensions,
pinned geometry, visible click targets, absence of collapsed code, and an unchanged
Git worktree/index. Find matches stay below the pinned controls. Keyboard, caret,
selection, and logical-anchor regressions also pass with the same reading inset.

[Results](reading-backlog-evidence/sticky.json) and the visually inspected
[contact sheet](reading-backlog-evidence/contact-sheet.jpg) are retained here;
full per-zoom layouts, PNGs, scripts, and logs are in `output/backlog-verified-sticky`.
The final 120-frame gate passes at 1.04/1.06/0.90 ms p99. This particular gate runs
after folding the unstaged file; it does not measure an expanded large diff.
The separate 3,000-pair split replay also passes its 20 ms gate at every zoom.
These are headless application CPU measurements, not physical compositor timings.

The missing Afterhours sticky-child primitive and the subtree-position workaround
are documented in [afterhours-gaps.md](afterhours-gaps.md#pinning-an-interactive-header).

## Reading runtime and measurements

Historical source metadata now comes from one bounded `ls-tree -l -z` request
for the resolved revision and literal path. Index and working-tree freshness
checks remain separate. Source and commit traces report queueing, validation,
cache access, read/decode, Git lock/process time, publication, and completion.
The reading probe records actual layout time through the matching frame.

The split renderer caches paired line indices and cumulative wrapped heights in
the existing metrics budget. Offscreen pairs skip preparation and intraline
comparison. Shared immutable wrap vectors remain accounted while a renderer holds
them after eviction; admission refuses entries that cannot fit alongside leases.
Active and recently visited source documents have explicit separate accessors.
Request validation compares current fields without allocating a temporary stamp.

Initial verification on the sticky-header build:

- `output/backlog-reading-after`: six complete reading journeys (two per zoom).
- `output/backlog-split-after2`: 3,000-pair diff at 100%, 140%, and 200%. Settled
  frames prepared zero pairs and compared 23–76 emitted pairs; split construction
  took 0.315–1.218 ms in the captured frames. Metrics remained at 2,742,323 bytes.
- `output/backlog-code_wrap.log`, `output/backlog-anchor_layout.log`, and
  `output/backlog-source_folding.log`: passed.
- `output/backlog-local_context.log`: 100% and 140% passed; the 200% run reached
  its final CPU frame gate but missed it at 23.28 ms while another build ran.
  This failure is retained; the later quiet repeat passes.
- `output/backlog-syntax_diff.log`: 100% and 140% passed. At 200% the final
  CPU gate missed at 20.33 ms with the build in flight; retained for comparison.

The warm-correct-content target is still open. A captured warm historical read
spent 220.62 ms inside its one remaining Git metadata process, versus 0.005 ms
in cache lookup and 0.140 ms decoding. A cache hit alone is not a fast switch.
The small initial samples do not establish p95 performance or a win over px0.

The extended Find check reproduced a pinned-header occlusion at 200% zoom:
`output/sticky-file-headers-find-before2/200/found.json` places the match at
507.40 px while the header ends at 620 px. The follow-up gives the reader an
explicit inset for the actual header height. Find, caret navigation, source
origins, selection hit tests, and logical anchors share the remaining code area.
The corrected geometry check passes in the final sticky replay at all three zooms.

## Review hash reuse

Review hunk keys now share the existing signature-cache budget, keyed by published
file and hunk identity. The persistent key format is unchanged. Review state is
queried live, so approving/unapproving or replacing contents invalidates the
appropriate result. Progress calculation shares the existing review logic.

`output/backlog-hash-units.log` records passing metrics, navigation, review-store,
and content-reader suites (including the new shallow-history test).
`tests/review_hash_benchmark.cpp` checks identical key bytes across 1,000 requests
for a 6,000-line replacement hunk. Its optimized local run took 223.168 ms without
reuse and 0.358 ms with reuse, with one cached scan. This isolates hash lookup;
it is not an end-to-end application speedup. The final split replay passes with
file/hunk scan counts unchanged between top and bottom scroll positions.

## Regression follow-up

On build `13048c1ef751870307763ad6cf058a6f9e8da7584b1c53f520bd4d8e3eddac3b`,
`output/backlog-final-replays.json` records passing pinned headers, anchor layout,
split preparation/hash reuse, local context, syntax, native clipboard commands,
wrapping, and feedback return. The local-context and syntax 200% frame gates that
missed during the earlier build both pass with no build running.

Two additional checks caught behavior to correct before final acceptance:

- Keyboard selection at 200% moved a fully visible before-side line by 8.80 px
  because the caret reveal rule required extra top margin. Fully visible lines
  now keep their scroll position during caret movement.
- Closing Find at 200% could leave the caret outside the viewport: the review had
  navigated to a before-side match while the caret remained on the after side.
  Explicit Find navigation now updates the caret and clears the prior selection;
  opening Find with a seed still preserves the original selection and position.

The failing evidence remains in `output/backlog-final-keyboard_selection` and
`output/backlog-final-reader_caret`. Focused reruns of the corrected build both pass, including the added assertion
that dismissing Find leaves the caret at the matching path, side, line, and column.

## Final batch verification

Optimized application SHA-256:
`042400d5a3c3ae6398896862c0e6a3979f4d35b66e85554e0bf7f20be88c40b9`.
Build logs: `output/backlog-find-caret-build.log` and
`output/backlog-latest-publish.log`.

All 11 sequential [verification jobs](reading-backlog-evidence/replays.json)
passed: pinned headers, keyboard selection at 200%, asymmetric before-side
selection, caret, selection gestures, offscreen selection, logical anchors,
split rendering, six reading journeys, conditional file tooltips, and the
historical metadata comparison. Compilation did not run during these replays.
The earlier clipboard, wrapping, local-context, syntax, and feedback-return
checks passed in `output/backlog-final-replays.json`. Unit results remain in
`output/backlog-hash-units.log` and `output/backlog-allocation-unit.log`.

The final [split measurements](reading-backlog-evidence/split.json) record zero
settled pair preparation, 23–76 emitted/intraline comparisons, 0.205–1.589 ms
construction, and 2,742,552 metric-cache bytes. Only one file and one hunk hash
scan occurred through the measured scroll. The cache retains its existing 5 MiB
budget; immutable leases remain counted after eviction. Final-verdict signature
hashing is not covered by this measured optimization.

The [metadata comparison](reading-backlog-evidence/metadata-comparison.json)
uses four alternating blocks and 120 warm reads per variant of the same fixture.
Both benchmark binaries use the same unoptimized compilation flags. The first
read in each process is recorded separately, and OS caches were not flushed.
Warm p50 fell from 178.10 to 59.43 ms; p95 from 203.36 to 80.18 ms; maximum from
236.24 to 117.79 ms. Metadata commands fell from three to one. These results
measure the bounded reader, not input-to-presentation or px0 performance.

The six final [reading journeys](reading-backlog-evidence/reading-summary.json)
pass correctness and retain per-stage traces in `output/backlog-verified-reading`.
Warm source readiness spans 89.50–146.38 ms and warm commit readiness spans
65.86–112.27 ms. There are only two samples per zoom/journey, so reported sample
percentiles do not establish a population p95. The 100 ms warm-switch target and
px0 comparison remain open. Host activity is uncontrolled; earlier misses and
all raw benchmark samples are preserved rather than averaged away.

## Settings and panel size

Settings setters skip unchanged scalar and collection values. Changed values
schedule one write after 250 ms without another change. The normal application
frame captures window/layout preferences and drains the pending save; shutdown
still forces the final write immediately. Repository relinking still saves
synchronously so failure can roll back; a failed relink preserves an earlier
pending save. Settings remain typed and use Afterhours atomic replacement.

The command-log height is saved in logical pixels. Narrow or short windows clamp
its displayed height without replacing the remembered preference. The saved
height returns when space becomes available and after a process restart.

All [28 settings tests](reading-backlog-evidence/settings-units.log) pass.
The measured setter sequence applies 60 resize/font/navigation updates with one
write; another 60 unchanged updates produce no writes. Tests also cover defaults,
clamping, migrations, immediate shutdown flush, reload cancellation, transactional
relink failure, and existing malformed/future-file protections. This measures
write counts, not filesystem latency or power-loss durability.

`nice -n 10 python3 tests/settings_layout.py --output output/backlog-settings-layout`
passes real drag input, dock collapse/expansion, temporary narrow-window clamping,
and process restart at 100%, 140%, and 200% zoom. It checks the log panel against
the fixed footer and preserves the fixture worktree. [Results](reading-backlog-evidence/settings-layout.json)
and a visually inspected [contact sheet](reading-backlog-evidence/settings-contact-sheet.jpg)
are retained. The replay uses isolated settings and the same explicit save helper
as shutdown; debounce behavior is covered separately by the unit tests. At 200%
in the short window the existing review controls leave little code space while
the log is open; the legacy compact-layout work remains open in triage.

Build: `output/backlog-settings-build.log`; application SHA-256:
`054a75f1803334579309a70d576b75e859c664c9e7a1c66287e8f704df6f6fbb`.

The [pinned-header replay on this final build](reading-backlog-evidence/sticky-final.json)
also passes at all three zoom levels, including Find and narrow windows.
