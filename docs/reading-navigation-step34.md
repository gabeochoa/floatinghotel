# Step 34: Keyboard search previews

Up/Down in the search query or result list selects the next visible match,
skipping file headers and collapsed groups. It previews the exact revision and
source line while focus stays in search. The selected result is scrolled only
enough to remain visible. Modified arrow keys remain available to text editing.

Enter from the query keeps the selected result and moves focus to its document.
If the query has changed, Enter still submits the search immediately. Escape
from the matching source document returns focus to the retained search input;
menus and other temporary UI keep their existing dismissal priority. Escape from
search itself closes the pane. Source identity and originating review still pass
through the shared navigation boundary.

Thirteen search unit tests pass, including collapsed groups, empty lists, and
endpoints. Native keyboard, focus, geometry, and deleted-file checks pass at all three
zoom levels.

The first native replay did not recognize query focus because the text-input
wrapper and its focused inner field have different entity IDs. Shortcut routing
now reads the existing semantic control identity (`repo_search_input`) through
`ui::focus_target`; tests use that same exposed semantic identity. The failed
capture is retained under `failures/input-focus`.

The final keyboard fixture previews historical lines 1 and 20, skips a collapsed
file, opens both lines of a deleted file, and renders a match at line 5,000.
The comparison fixture additionally previews a deleted-file match from the
comparison's exact base and returns to its originating review. Enter keeps tabs;
Escape returns to search; Cmd+Down leaves the selected result unchanged.

Render p99 for the large-file journey was 1.10, 1.69, and 10.12 ms at 100%, 140%,
and 200%; its largest frame was 14.22 ms. Comparison journeys measured 4.35,
3.81, and 4.74 ms p99. All pass the 20 ms gate. Grouping, debounce, retained
search, mouse comparison navigation, focus return, Escape, and all seven
navigation regressions pass. Git state remains unchanged. Evidence is in
`docs/reading-navigation-evidence/step34`.
