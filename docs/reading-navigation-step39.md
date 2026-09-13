# Step 39: compact commit details

Commit tabs open with a subject limited to two lines at 20 logical pixels and
one 32-pixel metadata row. The sticky heading keeps the short revision visible
as metadata scrolls away. Details expands the full subject, message, author,
date, parents, and refs. Its state belongs to the document: another commit
starts compact, and returning to an expanded tab retains that choice.

Merge-parent selection remains available with Details closed. Long messages
continue to use the existing virtualized reader and active-document cache.
Metadata values wrap, and narrow windows put their labels above the values.
The duplicate short hash and separate Full message control are removed.

The native fixture includes a long Unicode subject and author, a 10,000-line
message, a root commit, and a two-parent merge. The runner exercises normal and
narrow windows at 100%, 140%, and 200% zoom, disclosure, tab retention, complete
message navigation, and parent switching. It checks header/control geometry
and retains screenshots and workspace/layout snapshots.

The initial behavioral replay passed, but screenshot review found inherited
oversized metadata-card corners and a scrollbar overlapping the Details
control. The card now uses the application's explicit box radius, and the
metadata row reserves scrollbar space. Initial captures are retained separately.

Final verification passed: the three-zoom fixture, compact-header regression,
13 logical-anchor comparisons, all seven navigation regressions, both existing
full-message journeys, and 33 diff-tools unit tests. Long-message and steady
merge-reader p99 frame measurements were ['1.41', '3.32'], ['3.07', '2.59'], and
['2.70', '4.69'] ms at 100%, 140%, and 200%, respectively; all passed the 20 ms gate.
These measurements exclude cold loads and compositor timing. Cache limits are
unchanged. Native logs retain existing transient zero-size startup-layout warnings.

Evidence: `docs/reading-navigation-evidence/step39`. Archives contain layout and
workspace JSON, scripts, and logs; representative PNGs are directly viewable.
Rerun with `nice -n 10 python3 tests/commit_metadata.py --output output/metadata-check`.
Final binary SHA-256: `38e6575e19c1f3d0e86c7a712d7e1d7a62609128fcd0e2b6eed3e8b66eee90ac`.
