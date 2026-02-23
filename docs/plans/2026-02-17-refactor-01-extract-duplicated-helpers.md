# Refactor 01: Extract Duplicated Helpers

## Problem

`parse_iso8601`, `relative_time`, and `parse_decorations` are copy-pasted
identically between two namespaces:

- `commit_log_detail` in `src/ecs/sidebar_system.h` (lines 114-196)
- `commit_detail_view` in `src/ecs/layout_system.h` (lines 411-488)

The only semantic difference is `relative_time`: the sidebar version returns
`"3d"` while the layout version returns `"3d ago"`. Everything else is
character-for-character identical. A bug fix in one copy won't propagate to
the other.

## Scope

~170 lines of duplicated code across 2 files.

## Plan

1. Create `src/util/git_helpers.h` with:
   - `parse_iso8601(const std::string&) -> std::time_t`
   - `relative_time(const std::string&, bool suffix = false) -> std::string`
     (the `suffix` flag controls whether " ago" is appended)
   - `DecorationType` enum + `Decoration` struct
   - `parse_decorations(const std::string&) -> std::vector<Decoration>`

2. Delete `commit_log_detail::parse_iso8601`, `relative_time`,
   `parse_decorations` from `sidebar_system.h`.

3. Delete `commit_detail_view::parse_iso8601`, `relative_time`,
   `parse_decorations` from `layout_system.h`.

4. Update both files to `#include "../util/git_helpers.h"` and use the
   shared versions.

## Risk

None. Pure mechanical extraction with no behavior change.

## Estimated impact

- ~170 lines removed
- 1 new file (~80 lines)
- Net: ~90 fewer lines, single source of truth for these helpers
