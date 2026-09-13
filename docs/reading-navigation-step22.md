# Step 22: review-tree context menus

Review file rows offer Open diff, Open source, Keep open, and Copy relative
path. Working and staged rows also offer Reveal in tree, which opens their
matching Files scope and reveals the row. Historical rows retain the review
tree because the Files view represents the current checkout.

Actions capture typed destinations from the clicked row, including its
resolved commit and parent. Deleted files use the before revision; renamed
files retain the old path when opening that side. Callbacks check repository,
document, and navigation generation before acting. No rendering payload is
retained by a menu.

The native menu journey passed at 100%, 140%, and 200% zoom. It verifies
clicked-row targeting, menu dismissal and focus, renamed/deleted source text,
root commits, retained review trees, preview/keep, and working/index reveals.
The staged diff, unstaged diff, and status are unchanged. The p99 frame samples
were 4.06, 6.64, and 8.02 ms; the 200% maximum was 22.22 ms.

Focus-return regressions passed at all three zooms, and all 14 context-menu
unit tests and the navigation ownership check passed. Clipboard contents remain unverified
with the headless Metal backend; the existing limitation is documented in
`docs/afterhours-gaps.md`.

Evidence: `docs/reading-navigation-evidence/step22` (layout/workspace JSON is gzip-compressed).
Full replay: `output/step22-menu`.
Binary SHA-256: `8a93944d99d18d48cb1c2cdd46d091dda54f75ac43fcda4947348061661edc40`.
