# Step 47: separate plain and location copy

Cmd+C and Edit → Copy request plain selected code. Cmd+Shift+C and Edit →
Copy with location retain the existing `path:Lstart-end` header. The former
copy-with-location preference is removed; old settings fields are ignored.
Both entry points share the same copy function.

The fixture distinguishes committed, staged, and working-tree text. Its selected
range includes tabs, Unicode, trailing spaces, and CRLF endings. The hidden
native replay verifies actual pasteboard contents for both shortcuts and both
menu actions in six scopes at 100%, 140%, and 200% zoom: 72 actions. All pass,
with unchanged source/index contents, destination revision, and viewport.

The baseline reproduces the old selection/location format through layout
snapshots. It does not verify the old native clipboard: offscreen Metal does not
initialize Sokol's clipboard. The first failed replay records that limitation.
The next two failed runs exposed a native test-runner assumption: simulated
update ticks do not ensure a render pass before clicking a newly visible
control. Screenshot barriers now provide that render pass.

`FH_TEST_NATIVE_HIDDEN` takes effect only with `--test-mode`. It retains native
clipboard and drawing while suppressing window presentation and activation.
The pasteboard guard stores prior items in memory, reads only fixture-marked
text, and restores prior contents if the marker remains present. It does not
provide isolation from concurrent user copying. The upstream scoped-clipboard
request and workaround are recorded in `docs/afterhours-gaps.md`.

Native hidden-window screenshots are unavailable through the macOS window
capture API. Matching offscreen journeys provide screenshots and geometry;
native journeys provide clipboard evidence. Neither substitutes for physical
mouse/compositor verification.

The 21 settings tests, native-menu unit check, native menu journey, and
three-zoom focus and keyboard-selection regressions pass. The 18 offscreen
copy journeys verify exact selected text, confirmation labels, revision badges,
and unchanged viewport geometry. The large-source keyboard regression reports
p99 rendering of 3.51–8.40 ms (largest frame 8.77 ms).

The hidden native resize regression also passes all sixteen sizes across two
DPI settings, including GPU pixel checks. Resize calls took 1.94–8.20 ms in
this run. Earlier variability remains recorded; this does not establish a
universal physical-drag frame-time bound or resolve the warm-switch target miss.

Final binary SHA-256:
`4ed7cefda37cd93b532906527f2136cdb6b4cac58fbe5d87ac85ab7fa6d1777f`.
Evidence and failed attempts are under `docs/reading-navigation-evidence/step47`.
