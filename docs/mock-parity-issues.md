# Review-focus discrepancies

Reference: `docs/mocks/review-focus.html` and
`output/review-focus/mock-1440.png`. Native baseline:
`output/mock-parity/baseline/commit.png`. The baseline includes the separately
verified background fix. Its failing-before evidence is the earlier spacing
capture recorded in `docs/mock-parity.tsv`.

Each row describes an existing mismatch, not an already implemented feature.
User overrides listed in the plan take priority over the mock.

| ID | Existing mismatch | Target | Status |
| --- | --- | --- | --- |
| 01 | Outer content gutter exposes RGB 30/30/30 | Active window background | Addressed |
| 02 | Commit tabs reserve 220 pixels | Content-sized tabs with a width cap | Addressed |
| 03 | Tab underline spans the entire tab | Inset two-pixel indicator | Addressed |
| 04 | Commit tab has no commit marker | Small outlined commit marker | Addressed |
| 05 | Source tab has no file-type marker | Muted monospace type marker | Addressed |
| 06 | Review/Files buttons lack an enclosure | One outlined segmented group | Addressed |
| 07 | Review/Files navigation lacks symbols | Small consistent symbols beside labels | Addressed |
| 08 | Selected navigation segment barely contrasts | Raised selected surface | Addressed |
| 09 | Repository title has no weight hierarchy | Bold title, muted branch | Addressed |
| 10 | Sidebar headings look like row text | Quiet uppercase bold headings | Addressed |
| 11 | History branch runs into its heading | Separate right-aligned branch | Addressed |
| 12 | Changed-files header omits viewed progress | Scoped viewed/total count | Addressed |
| 13 | File filter is a heavy filled pill | Transparent field with bottom rule | Addressed |
| 14 | File filter has no search marker | Small search icon | Addressed |
| 15 | Folder disclosure uses literal v/> strings | Dedicated chevron column | Addressed |
| 16 | Review files have no type markers | Monospace file-type column | Addressed |
| 17 | Selected file touches sidebar edges | Eight-pixel outer inset | Addressed |
| 18 | Selected file has square corners | Five-pixel corner radius | Addressed |
| 19 | Mixed changes hide deletion totals | Both addition and deletion counts | Addressed |
| 20 | Tree change colors are harsh | Soft diff green and red | Addressed |
| 21 | Tree counts use proportional text | Monospace count columns | Addressed |
| 22 | Working-tree indentation uses spaces | Shared logical-pixel indentation | Addressed |
| 23 | Commit header begins against tabs | Deliberate top breathing room | Addressed |
| 24 | Eyebrow only says Commit history | Review context with retained back action | Addressed |
| 25 | Long subject is always cut off | Bounded two-line title | Addressed |
| 26 | Metadata prioritizes SHA over author | Author, relative date, then SHA | Addressed |
| 27 | Metadata fields use only doubled spaces | Explicit gaps and separators | Addressed |
| 28 | Compact date is ISO-only | Relative date, full timestamp in tooltip | Addressed |
| 29 | SHA uses ordinary UI text | Separate monospace SHA | Addressed |
| 30 | Compact header hides decorations | Compact HEAD/branch badge | Addressed |
| 31 | Description shows only its first line | Bounded wrapped paragraph | Addressed |
| 32 | Details disclosure looks like a primary action | Quiet low-emphasis button | Addressed |
| 33 | Toolbar totals have one flat color | Separate colored count spans | Addressed |
| 34 | Summary says 1 files changed | Correct singular form | Addressed |
| 35 | Unified/Split look unrelated | One segmented control | Addressed |
| 36 | All toolbar spacing is the same cramped gap | Separate internal and group spacing | Addressed |
| 37 | Feedback omits scoped comment count | Count in the action label | Addressed |
| 38 | Feedback lacks a comment marker | Small comment symbol | Addressed |
| 39 | Finish review lacks check/weight hierarchy | Check marker and bold action text | Addressed |
| 40 | File header is the same surface as sidebar | Raised file-header surface | Addressed |
| 41 | Diff path uses proportional text | Monospace path | Addressed |
| 42 | Directory and filename have equal emphasis | Muted directory, bright basename | Addressed |
| 43 | Stats and status clutter the path label | Separate change-count cells | Addressed |
| 44 | Viewed looks like a transient command | Persistent checkbox state | Addressed |
| 45 | File actions are tiny filled pills | Quiet controls with 28-pixel targets | Addressed |
| 46 | Files have only eight pixels between them | Fourteen-pixel separation | Addressed |
| 47 | File endings lack context information | Quiet context and language footer | Addressed |
| 48 | End of commit is indistinguishable from blank space | Explicit end/progress marker | Addressed |
| 49 | Hunk range and function have equal emphasis | Separate range and muted context | Addressed |
| 50 | Hunk caption competes with code | Smaller readable monospace caption | Addressed |

Not included in these fifty: compact single-child folder chains, a new filter
shortcut, a full enclosing diff-card border, and moving context expansion to a
leading icon. These need behavior or rendering decisions beyond the selected
style corrections. Existing context expansion stays clearly labeled.

## Closure evidence

All fifty rows are addressed. The native checks passed against the final
implementation in `output/mock-parity/verified-checks.log`. Each capture has a
JSON dump beside it with actual rectangles, spacing, fonts, and text colors.

| IDs | Commit | Native evidence | Repeatable check |
| --- | --- | --- | --- |
| 01 | `7ec480d` | `verified/mock_parity/commit.png` | `tests/check_panel_background.py` |
| 02–09, 33–39 | `38f8129` | `verified/mock_parity/commit.png`, `source.png`, `narrow.png` | `tests/check_mock_controls.py` |
| 10–22 | `daaa1ed` | `verified/mock_parity/tree_selected.png`, `tree_collapsed.png`; `verified/mock_tree_working/working_tree_selected.png` | `tests/check_mock_tree.py`, `mock_tree_working.e2e` |
| 23–32 | `9994845` | `verified/mock_parity/commit.png`, `details.png`, `narrow.png` | `tests/check_mock_heading.py` |
| 40–50 | `d9b5340` | `verified/mock_diff/diff_default.png`, `diff_viewed.png`, `diff_folded.png`, `diff_narrow.png`, `diff_split.png` | `tests/check_mock_diff.py` |

Capture paths in this table are relative to `output/mock-parity/`. The checks
cover structure and behavior; visual inspection against the mock covers the
appearance choices. These are not fifty independent pixel-difference tests.
The styled-caption clipping workaround and remaining rendering limits are
recorded in `docs/afterhours-gaps.md`. This pass does not claim pixel-perfect
parity.

Issue 46 also has an exact two-file check in `tests/check_mock_file_spacing.py`,
using `verified/mock_multi/multi_both_folded.json`. The final regression pass
verified visible new-file and unresolved-comment state after the path/count
split. See `docs/mock-parity-results.md` for the complete result and rerun notes.
