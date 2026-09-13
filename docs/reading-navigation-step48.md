# Step 48: selections beyond rendered rows

Completed and verified. Each document owns selection endpoints as source path,
diff side, line, and decoded column, plus the source destination and version.
Rendered entity IDs serve hit testing and geometry. Selection survives page
loading, layout changes, closing, and reopening.

Copy reads the selected source range on a worker through the existing bounded
reader. Pages check source identity; completion checks repository, document,
request, and selection before writing the pasteboard. Output above 8 MiB produces
an explicit error with no partial copy. Changing the selection, document, or
repository cancels pending work. Edge dragging scrolls while retaining endpoints
whose rows have left the renderer.

## Verification

- The step-47 executable reproduces the missing-endpoint copy failure in eighteen
  scope/zoom cases.
- All eighteen final hidden native cases pass at 100%, 140%, and 200% zoom:
  working-tree, index, historical source, unified diff, and both split sides.
  Each compares the actual private pasteboard with the complete 5,000-line source
  version, including Unicode, tabs, CRLF, and a final line without a newline.
- Edge dragging passes at all three zooms after the anchor leaves the rendered
  rows. Font changes, resizing, closing, and reopening retain logical endpoints.
  Document/repository cancellation and 8 MiB refusal preserve a copied sentinel.
  Git contents remain unchanged.
- Nine offscreen screenshot journeys pass. Geometry checks pass for 66 native and
  33 offscreen snapshots: viewport clipping, gutter exclusion, selected lines,
  and split-side isolation. Source-drag and narrow split screenshots were also
  visually inspected.
- All eleven existing regression runners pass: selection gestures, keyboard
  selection, 72 native clipboard actions, bookmarks, per-document Find, source
  origins, focus return, shortcut focus, line navigation, seven history/navigation
  scenarios, and the 4,000-line source rendering gate.
- 61 targeted unit checks pass across bounded copy, navigation/diff tools, metrics,
  byte-cache ownership, and token caching. Cache replacement, eviction, and rehash
  checks also pass with AddressSanitizer and UndefinedBehaviorSanitizer.

The verified binary SHA-256 is
`948976a3f2c6a47f18480431c8372f03ecbd138c3a754700f2e960579655e9a3`.

## Failures found during the replay

Copying code left the framework's TextCopy action pending. Opening Quick Open
then copied its selected filename over the code. The reader's focus boundary
now consumes the matching action; the cancellation and clipboard regressions
verify the fix.

The large unified diff initially failed at p99 203.60 ms. The old committed build
also fails at 183.79 ms. A stack sample from the real offscreen renderer traced
most preparation time to repeated Unicode line-break calculations after cache
eviction. The LRU stored each text key twice. Its lookup now borrows the stable
entry-owned key and removes the lookup before releasing that entry.

That first correction passed at 100% but still failed at 200% split, p99 194.85 ms,
because narrower columns retain more wrap offsets. Wrap keys now use the existing
published file identity, side, and line, along with width, font size, and whitespace
mode. Key strings and offset vectors release unused capacity when cached. Both
10,000-row and narrow-column unit replays reuse every measurement on their second
traversal. The 3 MiB wrapping, 2 MiB signature, and other existing cache budgets
remain unchanged.

The final eighteen frame benchmarks span p99 3.39–17.50 ms; the slowest individual
frame is 23.84 ms. Earlier failures and outliers remain in the evidence. These are
hidden native/injected-input results, not a claim that every physical resize
frame takes less than 16 ms. Cold loading and warm navigation remain separate
acceptance measurements for step 60.

## Evidence and framework gaps

`docs/reading-navigation-evidence/step48` contains failed and passing replays,
geometry, timing, stack samples, unit/build logs, and six representative PNGs.
`final3.tar.gz` is the final native matrix; `snapshots.tar.gz` contains the
corresponding offscreen layout evidence. `timing.json` inside the native archive
records each benchmark separately. The other named archives hold regression
runners. `tests/selection_extent.py` and `tests/check_selection_geometry.py`
reproduce the behavior and geometry checks.

Native clipboard tests use a unique named macOS pasteboard. Hidden test mode
redirects NSPasteboard's general-pasteboard lookup inside the test process, so the
actual Sokol backend writes the private board. The helper clears and releases it
on exit without reading or replacing the user's clipboard. Offscreen rendering
remains separate because it has no Sokol clipboard backend. Upstream candidates
for source-backed selection, scoped clipboard testing, action consumption, and
bounded-cache ownership are recorded in `docs/afterhours-gaps.md`.
