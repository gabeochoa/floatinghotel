# Step 55: dock reading state and native resizing

Dock transitions preserve the active document, selection, logical reading
anchor, and remembered reading width. A collapse no longer overwrites that
width while a previous window resize is still queued. Resizing continues to
coalesce between Metal frames without animation.

An isolated hidden-native test option enables the production dock resize path,
which ordinary E2E tests intentionally disable. Native size commands wait for
the actual window dimensions with a host-frame barrier and a ten-second timeout.
The replay covers repeated toggles, manual expanded and dock resizing, rapid
toggles, reading selection, anchors, and fixed-footer geometry at all three zooms.
Offscreen runs capture the corresponding visual states.

Final binary: `1a16624d85427b0289691c0b2ba816647be8472963c403ab160923ac72d8fe0f`.

Six layout unit checks passed. Native and offscreen journeys passed at 100%,
140%, and 200%, including exact line geometry after returning to the reader.
The native p99 samples were 4.28, 3.54, 5.93 ms (20 ms gate). Saved window
sizes and splitter placement survived restart. Escape, nested temporary UI,
drafts, document closure, and retained-reading regressions passed at all zooms.

The Metal dock probe passed 56 matching drawable frames at native/Retina scale,
including coalesced requests and no dimension changes inside a draw callback.
The live-resize probe passed 16 synchronous frames with pixel validation. Draw
callbacks took 1.1–5.4 ms; complete window operations took 3.3–15.3 ms in this
run. These are individual hidden native-window samples, not physical mouse
input or a guarantee about every compositor frame. The app replay exercises
production dock sizing; the separate live-resize probe uses the app-built
Metal backend object with its own rendering callback.

Screenshots were inspected for dock, expanded reading, selection, and footer
placement. `docs/reading-navigation-evidence/step55` contains 20 artifacts,
3,165,500 bytes; all compressed members and PNG signatures verified.
