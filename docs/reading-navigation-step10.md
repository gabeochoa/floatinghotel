# Step 10: restore kept reading tabs

Kept document destinations, order, active document, subjects, MRU order, and
reading anchors are saved through Settings. Previews are excluded; when the
active document is a preview, the most recent kept document becomes the saved
active tab. Repository sessions restore when that repository becomes active.
Inactive document records contain no rendering payloads.

Anchors identify the path, revision, diff side, line, decoded character column,
and fractional viewport position. The diff layout walk records them without
reading transient entity geometry. A restored source requests the page around
its saved line; after content and initial repository refresh are ready, the
layout walk restores its viewport position. Missing source objects retain their
historical identity and show the existing unavailable/error view.

This adds session anchor restoration. Step 13 still owns replacement of the
ordinary pixel-based navigation restoration, folding/layout transitions, and
explicit-scroll cancellation across all pending navigation.

## Verification

The native save/restart journeys passed at 100%, 140%, and 200%. They verify
kept order, preview exclusion, active fallback, lazy cache activity, exact
logical source/review anchors, immutable historical bytes, unavailable source
and review objects, and explicit unresolved-reference resolution. The first
active source restores at the default zoom; inactive source and review tabs
are activated at their saved zoom. Geometry checks compare the saved line and
viewport fraction within two pixels.

Four session tests, 44 navigation tests, 21 settings tests, and 23 content-reader
tests passed. Window restoration, all seven navigation regressions,
close/reopen, and recent-tab switching passed. The 4,000-line source replay
retained bounded rendered rows and reused metrics, with p99 6.55 ms. Final
restart replay p99 values were 14.12, 7.51, and 6.04 ms at the three zooms.
Earlier timing failures remain recorded below.

Binary SHA-256:
`cda1be1391e6ef9d833102a716227c434b7a0fde18459a547754e10a9528a996`.
Screenshots, geometry, saved settings, and cold-load cache/owned-byte metrics
are in `docs/reading-navigation-evidence/step10`. Full logs are in
`output/step10-acceptance-*` and `output/step10-picker-current`.

The older Quick Open regression checked loaded contents after ten frames while
the asynchronous read was still pending. It now waits for the read through the
existing refresh-wait command. Its corrected replay passed.

The initial save replay used the source scroll debug name for a commit review;
commit reviews have a containing scroll view. The first relaunch exposed an
initial-refresh race: a working-file read could restore before Git refresh,
then reset when the refreshed source version loaded. Session restoration now
waits for that refresh. The timing probe also needs an input event to start its
clock, and historical source menu labels include their revision badge.

The next replay caught the existing line-jump action overwriting the anchor's
viewport fraction in the same frame. Anchor restoration now waits until that
jump completes. The missing-object case also caught `source()` returning the
most recent source instead of the active document; it now reads the active
source first. Saved active documents become the most recently activated entry.

A saved unresolved revision query is unavailable until the reader explicitly
chooses Resolve revision now. This prevents an unresolved branch name from
silently selecting newer contents after restart. Resolved destinations retain
their object IDs and need no such action.

The window-size regression then exposed a conflicting persisted value: a saved
review-open flag overrode a later saved dock state. Reading sessions now store
document state only; the existing window settings determine the window mode.
The native window-size replay covers that precedence.

Two runs of the previous binary exceeded the 20 ms p99 gate: 25.28 ms at 140%
and 26.87 ms at 200%. A repeat of the same binary measured 18.33 ms at 100% and
15.33 ms at 140%. These failures are retained in `output/step10-final` and
`output/step10-replay2`; they are not averaged out of the results.

At 200%, restoring into a larger default-zoom viewport exposed insufficient
context before the saved source line. Targeted reads can now include leading
lines while retaining the 256 KiB and 4,096-line limits. Prefix bytes use at
most half the page budget so a long preceding line cannot consume the target's
space. Page cache keys include the requested context. The content-reader tests
cover long prefixes, missing targets, and distinct context requests.
