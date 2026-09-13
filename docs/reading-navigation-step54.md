# Step 54 — bounded immutable commit prefetch

Verification is in progress.

A stable commit-row hover waits 150 ms, then warms the existing 32 MiB patch
cache on the background executor. Moving away cancels it. Repeated hover on the
same candidate does not resubmit. Repository and diff-option changes reset the
intent. Only resolved object IDs are accepted.

The repository owns candidate and completion state. The future carries a boolean,
so completed prefetches retain no second patch payload. A process-wide admission
permit survives repository-tab closure until the worker or cancelled queued job
drains. Thus rapid movement and closed tabs cannot build a prefetch queue. The
existing executor still reserves foreground capacity.

Keyboard selection already opens a foreground preview. It suppresses the
stationary mouse's old hover candidate until the pointer moves, avoiding duplicate
prefetches for the selected document. Hover alone never navigates or expands dock
mode.

Activation still validates objects and shallow boundaries through Git. Existing
missing-object and shallow-deepening tests prohibit bypassing that validation.
The native runner separates ordinary Git timing from delayed cancellation tests.
Final binary: `5ae8581cb8d7046ef77fc4534cf46218c35783ac90ee88d18b20a69a841f62fe`.
All 13 focused checks pass: stable intent, cancellation admission, worker
reservation, real Git patch/cache behavior, missing objects, shallow boundaries,
and the queued-job limit across closure. Native journeys pass at all three zooms
for hover without navigation, cache-hit clicks, delayed cancellation, rapid
traversal, and keyboard foreground priority.

| Zoom | Cold click | Prefetched click | Selection feedback |
| --- | --- | --- | --- |
| 100% | 410.46 ms | 140.20 ms | 5.43 ms |
| 140% | 340.79 ms | 139.92 ms | 5.65 ms |
| 200% | 438.90 ms | 155.76 ms | 5.76 ms |

These are individual ordinary-Git samples, not p95 estimates. The 100 ms warm
switch target remains unmet. Separate delayed-wrapper journeys prove
cancellation but are excluded from this timing comparison. Their click times
were 222–414 ms. All nine feature frame gates passed: p99 3.41–13.81 ms,
maximum 19.43 ms.

The first build caught a pointer-type namespace error. A native run caught a
missing host-frame barrier in the new wait command; it now uses the same barrier
as the existing reading/filesystem waits and fails explicitly after ten seconds.
The accelerated tick loop must yield to real rendering and worker completion.
Repeated-selection and delayed-loading regressions passed at all three zooms.
Evidence package: 22 artifacts, 1,944,335 bytes; all compressed members read successfully.
See `docs/reading-navigation-evidence/step54`.
