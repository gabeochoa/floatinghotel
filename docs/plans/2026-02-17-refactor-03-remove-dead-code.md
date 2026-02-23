# Refactor 03: Remove Dead Code

## Problem

Two pieces of dead code increase cognitive load and compile time without
providing any value.

### 1. `GitDataRefreshSystem` (`src/ecs/git_refresh_system.h`)

This was the original synchronous git refresh system. It was replaced by
`AsyncGitDataRefreshSystem` (registered in `main.cpp` line 538). The sync
version is never registered anywhere. The file is 74 lines.

The comment in `async_git_refresh_system.h` even says: "The original
synchronous GitDataRefreshSystem is preserved alongside this one -- swap the
registration in main.cpp to choose which to use." That swap never happened
and won't -- async is strictly better.

### 2. `SettingsComponent` (`src/ecs/components.h`, lines 174-184)

This ECS component is defined but never instantiated, never queried, and
never used by any system. All settings are managed through the `Settings`
singleton (`src/settings.h`). The component is 11 lines of dead weight.

### 3. `MenuComponent::commandLogVisible` (line 155)

This field is defined on `MenuComponent` but never read or written. The
command log visibility is controlled by `LayoutComponent::commandLogVisible`.

## Plan

1. Delete `src/ecs/git_refresh_system.h`.
2. Remove the `#include "ecs/git_refresh_system.h"` if present in any file
   (currently not included anywhere in `main.cpp`).
3. Delete `SettingsComponent` from `src/ecs/components.h`.
4. Delete `MenuComponent::commandLogVisible` from `src/ecs/components.h`.

## Risk

None. The code is unreachable.

## Estimated impact

- 1 file deleted (74 lines)
- ~12 lines removed from components.h
- Cleaner component surface: every component is actually used
