# Step 60: complete reading and navigation replay

Status: replay complete. All 75 stages have passing final results on binary
`43a32331ad09660019edf6f11b105fca0449719117bcd387cb9fdffddbbcc824`.
The 100 ms warm-switch target remains unmet. Earlier failed attempts are retained.

`tests/reading_acceptance.py` runs the baseline journey, hidden native journey,
feature regressions, startup checks, and native resize probes in sequence. It
records the executable hash, test hashes, environment, individual exit codes,
elapsed times, and logs. A failed stage does not suppress independent checks.
Creating `STOP` inside its output directory stops it between stages and preserves
completed results. The runner refuses to report success if the executable changes.

Run it after building, without another build or CPU-heavy test running alongside:

```sh
nice -n 10 python3 tests/reading_acceptance.py --output output/acceptance-new
```

Use `--stages reading reading_native` for only the baseline comparison, or choose
individual stage names from the runner. `--skip-units` is for an already recorded
full unit run. Output directories must be new so earlier failures remain intact.

`tests/summarize_reading_acceptance.py` compares the recorded baseline with the
final offscreen and native journeys. It retains individual samples, cache activity,
owned-content estimates, rendering gates, and failures. Timing starts at synthetic
input dispatch and ends at CPU completion of a matching frame; it does not measure
OS presentation. Three repeats per zoom and destination describe these runs, not
a population percentile. Cold and warm measurements remain separate.
The fixture and viewport sizes are unchanged. The final reader uses the user's
requested 17.6-pixel default code font, compared with the baseline's 16 pixels,
and the new compact headers; the rendered work is therefore not pixel-identical.

The full unit run passed all 47 suites and 475 individual tests before the final
reader-inset adjustment. Its log is `output/step60-units.log`.

The final suite, rechecks, continuation, and selection repeat contain 86 stage
attempts: 75 passing final stages and 11 earlier failed attempts. They include
69 feature journeys, both reading modes, the navigation-boundary check, startup,
and native window/resize probes. Seven legacy navigation scenarios also passed
inside the navigation-regression stage. The machine-readable
[summary](reading-navigation-evidence/step60/acceptance-summary.json) includes all
323 logged rendering samples and the original error lines.

## Right-edge spacing

The user's screenshot exposed a 24-logical-pixel right margin outside the reader's
scrollbar. The before layout ends at x=1576 in a 1600-pixel viewport. The shared
content layout now keeps its left inset and extends to the right edge. No
Afterhours change or extra scroll container is needed.

`tests/reading_width.py` checks unified diffs, split diffs, and source documents at
1600 × 1100 and 1100 × 900, with 100%, 140%, and 200% zoom. All 18 offscreen and
18 hidden native geometries passed. The code reaches the scrollbar beside the
window edge. Screenshots were inspected at 140% wide and 200% narrow. Evidence:
`output/step60-width-before`, `output/step60-width`, and
`output/step60-width-native`.

The final native probes passed six coalesced resizes and 28 matching Metal frames
at each of 1× and 2× DPI. Sixteen synchronous tracking-loop resize calls each
produced a frame before returning. Their callback work took 1.06–4.97 ms; total
resize calls took 3.63–11.07 ms. These are hidden host-window probes, not measured
physical dragging of a full review. Logs are in `output/step60-tail/native_window.log`
and `output/step60-tail/native_live_resize.log`.

## Replay corrections and retained failures

The first acceptance run was stopped between stages to apply the user's width
request. Its completed results and the interruption record remain in
`output/step60-acceptance`. It found four stale test assumptions:

- The second source opens at line 1. Checking that line 12 was fully visible
  failed at 200% even though the correct file and contents were loaded. Historical
  source and Back still require their explicit line-12 destination to be visible.
- The title fixture must explicitly choose working-tree Quick Open scope and an
  after-side diff location to create the intended historical/working file pair.
- A restored scroll offset of 0.000002 pixels requires a subpixel tolerance,
  rather than exact equality with zero, when checking menu scroll isolation.
- Resizing preserves a source location's fractional viewport position. The
  tab-drag cancellation check now verifies that anchor while still requiring
  unchanged pixel offsets for actions that do not resize the viewport.

The final suite also exposed older tree tests that entered Quick Open before its
historical path list was ready, searched a renamed working-tree path in the old
revision, and clicked the removed "Back to diff" text instead of the compact
header's return control. Their updated scripts wait for the path list, choose the
working scope explicitly, and address `full_file_back`. One title-script retry
also attempted to switch scope when it was already working tree; that setup was
corrected without changing application behavior.
Go to Line retains the current diff side, so the title fixture now clicks its
added line explicitly before opening the after revision.
The source-header blame check now explicitly selects code: a single click places
the caret after step 44 and no longer creates the selection required by that action.
Its picker also clears the retained Quick Open query before entering a new path.

The Find replay compared absolute scroll offsets while a historical source page
was still loading. Completion prepended 42 lines and compensated the offset by
the same amount; visible lines 44–82 stayed in place within a fraction of a pixel.
The check now compares visible source identities and geometry, while retaining
the unchanged-viewport and horizontal-scroll assertions. It therefore covers
opening Find during a page load rather than requiring a quiescent reader.

The first final-suite tree-keyboard run missed its 200% rendering gate at 24.61 ms
p99. The comparison-source search journey also missed at 21.51 ms p99, with a
31.72 ms maximum. Bookmark navigation missed at 43.03 ms p99. These results are
retained. Unchanged repeats passed at all three zooms: tree keyboard 3.35–5.72 ms
p99, comparison-source search 2.68–4.32 ms, and bookmarks 2.32–2.64 ms. The repeats
show variability; they do not erase the earlier gate failures.

The large-selection replay missed at 20.47 ms p99 in its 100% split-after case.
The 120-frame sample averaged 14.34 ms, with 10.88 ms in main-content construction
and 1.29 ms in rendering. Its before-side case had a similar 10.84 ms main-content
average. The source audit found whole-hunk preparation before viewport rejection
and repeated review hashes as candidates for measurement; it did not establish
a function-level cause. The original failure remains in
`output/step60-tail/selection_extent/100-split_after/journey.log`.
The unchanged repeat passed all 18 source/diff selection cases, three edge-drag
cases, cancellation, and explicit 8 MiB copy refusal. Its p99 samples ranged from
3.10 to 15.41 ms; the previously failing split-after case measured 14.54 ms.

The first three-zoom reading replay's completed samples and failing geometry are
retained. The final replay uses fresh fixtures and output directories.

## Reading journey measurements

All nine final offscreen runs and nine hidden native runs passed their destination,
visibility, cache-budget, and 20 ms p99 rendering checks. Each mode contains 90
navigation samples: three repeats at each zoom, with five cold and five warm
destinations per repeat. The largest selection-feedback sample was 10.81 ms
offscreen and 18.49 ms native, below the 50 ms target.

The following values are observed minimum–maximum ready times in milliseconds
across those nine samples per destination. Per-zoom sample p95s remain in the
machine-readable evidence. The baseline was offscreen; native values are shown
separately rather than treated as a direct before/after comparison.

| Destination | Baseline offscreen | Final offscreen | Final hidden native |
| --- | ---: | ---: | ---: |
| Cold commit | 527.0–688.9 | 334.7–552.2 | 267.8–400.8 |
| Warm commit | 355.1–737.4 | 103.3–200.9 | 106.3–180.8 |
| Cold source | 506.2–1104.5 | 475.8–673.6 | 372.2–623.4 |
| Warm source | 3.3–5.7 | 328.4–497.9 | 289.1–633.8 |
| Cold Back | 333.5–510.1 | 472.8–554.2 | 354.4–588.7 |
| Warm Back | 346.9–567.0 | 298.0–477.1 | 288.0–444.8 |

Warm source switching regressed. The old two-slot arrangement retained its source
payload while showing a commit; the new workspace keeps one active rendering
payload and reloads through the bounded cache, including revision validation.
Warm commit loading improved, but neither final mode meets the 100 ms warm-switch
target. These results do not establish smooth switching at that target.

Maximum accounted content capacity rose from 16,993 to 20,139 bytes on this
fixture; the current counter includes lexical-state storage. Maximum blob-cache
contents were 8,602 bytes, versus 4,101 before. Patch-cache contents were 56,831
bytes offscreen and 56,863 native, versus 29,719 before. Both remain within their
existing 32 MiB budgets. These small-fixture values are not process memory or a
large-file peak-memory measurement; the bounded-reader journeys check the latter
reader limits separately.

## Known limits to report with final measurements

Cache-resident switching still performs revision validation and misses the
100 ms target in this final replay.
Owned-content counters estimate vector/string capacities and exclude allocator
metadata, worker memory, GPU allocations, and process RSS. Native resize probes
exercise hidden windows and tracking-loop callbacks, not a physical mouse drag
or a compositor presentation guarantee. CJK/emoji font fallback and snapshot-view
character selection remain documented limitations in [the complete upstream-gap
ledger](afterhours-gaps.md)
and the corresponding step reports.

The user-requested [px0 research](px0-read-only-ide-research.md) compares the shared
reader capabilities, identifies useful additions, and specifies a matched
benchmark. Its published HTTP minima and server-only memory numbers are not
comparable to these frame-completion measurements. No px0 benchmark ran and no
competitive performance win is claimed.

## Retained evidence

[Step 60 evidence](reading-navigation-evidence/step60) includes the before-width
attempt, final suite, rechecks, continuation, selection repeat, all width geometry,
startup layouts, build/unit logs, and representative screenshots. Archives retain
JSON, logs, and generated input scripts; fixtures and settings are excluded and
can be regenerated with the committed runners. The baseline remains in
[step 01 evidence](reading-navigation-evidence).
All 31 packaged artifacts (76,025,150 bytes), including 10,152 archive members,
passed integrity checks. [The verification manifest](reading-navigation-evidence/step60/verification.json)
records their sizes and SHA-256 hashes, plus the stage and unit counts.

Rebuild the aggregate report with:

```sh
nice -n 10 python3 tests/summarize_reading_acceptance.py \
  --reading output/step60-final/reading \
  --native-reading output/step60-final/reading_native \
  --suite output/step60-final \
  --retry-suite output/step60-rechecks \
  --retry-suite output/step60-tail \
  --retry-suite output/step60-selection-repeat \
  --output docs/reading-navigation-evidence/step60/acceptance-summary.json
```
