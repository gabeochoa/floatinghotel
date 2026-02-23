# Refactor 02: Split RepoComponent Into Focused Components

## Problem

`RepoComponent` in `src/ecs/components.h` has ~30 fields serving 5 unrelated
purposes. This makes it hard to reason about what systems actually need, bloats
every entity that carries it, and means unrelated state changes (e.g. toggling
a branch dialog) look like repo mutations.

### Current field groups

| Group                | Fields | Purpose                          |
|----------------------|--------|----------------------------------|
| Repo identity        | 8      | path, branch, dirty, head hash   |
| File change lists    | 3      | staged, unstaged, untracked      |
| Commit log           | 3      | commitLog, loaded, hasMore       |
| Commit detail cache  | 5      | cachedHash, diff, body, email, parents |
| Branch dialog state  | 5      | showNew, name, showDelete, etc.  |
| Selection + diff     | 3      | selectedFile, selectedCommit, currentDiff |
| Refresh flags        | 2      | refreshRequested, isRefreshing   |

## Plan

### Phase 1: Extract BranchDialogState (low risk, high clarity)

Create a new component:

```cpp
struct BranchDialogState : public afterhours::BaseComponent {
    bool showNewBranchDialog = false;
    std::string newBranchName;
    bool showDeleteBranchDialog = false;
    std::string deleteBranchName;
    bool showForceDeleteDialog = false;
};
```

- Add `BranchDialogState` to each tab entity alongside `RepoComponent` in
  `main.cpp` and `TabBarSystem::create_new_tab`.
- Update `sidebar_system.h` (dialog rendering, "+ New" button) to query
  `BranchDialogState` instead of `RepoComponent` for dialog fields.
- Update `HandleMakeTestRepo` to clear dialog state on the new component.
- Remove the 5 fields from `RepoComponent`.

### Phase 2: Extract CommitDetailCache (low risk)

Create:

```cpp
struct CommitDetailCache : public afterhours::BaseComponent {
    std::string cachedCommitHash;
    std::vector<FileDiff> commitDetailDiff;
    std::string commitDetailBody;
    std::string commitDetailAuthorEmail;
    std::string commitDetailParents;
};
```

- Add to tab entities.
- Update `render_commit_detail` in `layout_system.h` to query this component.
- Update `HandleMakeTestRepo` to clear it.
- Remove 5 fields from `RepoComponent`.

### Phase 3 (optional): Extract CommitLogData

Could split `commitLog`, `commitLogLoaded`, `commitLogHasMore` into their own
component. This is lower priority since these fields are tightly coupled to
the refresh cycle.

## Risk

Low. Components are data-only structs. The refactor is mechanical: move fields,
update queries, add component to entity creation. No logic changes.

## Estimated impact

- `RepoComponent` drops from ~30 to ~20 fields (phases 1+2)
- 2 new components (~15 lines each)
- Clearer data ownership: dialog state is UI-only, cache is derived data
