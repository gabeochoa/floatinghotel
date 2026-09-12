# Reading priority evidence

The native runners in `tests/priority_replay.py` cover the requested priority
changes. `tests/reading_journey.py` records all navigation samples and retains
render-gate failures while continuing the remaining timing runs.

`build9-evidence.json` maps archived geometry and screenshots to their original
files and hashes. Build 9 covers all three zoom levels for close-hover pixels,
commit spacing, wrapping, selection, compact headers, tabs, and divider dragging.
`build10-evidence.json` covers unstaged/staged feedback, unchanged UI geometry
when resizing code text, and tab menus. Build 10 fixes new-file readiness.
Build 11 fixes the Untracked sidebar changing sections during navigation.

`reading-metadata.json`, `reading-samples.json`, `reading-summary.json`, and
`reading-outcomes.json` record the completed 90-sample replay on build 11.
`reading-measured-result.json` contains aggregate values. The earlier 24.19 ms
and 32.20 ms render failures remain in `earlier-render-failures.json`; the final
run passed all nine 20 ms gates. Cached switching still misses the 100 ms target.

The geometry files retain rendered named controls, reading rows, measured
rectangles, font sizes, and selection text. Full original layout dumps, logs,
and scripts remain under the recorded `output/priority-polish` paths. Unit logs
are retained here as well. This evidence verifies the priority batch, not all
60 planned commits.
