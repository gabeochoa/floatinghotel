# Review synchronization through Git

Proposal, for decision. Sync reviews through a dedicated ref, `refs/floatinghotel/reviews/<reviewer>`,
holding one JSON file per review scope, merged per comment with a three-way merge keyed on a
stable comment id. Git notes are rejected below. The local file in `reviews/` stays the source of truth: sync
reads and writes it and never replaces it with something the app cannot open offline.

## What exists today

`src/review_store.cpp` writes one JSON file per repository (and per extra scope) under the save
path's `reviews/` directory, named by `storage::key(repo path)`, schema version 2, through
`afterhours::files::write_string_atomic`. A file holds:

| field | contents |
|---|---|
| `comments` | scope (`wt`, `index` or a commit SHA), file, line, end_line, ranges, old_side, resolved, text, revision, code_context, kind |
| `drafts` | unsent comments keyed by hunk key |
| `approved_hunks`, `reviewed_files` | review progress |
| `verdicts` | per scope: verdict and signature |
| `folded_hunks`, `folded_files` | view state |

Comments have no identity of their own. Two edits to the same comment cannot be matched today,
which is the first thing sync needs.

## What syncs

Only feedback that is about a commit: comments, resolution and verdicts whose scope is a commit
SHA. Working-tree (`wt`) and index comments describe bytes nobody else has; they stay local until
`review_anchor::carry_to_commit` moves them onto a commit, at which point they become syncable.
Drafts, approvals, reviewed-file marks and folds are personal progress and stay local.

## Storage: a dedicated ref, not notes

| | dedicated ref | `refs/notes/*` |
|---|---|---|
| unit of storage | any tree we choose | one blob per annotated commit |
| a comment on a range of commits | one file | duplicated per commit |
| several reviewers | one ref each, no write contention | one shared notes tree, every push races |
| hosting | pushed and fetched as an ordinary ref | many hosts hide or drop notes; `git fetch` skips them without config |
| merge | ours, per comment | `git notes merge` strategies, per note, line based |

The ref `refs/floatinghotel/reviews/<reviewer>` points at a commit whose tree is
`<scope-sha>.json` per reviewed commit. `<reviewer>` is a slug of `user.email`. Each reviewer only
ever writes their own ref, so pushes do not conflict across people; reading a review means
fetching every `refs/floatinghotel/reviews/*`.

Fetch and push need refspecs the user opts into:
`+refs/floatinghotel/reviews/*:refs/floatinghotel/remote/<remote>/reviews/*` for fetch and
`refs/floatinghotel/reviews/<me>:refs/floatinghotel/reviews/<me>` for push. The app adds neither
on its own.

## Schema changes

1. Add `id` to every comment: a random 128-bit hex string assigned at creation and kept through
   edits, carries and resolution. Old files without ids get them on next save.
2. Add `author` (from `user.email`) and `updated` (UTC timestamp) per comment.
3. Add `deleted: true` tombstones instead of removing synced comments, so a deletion survives a
   merge with a copy that still has the comment. Tombstones older than the newest common base are
   dropped on write.
4. Bump `schema_version` to 3. Version 2 readers refuse version 3 files today (they reject
   `schema_version > 2`), which is the intended failure.

## Merge

For one scope file, with `base` the version last synced, `local` the file on disk and `remote` the
fetched blob, merge per comment id:

| local vs base | remote vs base | result |
|---|---|---|
| unchanged | changed | remote |
| changed | unchanged | local |
| changed | changed, same value | either |
| changed | changed, different | conflict |
| absent | present | remote (new comment) |
| tombstone | changed | conflict |

Fields merge independently inside a comment: a resolution change on one side and a text edit on the
other combine without a conflict. A conflict keeps both versions in the local file, marks the
comment `conflict: {theirs: {...}}`, and the feedback basket shows it with "Keep mine", "Keep
theirs" and "Edit". Nothing is pushed while a conflict is open.

`base` lives at `reviews/<repo-key>-sync/<remote>/<scope-sha>.json`, written after every successful
merge. Without it (first sync) every differing field is a conflict, so the first sync of an
existing review shows its differences before pushing anything.

## Concurrent edits on one machine

Two app windows on the same repository already share the review file. Sync adds one more writer.
All writes go through the existing atomic writer and read-modify-write the file under an advisory
`flock` on `<file>.lock`, so the app and a sync never lose each other's comment. The atomic-save
durability entry in triage (distinct temporary names, fsync) is a prerequisite.

## Flow

1. Fetch: `git fetch <remote> <refspec>` as a network operation (cancellable, untimed, like push).
2. For each fetched reviewer ref and scope file: read the blob with `git cat-file`, merge into the
   local file, write `base`.
3. Push: build a tree of this reviewer's commit-scope files with `git hash-object -w` and
   `git mktree`, commit it with `git commit-tree -p <old>`, update the local ref with
   `git update-ref <ref> <new> <old>` and push it. A rejected push (someone pushed this reviewer's
   ref from another machine) fetches and merges again.

No working tree, index or branch is touched; every step uses plumbing on the object database.

## Out of scope

Hosted pull-request comment sync (GitHub or GitLab review APIs) is a separate integration.
Comment anchors already re-locate by content (`review_anchor`); sync does not change that.

## Decisions needed

1. Dedicated ref (recommended) or notes.
2. Whether comments of other reviewers are editable or read-only. The merge above allows edits;
   read-only removes most conflicts.
3. Whether sync is per remote (recommended, matches how refs fetch) or one global setting.
