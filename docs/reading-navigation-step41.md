# Step 41: quieter hunk controls

Hunk locations stay visible while secondary controls appear on pointer hover or
keyboard focus within that hunk. Their layout space stays reserved, so revealing
them does not move the location or code. Narrow panes use compact approve and
comment icons, and every action remains available from the hunk context menu.

Hover uses clipped physical geometry and respects modal input gates. Focus uses
the hunk's semantic identity, including repository and document. Read-only commit
hunks now have distinct focus identities as well. The layout probe records
effective opacity so the replay can check hidden and revealed controls directly.

The initial three-zoom replay passed interaction and stable-layout checks.
Screenshot review then found tight scrollbar spacing and insufficient icon
padding in compact buttons. The final layout reserves the scrollbar gutter and
centers 16-pixel icons inside 22-pixel buttons. Those initial screenshots remain
in the evidence archive. The baseline runner was corrected to focus the hunk's
location label: clicking the center of the entire row can legitimately hit a
secondary action.

Final verification passed at 100%, 140%, and 200%: hover, pointer exit,
keyboard focus, separate commit-hunk identities, unchanged caption geometry,
minimum location width, full button visibility, centered icons, and context-menu
commenting without staging. Existing fold preservation, selected-file review,
shortcut ownership, wrapped Unicode selection in unified/split/source views,
and all seven navigation regressions passed.

The final steady-reader p99 samples were [['1.51'], ['2.01'], ['1.05']] ms at the three zooms;
all passed the existing 20 ms gate. Cold-load and compositor timing are excluded.
The initial compile rejected an Entity passed to the input-gate API, which takes
an entity ID; that was corrected before the native replay.

Evidence: `docs/reading-navigation-evidence/step41`. Extract archives with
`nice -n 10 tar -xzf`; representative PNGs are directly viewable. Rerun with
`nice -n 10 python3 tests/hunk_chrome.py --output output/hunk-check`.
Final binary SHA-256: `bd5d9ee2b89d0c947ed6bf540bb80d1c024d9927ce75ffc6d523a5fde5b5b718`.
