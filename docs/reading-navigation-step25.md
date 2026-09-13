# Step 25: commit-history keyboard preview

Up and Down preview adjacent commits while focus stays in history. The selected
row is revealed inside the virtual list; Enter keeps the review tab. Mouse
selection also retains history focus, allowing arrows immediately after a click.
Preview requests use the existing navigation and asynchronous content boundary.

The first native replay passed history movement, preview replacement, Enter,
first/last bounds, distant row visibility, and final content after rapid input.
It caught Down Arrow leaving the file filter and selecting a tree row. The
framework checks directional ownership only on the focused child, which the
text-input widget does not mark. The app now marks focused text controls at
frame start; the upstream gap is recorded in `docs/afterhours-gaps.md`.
The failed attempt is retained in `output/step25-keyboard`.

The corrected history journey passes at 100%, 140%, and 200% zoom, with
screenshots and geometry checks for focus and offscreen row reveal. Final
content matches the last commit selected after rapid input. The Git index
and unstaged diff remain unchanged. Frame p99 samples are 5.36, 2.97, and
3.27 ms, respectively. Nine focus-routing unit tests pass.

Evidence: `docs/reading-navigation-evidence/step25` (raw JSON is gzip-compressed).
Full replay: `output/step25-verified-commit_keyboard`.
Binary SHA-256: `85ed77023f53ca5e7e5266776a7b0a6176a99b0734fc1473ac77e8eebfb5ff08`.

Focus-return, shortcut-routing, and tree keyboard journeys pass at all three
zooms. All seven existing navigation regressions and the navigation ownership
checker pass.
