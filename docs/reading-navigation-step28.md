# Step 28: Revision-aware Quick Open

Quick Open derives its default scope from the active source or the after revision
of a review. Its header offers an explicit working-tree switch. Historical path
reads run through the existing bounded executor and resolve symbolic revisions
before returning paths. Opened historical source documents retain the resolved
object ID; index files remain separate from working-tree files.

The active picker owns one cancellable path-list task. Repository, document,
request key, navigation generation, and data generation must still match before
results are applied. Scope changes clear the previous list immediately. Closing
the picker releases its catalog while preserving the selected path for the same
repository/scope/query. Historical output is limited to 4 MiB with a visible
limit notice; existing content cache budgets are unchanged.

Two real-Git unit tests pass, covering typed default scopes, merge/comparison
after revisions, historical renames/deletions, Unicode and newline paths, index
versus working contents, missing objects, cancellation, and bounded output.
Native scope, delayed-result, focus, shortcut, and overlay journeys pass at
100%, 140%, and 200%, including narrow windows. They verify actual source
contents and resolved revision identities, switch scopes during delayed reads,
reuse an open working-tree document, cancel on repository changes, and confirm
that neither staged nor unstaged contents change.

Async list arrival initially stole focus from the Working tree scope button.
List reveal now preserves focus; only keyboard list movement returns focus to
the query. The regression focuses the scope button while loading, waits for the
list, then presses Enter and checks that the working-tree catalog opens.

The 200% journey returns through the visible history row because its older tab
has scrolled out of view. Older picker tests now wait for historical loading
before Enter. The original failures remain in the evidence directory.

Binary: `d9aa99c97193d055038a9eb2ddfd835f73cc414731f337ec6f72261253926699`.

| Journey | p99 at 100% / 140% / 200% |
| --- | --- |
| Revision scopes | 2.14 / 2.55 / 3.21 ms |
| Overlay and narrow window | 3.54 / 3.81 / 4.92 ms |
| Focus return | 3.52 / 3.35 / 5.71 ms |
| Shortcut ownership | 3.40 / 7.24 / 4.43 ms |

Escape passes at all three zooms (p99 2.55 / 4.10 / 4.40 ms). All seven native
navigation scenarios and the navigation ownership checker pass. The boundary
scenario now explicitly selects Working tree, preserving its original purpose
of opening working content while retaining a historical review. Screenshots,
compressed layout/workspace snapshots, scripts, logs, and the original failures
are in `docs/reading-navigation-evidence/step28`. No new Afterhours workaround
was required; the async focus correction belongs to the app's picker.
