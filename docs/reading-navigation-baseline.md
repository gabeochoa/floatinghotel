# Reading-journey baseline

Step 01 verified on 2026-09-12 against `4ef6c4e` plus diagnostic instrumentation.
The application navigation behavior is unchanged. Steps 02–60 remain unimplemented.

Run `nice -n 10 make -j2`, then `nice -n 10 python3 tests/reading_journey.py`.
The runner defaults to a fresh timestamped output directory and refuses to overwrite
an explicit existing directory. It creates a deterministic two-commit fixture,
runs cold and repeated journeys at 100%, 140%, and 200%, and saves PNGs, layout
JSON, navigation samples, source hashes, source snapshots, a tracked diff, and
build identity. This baseline used three repetitions at each zoom, 90 samples.

The journey clicks a commit and changed file, opens historical source, opens a
second working-tree source through Quick Open, then uses Alt+Left to return.
The warm journey repeats after selecting another commit. Every checkpoint waits
for matching rendered content before the next input. Checks cover actual source
path/revision, review subject, source contents, a fully visible first code line,
tab geometry, PNG output, and both existing 32 MiB cache budgets.

## Evidence

Final native captures, scripts, logs, and source snapshots are in
`output/reading-navigation/baseline-rendered`. Nine runs passed without E2E errors.
Raw samples, per-zoom summaries, metadata, render timings, and the upstream audit's
15 unit-test results are retained in `docs/reading-navigation-evidence`.
The 19 content-reader and four commit-patch tests also passed after fresh builds;
logs are retained as `content-unit.log` and `patch-unit.log` in the evidence directory.
The existing Back/Forward E2E journey also passed; its log is
`output/reading-navigation/existing-navigation-updated.log`.
The final optimized build log is `output/reading-build-visible-heading.log`.

`selection_ms` measures synthetic input dispatch to CPU completion of the first
rendered frame with matching destination state and a matching visible heading.
`ready_ms` additionally requires the matching content read to finish. These values exclude screenshot readback and OS
presentation. They do not establish physical input-to-photon latency.

Cold means the first journey in a new process. Warm means the repeated journey;
Back can hit caches in either cycle, and working-tree reads bypass the immutable
blob cache. Every sample includes per-step cache hits/misses and cumulative bytes.
Three samples per zoom and step establish a repeatable local baseline, not a
stable population p95. The summary uses nearest-rank sample p95 and retains maxima.

## Observed timing

Ranges below include all three zooms and three repetitions. All values are ms.

| Cycle | Step | Selection range | Content-ready range |
| --- | --- | ---: | ---: |
| cold | commit | 4.02–8.57 | 527.02–688.86 |
| cold | diff | 3.59–5.81 | 3.59–5.81 |
| cold | source | 4.01–6.23 | 506.24–1104.52 |
| cold | second | 4.92–8.65 | 4.92–88.17 |
| cold | back | 2.86–4.77 | 333.47–510.09 |
| warm | commit | 2.57–5.31 | 355.14–737.40 |
| warm | diff | 2.90–6.10 | 2.90–6.10 |
| warm | source | 3.26–5.74 | 3.26–5.74 |
| warm | second | 4.56–7.99 | 4.56–7.99 |
| warm | back | 3.07–5.59 | 346.93–567.01 |

All nine 120-frame render checks passed the existing 20 ms p99 gate. Their p99
values ranged from 2.34 to 5.54 ms. The slowest individual frame was
17.29 ms; the p99 gate does not mean every frame was below 20 ms.

Selection samples were below 50 ms. Repeated commit loading and Back exceeded the
100 ms cache-resident switching target despite cache hits. Git metadata resolution
still runs around cached content; the command timings are retained in each run log.
These are baseline shortcomings, not accepted final performance.

Maximum accounted content capacity was 16,993 bytes. Blob and patch cache
occupancy peaked at 4,101 and 29,719 bytes. Content capacity is an estimate of owned
vectors and strings across current, staged, source, and commit payloads. It excludes
allocator overhead, set nodes, in-flight workers, GPU resources, and RSS. No cache
budget was increased.

## Rejected runs and remaining coverage

- `probe` exposed a missing `PendingE2ECommand::retry` call in the new adapter.
- `probe-retry` showed that retrying does not prevent the runner from dispatching
  later commands. An app-owned checkpoint now pauses dispatch while frames continue.
- `probe-barrier` reached the destinations, but the initial Python content assertion
  omitted the rendered gutter prefix. The assertion now checks the code suffix.
- `probe-zoom` demonstrated that the Files controls clip `beta.cpp` completely at
  140%. Its `cold_files.json` reports zero visible height for that row. The final
  baseline uses Quick Open for the second source. This mouse-path failure remains
  outstanding; using another path does not fix it.
- `probe-picker` passed all zooms during compilation and is excluded from final
  timing claims. `baseline` passed 90 samples, but its geometry check was then
  strengthened to distinguish rendered registry entries from actually visible rows.
  `baseline-verified` passed that stronger check. A final review then required a
  matching visible heading before recording latency, rather than relying on
  destination state alone. `baseline-rendered` is the final replay with both checks.
  Earlier successful summaries are retained in the evidence directory, including
  a 4,516.90 ms repeated-commit ready sample and a 45.06 ms frame in `baseline`.
  They use the earlier
  timing predicate and are not substituted for the final 90 samples.
- The existing navigation script targeted the old `review_progress` wrapper in
  the default sidebar. It failed there; the log is `existing-navigation.log`.
  The updated script opens Files and activates `working_review_toggle`, with a
  render checkpoint. That actual Back/Forward and working-review journey passed.

The checkpoint limitations and nine source-audited upstream candidates are in
`docs/afterhours-gaps.md`. The candidates extend existing Afterhours APIs where
possible; native/UI proposals are not presented as reproduced runtime defects.
This baseline does not cover the full acceptance fixture matrix, dock behavior,
missing revisions, out-of-order reads, or the remaining implementation steps.

## Timing correction discovered during step 02

The baseline probe used `commit_subject`, which identifies the sidebar commit row. Step 02 exposed a renderer routing failure while that sidebar row still matched. The existing baseline screenshots remain evidence of the rendered journeys, but the commit timing predicate did not prove that the reader heading was visible on its first recorded frame. Treat those commit selection timings as state-plus-sidebar measurements. Source timing used the reader's `full_file_revision` heading. The step-02 probe now requires `commit_detail_subject`, and its layout checks assert that reader heading too. A direct commit timing comparison must account for this stricter predicate.
