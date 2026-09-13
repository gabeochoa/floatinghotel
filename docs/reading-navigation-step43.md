# Step 43: local hunk context

Hunk menus and the Expand context button request 20 unchanged lines above or
below one hunk. The original patch, hunk identities, feedback, and folds remain
unchanged. Adjacent expansions share their overlapping lines, and applying
context preserves the current logical reading position.

One worker reads bounded before/after source ranges and checks that their text
agrees. Renames use the old path on the before side. The active context runtime
holds at most 256 KiB of text and 4,096 lines; excessive or partial long lines
produce a visible notice directing the reader to source. Each document retains
only requested counts. Navigation releases inactive payloads, superseded requests
are cancelled, and results require matching repository, document, request,
workspace generation, and data generation. Existing blob-cache budgets remain
unchanged. The legacy saved-snapshot view, which already lacks source controls,
does not expose these revision-backed context actions.

Verification passed at 100%, 140%, and 200% for working, commit, and comparison
patches, split view, exact 20-line ranges, overlap removal, unchanged feedback and
folds, source return, cancellation on navigation, and keyboard navigation after
expansion. Adding leading context kept the original reading line within one
pixel. Forty unit checks cover renamed historical and staged content, Unicode,
EOF, missing/changed sources, cancellation, long-line limits, range boundaries,
context lifecycle, and existing diff/navigation behavior. Hunk chrome, folds,
source origins, 13 anchor-layout comparisons, change navigation, selected-file
review, and all seven navigation regressions passed on the final binary.

The checks caught an EOF cursor mistaken for an incomplete fragment and a 200%
assertion that confused loaded lines with virtualized rows. Review then found
that failed reads must not suppress neighboring context. Focus inspection caught
a separate 200% failure: when the invoking hunk left the rendered region,
framework focus fell back to a repository tab. Applying context now transfers
existing code focus to the stable Code region while leaving inputs and other
regions alone. The replay verifies both focus and a subsequent j command; the
framework limitation is recorded in `docs/afterhours-gaps.md`.

Forty expanded lines use 914 text bytes and 3,164 owned bytes in the local fixture;
opening source releases that payload. The long-line/overlap case uses 4,016 owned
bytes, including its error state. Final steady-reader p99 was 3.56, 3.24, and
3.36 ms at the three zooms, passing the 20 ms gate. Those samples use an
1800-by-3000 headless canvas to expose both neighboring hunks; ordinary journeys
use 1800 by 1100. Cold loads and compositor timing are excluded. Earlier samples
ranged up to 15.67 ms p99 and 61.67 ms maximum; they remain in the evidence.

Evidence: `docs/reading-navigation-evidence/step43`. Extract archives with
`nice -n 10 tar -xzf`; representative PNGs are directly viewable. The tall-canvas
limit capture also exposes existing comparison-label scaling outside the normal
window-size journey. Rerun with
`nice -n 10 python3 tests/local_context.py --output output/context-check`.
`tests/package_reading_evidence.py` packages named runs, logs, and representative
images for this and subsequent steps.
Final binary SHA-256: `4d516c76bdd6c0a15540c8f45575911d356d5e9bb4f130427f0586cae5d4603e`.
