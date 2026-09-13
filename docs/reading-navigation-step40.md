# Step 40: selected-file review

Selecting a changed file now shows that file's diff by default. The location row
contains a Selected file / All files menu, and the choice is stored per
repository. Before a file is selected, opening a review still shows all changes.
Choosing Selected file explicitly at that point selects the first file.

The full diff remains the source for review progress, approval eligibility,
feedback, and file filters. Only the rendered file order changes. Switching
modes requests restoration of the logical reading anchor; it does not load a
new patch. Source excursions retain the selected file and return destination.

Verification passed at 100%, 140%, and 200%: selected and all-file modes,
whole-review progress and feedback, source return, staged/unstaged content
identity, narrow geometry, restart persistence, and independent repository
preferences. The staged and unstaged Git diffs remain byte-for-byte unchanged.
Existing fold-preservation, 13 logical-anchor comparisons, all seven navigation
regressions, comparison/deleted-source search, and step 39's metadata/message
journeys also passed.

The first runner incorrectly expected commit-view hunk composition controls and
an additional tab when the existing preview was replaceable. Feedback is now
exercised in the working-changes review. Another assertion used exact floating
point equality for clipped geometry; the recorded difference was below 0.0001
pixels and the check now permits 0.1 pixels. Those failed attempts are archived.

The final steady-reader p99 samples were [['2.22'], ['1.66'], ['1.73']] ms at 100%, 140%, and 200%;
all passed the 20 ms gate. These exclude cold loads and compositor timing.
Cache and worker limits are unchanged.

Evidence: `docs/reading-navigation-evidence/step40`. Extract archives with
`nice -n 10 tar -xzf`; representative PNGs are directly viewable. Rerun with
`nice -n 10 python3 tests/selected_file_review.py --output output/selected-check`.
Final binary SHA-256: `ce44b63b54ff3ae55169e9fd7db5b5618f23c58d461b59a4575e6669d28e13d0`.
