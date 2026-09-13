# Step 18: tree keyboard navigation

Up and Down preview visible files while keeping focus in the tree. Left
collapses a directory or moves to its parent; Right expands a directory or
enters it. Enter toggles directories and keeps files open. The file filter
keeps normal editing keys, and Tab enters the remembered tree row directly.

Rows retain path identities across virtual-list reuse. The app guides the
list to an offscreen keyboard destination before building it, then reveals
that row through its scroll ancestors after layout. Historical review trees
and working-file views retain separate navigation state. Keyboard previews
bypass the mouse double-click detector and cancel pending reader-focus
requests. Review navigation runs after row rendering so switching from a
source document cannot release the summary data mid-iteration.

The All Files row cache includes the asynchronous path-list generation. A
refresh with the same file count can therefore replace paths correctly.
The native View menu supplies Changed, Tree, and All Files modes; the older
inline view buttons are no longer rendered.

The final native replay passed at 100%, 140%, and 200%, including 23 focused
row geometry checks per zoom, folder expansion/collapse, preview/keep, file
filter editing, the three working-file modes, and historical source return.
No repository contents or index entries changed. Six tree unit tests and
54 navigation/review-target tests passed, along with the navigation ownership
check. Tree-state, focus-return, and source-origin journeys passed at all
three zooms. All seven navigation regressions and 13 logical-anchor layout
comparisons passed.

Earlier attempts exposed
ambiguous filename clicks and obsolete inline view controls in the new test.
The corrected script targets sidebar rows and native menu actions. It also
opens the Review sidebar before source, because selecting that sidebar is an
explicit return-to-review action. A later attempt used a fixed frame wait
while returning from source; the commit was still loading. The replay now
waits for the pending read before issuing another arrow. That run also
recorded a 20.29 ms p99, narrowly above the 20 ms gate
(`output/step18-keyboard-fourth/100/run.log`). The next run reached
30.27 ms. Its profile showed 1,596 entities, including controls for all 63
file headers. The renderer now culls header controls outside its existing
overscan window, while retaining header heights and anchor identities.
The final replay passed the same 20 ms gate: p99 was 8.34, 10.37, and 5.10 ms
at the three zooms. The selected code row has identical geometry before and
after culling; header controls fell from 63 to 15 at 100%. An intermediate
200% run still spiked to 74.94 ms p99 (314.31 ms maximum). It also exposed a
test assumption that the first visible row remained the first file after
switching views. The corrected test uses Up to reach the start before its
assertions. Failed timings remain in `frame-metrics.json`; the passing replay
does not establish that those timing spikes cannot recur.

Evidence: `docs/reading-navigation-evidence/step18`.
Full final replay: `output/step18-keyboard-final`.
Binary SHA-256: `aebe83ae4075d1e45dc4fae7debbaa75e4c91686b2be1ef6376c2a2820f9c03c`.
