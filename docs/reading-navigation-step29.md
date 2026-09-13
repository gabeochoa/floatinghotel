# Step 29: Recent files and readable Quick Open results

Empty queries put recently visited files first, then list other files
alphabetically. The workspace retains at most 40 source destinations, with
separate working-tree, index, and resolved historical identities. Closing or
replacing a preview does not discard its recent entry. Back and Forward update
recency. Restored kept tabs seed recent entries without loading their contents.
Entries absent from the current scope's catalog are excluded.

Queries rank filename matches ahead of directory-only matches, with stable path
ties. Matching uses whole UTF-8 code points and ASCII case folding. Result labels
put the filename first, followed by a dimmed directory; highlighted characters
follow the same match used for ranking. Full paths remain in tooltips and
semantic focus identities. Existing virtualized rows and cache budgets remain.

The renderer's existing styled-ellipsis limitation still applies. Filename-first
labels keep the filename visible when a directory is long, and the list clips
the remaining path. The upstream gap is recorded in `docs/afterhours-gaps.md`.

The 29 diff-tool and 58 navigation tests pass. The native ranking journey
passes at 100%, 140%, and 200%, checking recent order, closed-file retention,
revision isolation, filename priority, deterministic ties, colored match spans,
duplicate paths, long-directory clipping, narrow geometry, at least 4.5:1 text
contrast, and fewer than 20 rendered rows for a 128-path catalog. No source or
index contents changed. Its p99 frame times were 5.03 / 4.76 / 5.00 ms.

Binary: `0489e171f7bae3db5bc8e7a96cb6da80cd8ab7b6d315245b4554895be73489bc`.
Historical-scope, overlay, restart, and focus journeys pass at all three zooms,
as do all seven navigation regressions and the navigation ownership checker.
The restart journey checks the restored source's recent position alongside
lazy loading and exact reading anchors.

One 200% overlay setup clicked a filtered tree row before it appeared. The test
now waits for its visible filename and captures the filtered row before the
click. The failed log and subsequent layout remain under `failures` in
`docs/reading-navigation-evidence/step29`. That directory also contains the
ranking screenshots/layouts, restored recent-list screenshots, unit results,
and regression logs. Full replay outputs are under `output/step29-*`.
