# Step 31: Retained repository search

Repository search uses the sidebar while the current source or review remains
visible. Closing the pane returns to the existing Review/Files sidebar.
Cmd+Shift+F reopens and focuses the retained query. Query, results, scope,
selected match, and logical scroll position remain repository-owned.

A search keeps the revision and review origin captured when it first opens.
Opening matches or activating another tab does not change that scope.
Options provides an explicit Use current document action. Existing regex,
case, whole-word, include/exclude glob, and changed-file controls remain there.
Search results retain the existing 5,000-match and 4 MiB limits. The pane uses
the existing zoom-aware virtual-list adapter.

Search jobs use their own repository/query/generation identity so document
navigation does not discard a matching result. Opening a match still uses the
shared navigation boundary and retains its originating review. Focus identities
for Search and its temporary preview belong to the repository, allowing focus
to return after a document change.

Verification passed: ten repository-search unit tests; retained search and
comparison-origin journeys at 100%, 140%, and 200%; all seven navigation
regressions; changed-file, matching-mode, and glob-filter regressions. Focus and
Escape journeys also passed at all three zooms. Native geometry checks verify
that the reader and search pane remain visible without overlap, including the
1100 × 800 window at 200%. Empty repository tabs ignore the search shortcut.
The 240-match fixture renders fewer than 50 result rows. Git status is unchanged.

The retained-search render p99 values were 2.98, 3.89, and 5.23 ms; comparison
journeys measured 6.89, 8.77, and 3.75 ms. All pass the 20 ms gate. These measure
rendering, not cold search latency. Logs, selected screenshots, compressed layout
and workspace captures are in `docs/reading-navigation-evidence/step31`.

The first 100% replay found that all-file searches still inherited the stored
changed-file path restriction. That restriction is now cleared when changed-only
scope is off. Failed evidence is retained under `failures/all-files-scope`.

The 200% test setup now activates off-strip tabs through the open-tabs menu.
The older repository-search regression waits for source readiness before
checking its target highlight. Captures showed the title had updated while
the bounded file read was still pending. Both failed attempts are retained.

The comparison-origin journey exposed an existing lifetime error in Open file:
navigation releases the comparison diff, but its renderer continued iterating
that vector. The debugger stopped in `file_header_label` on the next file. The
renderer now returns immediately after source navigation. No extra content
buffer is retained. The debugger backtrace and reproducer are archived under
`failures/comparison-open`; the fixed journey passes at all three zoom levels.
