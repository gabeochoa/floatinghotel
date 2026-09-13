# Step 42: next and previous change

Keyboard j/n and k and the Next Change / Previous Change menu items use one
navigation operation across working changes, commits, and revision comparisons.
The itinerary follows visible file order across file boundaries, including
selected-file mode. Text changes reveal their first added line, or their first
deleted line on the before side; binary and metadata-only changes reveal the
file header. First and last endpoints are announced.

Navigation uses typed review locations and logical anchors, unfolds the target,
and keeps focus in the code viewer. Progress and feedback retain their full
review scope. Hidden approved hunks remain excluded while Show approved is off.
New document navigation clears stale pending review actions before the next
content frame, so an empty review cannot inherit actionable hunk counts.

The first native replay found that selecting a binary file retained the previous
text file's document anchor. With no text rows to replace that anchor, Next Change
kept selecting the binary file instead of reaching the endpoint. Explicit file
navigation now clears an anchor belonging to another file. Reopening the review
without selecting a file retains its reading position; Back restores the prior
visit's anchor. Both cases have direct regression coverage.

At 200% zoom, the target line is visible while its file header can be above the
viewport. The source-return journey uses the selected tree row's Open source
menu at every zoom, preserving the before revision and originating position.

Final verification passed at 100%, 140%, and 200% for working, commit, and
comparison navigation, before-side source opening, binary endpoints, menu and
keyboard commands, input ownership, and empty reviews without staging. The
history, source-origin, folds, selected-file review, shortcut, and seven existing
navigation regressions passed, as did 13 anchor-layout comparisons and 52 unit
checks. Representative screenshots were inspected after geometry assertions.

Steady nonempty-reader p99 was 1.54, 1.49, and 1.75 ms at the three zooms;
empty-reader p99 was 1.15, 1.49, and 1.20 ms. These pass the 20 ms rendering gate
and exclude cold loads and compositor timing.

Evidence: `docs/reading-navigation-evidence/step42`, including baseline and failed
attempts. Extract archives with `nice -n 10 tar -xzf`. Rerun with
`nice -n 10 python3 tests/change_navigation.py --output output/change-check`.
Final binary SHA-256: `7573858c11ee8af60850c890b7b8562923145099672707b27089c47de4f7198e`.
