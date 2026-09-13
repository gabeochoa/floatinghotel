# Step 23: compact directory chains

Single-child directory chains occupy one row, such as `src/ui/components`.
Compaction stops at branches. Full paths remain row identities and tooltips;
file indentation follows the visible hierarchy. Prefix typing matches the
path displayed on the compact row.

Folding any intermediate directory keeps the compact row collapsed. Expanding
it clears those intermediate folds, including when filtering changes the
visible hierarchy. Keyboard Left/Right move through visible compact rows.

All 16 tree unit tests pass. The native compact-tree journey passes at 100%,
140%, and 200% zoom, including duplicate filenames, filtering, collapsed
chains, keyboard entry, type-to-select, and the Files tree. The Git index
and working diff remain unchanged. The p99 frame samples are 5.17, 3.53,
and 5.18 ms respectively.

The first replay expected an expanded child near the bottom of the viewport
to be fully visible immediately. The replay now enters that child with Right
Arrow before checking its visibility. This preserves ordinary tree scrolling;
explicit keyboard navigation reveals the child. The earlier attempt is
retained in `output/step23-compact`.

Existing keyboard, reveal, and review-menu regressions pass at all three
zooms. All 27 diff-tool tests pass, for 43 unit tests in this step.

Evidence: `docs/reading-navigation-evidence/step23` (layout/workspace JSON is
gzip-compressed). Full replay: `output/step23-compact-final`.

Binary SHA-256: `ccd0729cbce99f6c5f933037ac18985badb62855d63fe8de9be7d5be9220959c`.
