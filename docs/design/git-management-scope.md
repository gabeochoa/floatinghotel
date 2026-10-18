# Broader Git management, request by request

Reassessment, for decision. The app reviews and reads; it stages, commits, branches, pushes, pulls
and fetches. Each request below is judged by whether it serves reviewing a change. Recommended:
add conflict review and worktree switching, keep stash to its current basics, and leave the rest
to the command line.

Existing and not new work: stage/unstage file, hunk and lines; commit and amend (with
`commit.template`); new branch, checkout, delete branch with confirmation; push (with destination
dialog), pull, fetch (cancellable); basic stash; cherry-pick and revert entries in the commit menu.

| request | recommendation | reason | first step |
|---|---|---|---|
| merge conflict resolution | build, review-shaped | a conflicted file is a change to read; the reader already shows both sides of a diff | show `git diff --cc` stages as a three-way read-only view with "take ours / theirs / both" per hunk writing the file; `flow_merge_conflict.e2e` is a placeholder today |
| worktrees | build switching only | reviewing a branch without losing the working state is a review task | list `git worktree list --porcelain` in the repository menu and open each as a repository tab; creating and removing stay CLI |
| interactive rebase | no | rewriting history is authoring, and a half-done rebase in a GUI is hard to recover | none |
| richer tags | read-only | tags matter as comparison endpoints | add tags to Compare Revisions completion; no create/delete |
| remotes management | no | set once per clone | none; push already shows fetch and push URLs |
| stash management (list, apply, drop) | keep basics | stash is a working-state tool, adjacent to review | none beyond today's stash |
| Git LFS | read-only, exists | pointer files already show size, OID and local-cache availability | none |
| hooks | no UI | hooks run from git itself on commit and push; failures already reach the commit toast | surface hook stderr in full in the command log |
| repository publishing | no | one-time action outside review | none |
| hosted PR/CI integration | separate design | needs per-host auth and APIs; overlaps review sync | after `review-sync.md` is decided |

## Decisions needed

1. Conflict review: approve the read-only three-way view with per-hunk resolution.
2. Worktree switching: approve listing worktrees as openable tabs.
3. Confirm the "no" rows so they leave the triage list.
