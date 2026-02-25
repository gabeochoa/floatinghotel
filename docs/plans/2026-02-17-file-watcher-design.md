# File System Watcher — Auto-Refresh on External Changes

## Problem

Repository data (file changes, branches, commits) doesn't update when changes are made outside the app. Users must close and reopen to see external edits, terminal branch switches, etc.

## Approach

Platform-native file system watching with a compile-time abstracted API. Mirrors the render backend pattern (`window_manager.h`) but uses standard platform macros (`__APPLE__`, `__linux__`) instead of custom defines.

### Rejected Alternatives

- **Polling `git rev-parse` on a timer** — misses working tree edits, 2-3s latency
- **Cross-platform library (efsw)** — unnecessary dependency when we only need macOS now

## Architecture

### File: `src/platform/file_watcher.h`

Single header containing:

1. A `FileWatcherBackend` concept defining the required API
2. `#ifdef __APPLE__` — FSEvents implementation
3. `#else` — no-op stub (compiles everywhere, does nothing)
4. `using FileWatcher = <selected impl>` type alias

### Concept

```cpp
template <typename T>
concept FileWatcherBackend = requires(T& t, const std::string& path) {
    { t.watch(path) } -> std::same_as<void>;
    { t.stop() } -> std::same_as<void>;
    { t.poll_changed() } -> std::same_as<bool>;
};
```

- `watch(path)` — start watching a directory recursively. Tears down any existing watch first (handles repo/tab switching).
- `stop()` — stop watching, clean up resources.
- `poll_changed()` — returns true if anything changed since the last call. Resets the flag atomically.

### Apple Implementation (FSEvents)

- `FSEventStreamCreate` with callback that sets `std::atomic<bool> changed_`
- Stream runs on a dedicated `CFRunLoop` in a background `std::thread`
- Latency parameter: `0.5s` to coalesce rapid file writes
- `watch()` stops any existing stream before creating a new one
- Destructor joins the run loop thread

### ECS Integration: `FileWatcherSystem`

New system that runs each frame:

1. Checks `poll_changed()` on the watcher
2. If true, sets `repo.refreshRequested = true` on the active tab's `RepoComponent`

Slots in next to `AsyncGitDataRefreshSystem` — one detects, the other refreshes.

### Watch Scope

Watch the repo's working directory recursively, including `.git/`. This catches:
- External file edits (editor, build tools)
- Terminal `git commit`, `git checkout`, `git pull`, etc.
- Branch switches, rebases, merges

Redundant refreshes from our own git operations (which already set `refreshRequested`) are harmless — `AsyncGitDataRefreshSystem` re-fetches the same data quickly.

### Lifecycle

- `watch()` called when a repo is opened or the active tab changes
- `stop()` called on app shutdown
- Switching repos calls `watch()` again (which internally stops the old stream first)

## Build Changes

Add `-framework CoreServices` to `FRAMEWORKS` in the makefile (macOS only).

## Implementation Plan

1. Create `src/platform/file_watcher.h` with concept, Apple impl, and no-op stub
2. Add `FileWatcherSystem` ECS system
3. Wire into `main.cpp` — register system, call `watch()` on repo open
4. Add CoreServices framework to makefile
5. Test: edit a file externally, verify sidebar updates without manual refresh
