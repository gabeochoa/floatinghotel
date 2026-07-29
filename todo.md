# floatinghotel — TODO

## Review workflow (P0 — this is the product)
The core loop: review AI-produced changes, **stage = "approved for commit"**,
leave the rest unstaged ("still has my comments"), focus on the unstaged pile.
- [ ] **Hunk / chunk staging** (git add -p). Backend ALREADY EXISTS
      (`git::stage_hunk`/`unstage_hunk` → patch + `git apply --cached`); it is
      just not wired to the UI. Add per-hunk "Stage hunk"/"Unstage" buttons on
      each hunk header in the working-tree diff. Stretch: line-level staging.
- [ ] **Staged/Changes/Untracked as tabs** in the sidebar (not stacked
      sections). Default to "Changes" (to-review). Show counts per tab.
- [ ] **Review progress** signal: "everything staged is good to go, N files
      still to review" — e.g. a progress strip / count so the approved pile is
      visibly "done" and focus goes to unstaged.
- [ ] Per-file / per-hunk review state carried through refresh (which hunks are
      already approved).

## Review UX — decisions from concept mocks (2026-07-29)
Concepts mocked in `docs/mocks/creative_concepts.html`. Verdicts:
- **Chunk-by-chunk queue (concept 1) — YES.** Like `git add -p` but in the UI.
  Approve a hunk → stage it. Reject → stays unstaged (+ attach a comment).
  Decision per hunk; progress to zero.
- **Per-piece comments + "new since you last looked" (concept 2) — YES.** Each
  hunk/piece can carry a small comment. Show what's new since last review so you
  can "mark viewed" without staging — OK to leverage staging as the viewed
  signal.
- **Feedback basket (VALIDATED — the centerpiece).** The confirmed core of the
  review UX. Comments collect into a basket instead of being sent one at a time;
  it's the SAME mechanism for working-tree review and for stack/commit review.
  Each entry is tagged with `file:line` (+ commit SHA in stack mode), grouped by
  commit. Output them together — write `/tmp/floatinghotel-review.md` and offer
  **Send all to agent** / **Copy all** / **Send to terminal**. One round-trip
  carries the whole review. See mock `docs/mocks/review_stack.html`.
- **Spatial canvas (concept 3) — NO.** Drop it.
- **Semantic clusters (concept 4) — maybe.** Interesting for triage; user
  already does this mentally. Keep as optional grouping, not core.

## Stacked-commit review + in-place fixups (P0 — hardest real pain)
Today the AI agent commits and keeps working, building a STACK of commits on a
branch; the user wants to give feedback on a commit EARLY in the stack, not just
the tip. Requirements:
- AI works on a branch; floatinghotel shows the **commit stack** (commits ahead
  of base), selectable.
- User comments on hunks of ANY commit in the stack.
- Exported feedback is **tagged with the commit SHA** (+ file:line) so the agent
  knows exactly WHICH diff each comment targets.
- The agent must apply fixes **into the commented commit** (e.g.
  `git commit --fixup=<sha>` + `rebase --autosquash`, or edit-in-place), NOT as a
  new commit on top of the stack.
- **Comment live AND on the stack (both).** The Working-tree entry (top of the
  stack) is for in-progress, uncommitted changes the agent is writing right now;
  the commits below are the settled stack. Both feed the same basket. Live
  nuances to handle:
  - File-watch refresh updates the working tree as the agent edits — a comment's
    location should **anchor to the code (content/hunk), not a raw line number**,
    so it survives the diff shifting under it.
  - When the agent commits a hunk you'd commented on in the working tree, the
    comment should **migrate to that new commit** (re-tag with the new SHA) so the
    basket stays coherent instead of pointing at a now-committed line.
  - New commits appear at the top of the stack as the agent makes them; existing
    comments keep their SHA tags.
- Feedback file format (draft):
      ## Review of branch feature/x
      ### commit a1b2c3d "Add greeting"
      - greeter.cpp:12 — don't hardcode; read from argv
      ### commit d4e5f6 "Add tests"
      - test_utils.cpp:8 — add an empty-string case
      (agent: apply each as a fixup to the named commit, not on top)

## Diff pane as a hidden shelf (minimalist default)
The code/diff viewer should be a **shelf that's hidden by default** — the whole
app is just the width of the sidebar (repo panels + stack) so it stays out of the
way (Bear-like). Clicking a file or a commit **slides the diff shelf out**; a
"‹ hide" control collapses it back to sidebar-only, and **Escape** closes the
shelf. Feedback basket rides along on the shelf. See the combined mock
`docs/mocks/cockpit.html` (opens collapsed).

## Ballroom mode — interaction model (from interactive prototype)
**Ballroom** = the review flow (named after the floatinghotel poem — see memory);
you **Embark** to enter it ("Embark to ballroom"). Prototype:
`docs/mocks/cockpit.html` (data-driven; ⟳/refresh resets). Confirmed decisions:
- **Embark** (start review) = click the review-progress strip (no separate button).
- A chunk with a comment **auto-folds** (▸ + `✎ N`); click to reopen.
- **Snapshot timing (answer):** take the baseline snapshot **when you Start
  review**; that baseline is what "new since you last looked" diffs against.
  **Re-baseline when you Send all feedback**, so the next agent round shows only
  what changed since your last send.
- **Approve** a hunk = stage it (working tree). Reject = leave unstaged + Comment.
- **Approved chunks disappear** from the review screen (inbox-zero) until ⟳/reset
  — a small "N approved · hidden" note shows the count.
- **Keyboard / vim nav (git add -p style):** a chunk **cursor**; `j`/`k` or `n`
  move, `a` approve current, `c` comment current. Hovering a chunk moves the
  cursor to it. A persistent key-hint footer shows the bindings.
- **Hover-approve spotlight:** hovering a hunk's Approve rings that chunk green
  and dims the others, so it's obvious what you're staging.
- **File-row hover actions:** hovering a file in the sidebar reveals ↩ revert and
  ✓ approve. Modifiers (proposed): **⇧+approve** = stage only (don't mark
  approved), **⇧+revert** = open comment instead of discarding.
- **Comment** = inline textbox; **Enter** adds to basket, **Esc** cancels.
- **⌘⏎** = Send all feedback (Enter is taken by add-comment).
- **Esc** = close the diff shelf (cancels an open compose first).
- Basket **slides** in/out; stack rows show a live comment-count badge.
- **Status badges (M/S)** right-aligned in file rows.
- **Toasts** use the normal panel style (neutral), not neon green.

## Theming — extract styles into a theme struct (TODO in real app + mock)
Pull all colors into a single theme struct so themes are swappable (the mock
already uses CSS custom properties in `:root`; next step: drive them from a JS
`THEMES` object / in the real app, a `Theme` struct in `theme.h` with named
palettes). Ties into "Custom color themes" below.

## Drop Copy buttons in favor of select-to-copy
Now that drag-select copies with `file:line` prepended (the `copyWithLocation`
setting), the explicit per-hunk "Copy" and per-file "Copy Diff" buttons are
redundant clutter. Remove them from the diff UI; rely on highlight→copy. Keep
"Comment" (adds to basket) and "Approve" (working tree) as the hunk actions.
Applies to the real app diff_renderer + the mocks.

## Chunk granularity — auto-split + select-to-chunk
Make chunks small enough to approve confidently. Git has NO "chunk type" concept
— a hunk is just a line range, and you can synthesize any patch, so we control
granularity ourselves:
- **Auto-split:** diff with `--unified=0` so each contiguous change is its own
  hunk; then in the UI break any block >~20 lines into ≤20-line sub-hunks, each
  independently approvable by generating a per-sub-hunk patch → `git apply
  --cached`.
- **Select-to-chunk (line-level approve):** drag-select lines in the diff →
  "Approve selection" → build a patch covering exactly those lines and apply it
  to the index. Same mechanism as `stage_hunk` (already exists), generalized to
  arbitrary line sets.
- **Constraint:** the synthesized patch must apply cleanly — can't partially
  stage two edits on the same line; offsets/context must be correct. `git add -p`
  can only split at unchanged-line seams, which is why we build patches directly.

## Submodules (real gap — no handling today)
- [ ] Detect submodules and show submodule changes in the status list
      (`git status` reports gitlink `M`; need `--` handling + old→new subcommit).
- [ ] Submodule diff view: show `Subproject commit <old> → <new>` and the list
      of commits the pointer moved across.
- [ ] Stage/commit submodule pointer changes; ideally drill into the submodule.

## Polish pass — match the mock (menu bar, chrome, spacing)
The HTML mock (`docs/mocks/diff_view_mock.html`) reads much cleaner than the
shipped app. Do a full visual pass to close the gap: menu bar, title/tab chrome,
toolbar, section headers (count pills), row spacing/contrast.
- [ ] Compare mock screenshot vs live-app screenshot, build a punch-list.
- [ ] NOTE: the app font atlas only renders ASCII (see memory), so mock icon
      glyphs (⟳ ◧ ⑂ ↑ ↓) need a real icon font before they can ship.

## Performance (keep it snappy — a core feature)
- [ ] Preserve instant open + snappy feel. Guardrail: any new feature (folding,
      hunk staging, multi-file view) must stay cheap per frame — no full re-diff
      or re-parse on interaction; cache git output; windowed rendering for big
      diffs/logs (virtual_list). Never regress the fast startup.

## Ideas
- **Render markdown** in the diff/file viewer (e.g. `.md` files shown formatted,
  toggle raw/rendered).
- **Integrated terminal → agent bridge (long-term, aspirational).** Embed a
  terminal panel in the app so you can select text and right-click → "send to
  agent" (Cursor-style). Ties the review loop shut: select a diff region or
  terminal output, fire it to the agent, watch the file-watch refresh bring back
  the fix — all without leaving floatinghotel.

  Architecture (fits our stack — Sokol/Metal + afterhours UI, same shape as VS
  Code / Neovim GUIs / most terminal widgets):

      app (floatinghotel)
        └─ afterhours UI + Sokol/Metal renderer
             └─ terminal widget
                  └─ VT parser (escape/ANSI/cursor/modes)
                       └─ PTY
                            └─ bash / zsh / powershell

  Our terminal widget only has to: maintain a grid of cells, and render
  (monospace font, cursor, selection, scrollback). The VT parser handles the
  hard parts (escape parsing, ANSI colors, cursor state, terminal modes).

      struct Cell { char32_t glyph; Color fg; Color bg; uint8_t attributes; };

  VT parser options:
  - **libvterm** — was the obvious pick (parser-only, renderer-agnostic), BUT it
    is no longer maintained standalone; it's been absorbed into the Neovim tree
    (github.com/neovim/libvterm is a mirror). Could still vendor a snapshot.
  - **libtsm** — another parser-only lib, also fairly stale.
  - **Write our own** — a VT100/xterm subset parser is self-contained and matches
    "vendor as little as possible." Open question: effort. A usable subset
    (CSI/SGR colors, cursor moves, erase, scroll region, basic modes) is
    tractable; full xterm compat (mouse, alt-screen, DEC modes, wide chars,
    combining) is a long tail. Prototype: spike a minimal parser against `ls`,
    `vim`, and a REPL to gauge scope before committing.

  Also needs: PTY (forkpty/openpty on macOS), text selection + right-click
  context menu in the widget, and an agent transport for "send selection".

## Diff viewer backlog (from review session)
- [ ] Multi-file view: see all files' changes together in one scroll.
- [ ] Stage / unstage from the multi-file view (per-file + per-hunk).
- [ ] Fold / collapse a file in the multi-file view.
- [ ] Unfold hidden context lines around a hunk to see more of the file
      (VS Code-style "N hidden lines" expander).
- [ ] Sticky file-name header: pin the current file's name at the top of the
      scroll view as you scroll through its hunks.
- [ ] "No diff available for file": still show the file name at the top, plus
      file size and whether it changed vs the previous diff.

## Bugs
- [ ] File-list scroll: text scrolled off-screen stops rendering, but the rows
      are still present and clickable (render/cull mismatch in the scroll view).

## Refactoring
- [ ] Config-driven syntax / file-type highlighting. Current approach is
      hardcoded in code (e.g. `theme::fileTypeColor()` extension→color switch,
      and diff coloring baked into `diff_renderer`). Rework it so highlighting
      is defined by per-file-type **config files** — adding highlighting for a
      new language/file type should be "drop in a new config file", no code
      changes or recompile. Decide config format (JSON?), where they live
      (resources/highlight/*.json?), and how they're discovered/loaded.

## Polish
- [ ] UI audit: find and fix ~25 small UI issues (spacing, alignment, contrast,
      truncation, empty states).
- [ ] Settings boilerplate: collapse the per-setting getter/setter/Data/load/save
      plumbing into a generic key-value store.

## Theming & panels
- [ ] Custom color themes (user-supplied theme files; light/dark + custom).
- [ ] Resizable panels generally. Sidebar drag-resize + persist exists; extend
      to the commit-log split and the main/diff split, and persist their sizes.

## Editor features (aspirational — large)
- [ ] Syntax highlighting for many languages (ties into config-driven highlighting).
- [ ] IntelliSense: autocomplete, parameter hints, type info.
- [ ] Code navigation: go-to-definition, find references, symbol search.
- [ ] Safe rename, multi-cursor, column/block selection.
- [ ] Code folding, integrated search & replace across the project.

## Git-client feature reference (competitive union)
Captured from Fork / Tower / GitKraken / Sourcetree / VS Code+GitLens /
LazyGit / GitHub Desktop, for prioritization later. Most are well beyond the
current viewer+committer scope — this is a menu, not a plan.
- History/graph: commit graph, file history, line history, blame, commit search,
  branch comparison, commit compare.
- Branching: create/rename/delete/switch, drag-and-drop branches, Git Flow.
- Rewrite/apply: interactive rebase, squash/fixup, cherry-pick, merge, revert,
  reset (soft/mixed/hard), amend, undo git actions, action previews.
- Staging: partial/hunk/line staging from anywhere.
- Stash & tags: full stash and tag management.
- Remotes: remote management, multiple remotes/accounts.
- Diff: side-by-side, syntax-highlighted, image diffs, conflict-resolution tools.
- Repo: submodules, worktrees, Git LFS, hooks, cleanup, clone/create/publish.
- Collaboration: PR creation/management, code review, GitHub/GitLab/Bitbucket/
  Azure DevOps integration, issue/Jira linking, team workspaces.
- AI (our niche): AI code review, commit messages, git explanations.
