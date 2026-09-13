# Step 19: tree type-to-select

Typing while a tree row owns focus previews a matching visible filename.
Repeated letters cycle matches; additional letters extend the prefix. After
700 ms without input, the next character starts a fresh prefix. Matching
ignores ASCII letter case and preserves Unicode bytes. Directory rows match
their own names, and collapsed descendants are excluded. Arrow navigation
clears the prefix. Text fields retain editing keys; tree typing does not
invoke reader commands or open repository search.

The native fixture exposed an existing diff parser bug: Git-quoted Unicode
paths became empty filenames. The parser now decodes octal bytes, escaped
quotes, backslashes, and control characters in diff headers and rename
metadata. Binary and mode-only changes also retain paths without hunk
headers. The fixture uses Git's default quoting, without changing its config.

All 10 tree navigation/type-selection unit tests and 55 Git parser tests
passed. Timeout tests cover 699 ms, the 700 ms boundary, and a reversed clock.
The native replay passed at 100%, 140%, and 200%, checking cycling, longer
prefixes, no match, uppercase input, Unicode filenames, collapsed children,
filter editing, preview/keep, and working-tree source previews. File contents
and the index remain unchanged. Frame p99 values were 3.50, 3.69, and 5.68 ms.
Tree keyboard and shortcut-focus regressions passed at all three zooms,
as did source-origin journeys and all seven navigation regressions. The
navigation ownership check also passed.

The framework E2E character queue cannot inject Unicode code points correctly.
Unicode prefixes are covered by unit tests; the native replay types an ASCII
prefix to select a Unicode filename. The failed Unicode-injection attempt is
in `output/step19-type-second`, and the limitation is recorded in
`docs/afterhours-gaps.md`. Another test attempt could not reach Files rows at
200% with Git controls expanded; the replay now explicitly hides those
controls before entering Files.

Evidence: `docs/reading-navigation-evidence/step19`.
Full replay: `output/step19-type-final`.
Binary SHA-256: `8c1151beb4d743c7bbc556cf85f536eea7b049ece6b83874bcb7545803dcd056`.
