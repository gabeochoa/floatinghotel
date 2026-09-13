# Step 04: preview and kept tabs

`ReadingWorkspace` owns whether a document is a preview. Opening a new preview
replaces the previous preview in the same strip position using a fresh document
ID. Kept documents remain independent. Opening source keeps its originating
review before replacement. Keep changes no request generation and cancels no
read. Alias resolution preserves kept status when destinations coalesce.
Reopen Closed explicitly keeps the restored document.

Single-click tree, history, search, and Quick Open rows use the navigation
boundary. Enter in Quick Open keeps the result; Enter after navigation keeps
the focused document tab. Double-clicking a tab keeps it. Tabs have a Keep Open
context-menu item bound to the clicked document. Preview titles have a visible
Preview prefix. The bundled fonts do not include an italic face.

The 500 ms click sequence distinguishes tab, tree, history, picker, and search
regions. File and search rows compare complete destinations; tabs compare
document identity so a changing reading position does not break a double-click.
Back/Forward and ordinary open operations break the previous click sequence.

## Verification

34 navigation-state tests and 20 review-storage tests passed. The native preview
replay passed at 100%, 140%, and 200%, covering replacement, Enter, double-click,
context-menu keep, preserved review origins, and reuse of an existing file.
It verifies document identities, at most one preview, empty inactive payloads,
focus, screenshots, and tab geometry. Render p99 was 4.13/3.78/2.92 ms.

The six-kept-tab replay passed at all three zooms. All seven existing navigation
regressions passed. Compact-close hover, commit spacing, and the unstaged/staged
feedback journey also passed; commenting left the index unchanged. The native binary was
`e234b6170d78b26cd02a14862ee8a46471b19c0e0a0bd12867e83f4fee29c13a`.
Evidence is in `docs/reading-navigation-evidence/step04`; complete native runs
remain under `output/step04-native-final`, `output/step04-tabs`, and
`output/step04-navigation`. The reusable runner is `tests/preview_tabs.py`.

## Failures corrected

The first native replay caught Enter failing to keep a preview. Resetting focus
to the root let the framework grab the first unrelated focusable control.
Navigation now requests focus by repository-owned DocumentId and applies it
when the matching tab exists. Layout dumps include actual focused controls.

A unit replay caught two different file rows inside one commit being mistaken
for a double-click. They share document identity but have different click
destinations. The corrected comparison is covered alongside cross-region clicks
and history navigation interrupting a click sequence. Original failure logs
remain under `output/step04-native-first` and `output/step04-target-row-repro.log`.
