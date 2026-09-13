# Step 45: word and line gestures

Double-click selects a code word, including underscores and
non-ASCII text. Triple-click selects the logical line across its rendered wrap
fragments. Shift-click extends the existing selection on the same file and diff
side. Pointer geometry uses the existing zoom-aware reader hit testing.

The native baseline confirms no word selection at all nine mode/zoom combinations.
The 40 diff/navigation unit checks pass, including word boundaries, tabs,
underscores, composed characters, and emoji sequences. All nine native mode/zoom journeys pass at 100%, 140%, and 200%, including both
sides of split diffs. The replay verifies the selected word, a single identifier
spanning multiple visual rows, full logical-line text, Shift extension, exact
location labels, and clipped highlight bounds. Horizontal scroll input leaves
wrapped text and selection unchanged. Git contents and the index stay unchanged.

The first build failed on an incorrect coordinate type; the click tracker now
stores its two measured coordinates directly. The final binary also passes the
nine caret journeys, wrapped Unicode selection and Find, focus return, shortcut
ownership, all seven navigation regressions, and the 4,000-line source gate.
The twelve gesture benchmarks have p99 values of 1.58, 2.33, 2.90, 2.36, 2.00,
1.80, 2.04, 2.18, 1.30, 2.91, 3.35, and 2.40 ms; the largest single frame is
5.31 ms. All satisfy the 20 ms p99 gate.

Final binary SHA-256:
`da3b854d8f167e29e25c0fca4551be29c297b08a47aedb357e4035a397dbdfff`.
Baseline, native output, screenshots, unit results, and both build logs are in
`docs/reading-navigation-evidence/step45`. Shared gesture handling and code-word
policy are recorded as upstream candidates in `docs/afterhours-gaps.md`.

This step uses the active reader's existing selection records. Selection across
unloaded or unrendered content, edge autoscroll, and bounded full-range copy remain
step 48's work. No inactive document payload cache is added.
