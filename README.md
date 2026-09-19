# floatinghotel

A review-first Git client for reading, reviewing, and sending feedback on agent-written code. Browse the commit stack, step through diffs, approve the good hunks, and drop comments that get bundled into a single markdown review you hand straight back to your agent.

![Review cockpit](docs/screenshots/review-cockpit.png)

## Features

- Review unstaged changes like a commit, with staged changes in a separate view.
- Collect comments in a feedback basket and export a Markdown review for your agent.
  Marking reviewed and saving feedback do not implicitly stage files.
- Browse commits, historical files, and comparisons in independent preview or kept
  tabs, with close/reopen, Back/Forward, and saved reading positions.
- Read unified or split diffs with wrapping, selection, folding, Find, and source
  navigation. Cmd+C copies plain code; Cmd+Shift+C adds its location.
- Search files, commits, branches, and tags together in revision-aware Quick Open.
  Browse stashes, reflog, worktrees, and submodules from the repository's `…` menu.
- Shift-select a commit range and right-click to review it as one comparison.
  Cmd-click separate changed lines to attach one feedback item to those ranges.
- Pin repositories, filter the repository picker, and relink moved repositories
  while retaining reading state and feedback.
- Resume from a saved review baseline and follow changed files and unresolved comments.
- Keep a compact dock, a fixed footer, and remembered window size. Escape dismisses
  temporary UI; the dock has an explicit collapse action.
- Explicit Git actions support staging, commits, branches, and remote operations.
- Multi-repository tabs, native menus, command logging, and headless rendering.

Open work is tracked in [triage.md](triage.md). The
[final reading replay](docs/reading-navigation-step60.md) records verification and
known performance limits. The [triage implementation report](docs/triage-implementation.md)
records the September 19 additions and their evidence.

## Screenshots

**Commit detail** — message, metadata, changed files, and the diff:

![Commit detail](docs/screenshots/commit-detail.png)

**Feedback basket** — queued comments grouped by scope, one keystroke from your agent:

![Feedback basket](docs/screenshots/feedback-basket.png)

## Build & run

macOS (sokol/Metal backend):

```sh
git submodule update --init --recursive
make
./output/floatinghotel.exe <path-to-a-git-repo>
```

## Keyboard shortcuts

| Key | Action |
| --- | --- |
| `j` / `k` / `n` | Move between hunks |
| `a` | Mark the current hunk reviewed |
| `c` | Comment on the current hunk |
| `⌘⏎` | Send all feedback |
| `Esc` | Dismiss the topmost temporary UI |
