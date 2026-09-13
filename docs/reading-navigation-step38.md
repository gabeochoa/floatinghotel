# Step 38: bookmarks on demand

The source header's Bookmarks menu replaces the permanent bookmark row and its
page buttons. Entries show path, line, and revision, including distinct Working
tree and Index labels. Custom labels retain the underlying location. The menu
supports arrow navigation, Enter, and scrolling through longer lists. Its own
selection highlight owns keyboard navigation; the framework does not add an
unrelated focus outline to another menu row.

Adding a bookmark no longer changes the reader's viewport height or code
position. Bookmarks retain their existing settings storage and typed source
navigation. Closed source tabs can reopen at the stored revision and line.

The first native replay found a pointer-input leak: selecting the menu's
Bookmarks entry selected the code line underneath it. Context menus now block
underlying widget input for the entire frame, including the closing frame.
Custom code selection honors that same input gate and tests clipped row
geometry. At 200% zoom, a scrolled-out row had overlapped the header in its
unclipped rectangle, so clicking the header selected invisible text. The upstream recommendation
is recorded under U5 in `docs/afterhours-gaps.md`.

A second check found that the old bookmark action used a temporary line-reveal
request after it had been cleared. With no selection it fell back to line 1.
The action now uses the document's logical reading anchor when nothing is
selected, preserving the destination after a completed line jump.

The bookmark journey passed at 100%, 140%, and 200%, including historical,
working-tree, and index identities, keyboard navigation, closed-tab reopening,
saved settings, and missing-object errors with no working-tree substitution.
Adding a bookmark and dismissing the menu leave the viewport and rendered code
positions unchanged. A 44-entry fixture renders only 34, 24, and 17 menu rows at
the respective zoom levels, and keyboard navigation reveals its final entries.
Git's staged and unstaged diffs remain unchanged through these journeys.

Source-header, focus-return, Escape, text selection, wrapped Unicode selection,
all seven navigation regressions, and the existing bookmark/Markdown journeys
passed. All checks passed again on the final binary after removing the extra menu
focus outline. The Markdown unit suite also passed all five checks.


The final steady-reader benchmarks measured 2.55, 2.21, and 2.15 ms p99 at
100%, 140%, and 200%. The preceding run measured 2.80, 2.74, and 3.34 ms.
All passed the existing 20 ms gate. These headless frame measurements exclude
cold loading and compositor timing. Cache budgets remain unchanged.

Evidence is under `docs/reading-navigation-evidence/step38`, with failures and
representative final screenshots kept separately. Archives contain layout and
workspace JSON, runner scripts, logs, and the saved bookmark values. Extract
with `nice -n 10 tar -xzf`. Rerun with `nice -n 10 python3
tests/bookmark_navigator.py --output output/bookmark-navigator`.

Final binary SHA-256:
`2e9f5b04425257d1fde10ff8d323093c9a043f4fae7b520321380583bd4cc322`.
