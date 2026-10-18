# Integrated terminal and agent bridge

Evaluation, for decision. Recommendation: do not embed a terminal. Build the agent bridge as
explicit send actions that write review feedback to a file or the clipboard (most of which exists),
plus an optional "run command" that shows a non-interactive process's output in the command log.
An embedded terminal needs a PTY, a VT parser and Unicode-aware selection inside an immediate-mode
UI that today has none of the three, and it duplicates the terminal the user already runs agents in.

## What a terminal would need

| piece | what it takes here | existing code |
|---|---|---|
| PTY lifecycle | `forkpty`, resize via `TIOCSWINSZ` on every layout change, SIGCHLD reaping, process-group kill on close | `util/process.cpp` spawns with pipes and kills the group; no PTY |
| terminal parser | VT100/xterm state machine: cursor addressing, scroll regions, alternate screen, SGR colour, bracketed paste, OSC 8 links | none; candidates are libvterm (C, MIT) or a hand-rolled subset |
| grid rendering | a cell grid redrawn per frame, cursor, wide (CJK, emoji) cells | the code view draws lines, not cells; CJK/emoji fallback is an open afterhours item |
| selection | cell-based, across wrapped lines and the scrollback, with grapheme clusters | `diff_sel` selects code lines by byte offset |
| scrollback | bounded ring of lines with search | none |
| input | every key, including Ctrl chords the app binds today (Ctrl+G, Ctrl+Tab) | shortcuts are global; a focused terminal must take them first |

The keyboard conflict alone touches every shortcut owner (`shortcut_owner`, `focus_system.h`), and
the CJK/emoji gap means agent output with emoji renders wrong until the afterhours font fallback
lands.

## What the bridge is for

Moving review feedback to an agent and the agent's changes back into review. The second half
already works: the agent edits files, the file watcher refreshes, and Unstaged changes shows them.
The first half exists as Cmd+Enter "copy feedback" (the review footer) and `expect_review_export`
in tests.

## Proposed bridge

1. "Send feedback to file": write the review export to a path the user sets per repository (for
   example `.agent/feedback.md`), atomically, on demand. Agents read a file without integration.
2. "Run command": a per-repository command (for example `make test`) run as a non-interactive
   process; output streams into the command log, exit status as a toast. Cancellable like the
   network operations. No stdin, no PTY.
3. Explicit actions only: nothing is sent or run on save, commit or file change.

## Decisions needed

1. Accept the recommendation (no embedded terminal), or fund the terminal with libvterm as the
   parser and the font fallback as a prerequisite.
2. Whether "Run command" belongs in the app at all.
