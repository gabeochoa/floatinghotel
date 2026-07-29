# floatinghotel — TODO

## Ideas
- **Render markdown** in the diff/file viewer (e.g. `.md` files shown formatted,
  toggle raw/rendered).

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
