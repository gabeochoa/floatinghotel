# Proposal: atomic save/load helpers in afterhours `files`

**Status:** proposal (not yet implemented in afterhours). Written from
floatinghotel; hanabi has the same need.

## Motivation

afterhours already resolves per-app directories
(`afterhours::files::get_config_path()` / `get_save_path()`, backed by
platform_folders). But every consumer that writes to those directories rolls its
own file write, and they all use a **plain `std::ofstream`**:

- floatinghotel `src/settings.cpp` → `settings.json`
- floatinghotel `src/review_store.cpp` → per-repo review JSON (new)
- hanabi `src/settings.cpp` → `settings.json`, `src/api/disk_cache.cpp` →
  `sessions.json` + `tx_*.json`, `src/api/token_store.cpp`

A plain `ofstream` truncates the target file **before** the new bytes are
written. If the process crashes, is killed, or the disk fills between the
`open(O_TRUNC)` and the flush, the file is left **truncated or empty** — the
user loses their settings / save game / review. This is exactly the data the
"local-first" longevity ideal says we must protect.

The fix is the standard write-temp-then-rename dance. It's ~15 lines, but it's
easy to get subtly wrong (temp must be on the same filesystem as the target, you
must `fsync` before `rename`, you must clean up the temp on failure), so it
belongs in one shared place rather than copy-pasted into every app.

## Proposed API

Add to `vendor/afterhours/src/plugins/files.h` (declarations) and
`files.cpp` (definitions). Keep it at the **raw-string** level — do NOT pull a
JSON dependency into afterhours; callers serialize with whatever they already
use (floatinghotel and hanabi both use nlohmann::json at the app level).

```cpp
namespace afterhours {
struct files : developer::Plugin {
  // ... existing members ...

  // Atomically replace `path` with `content`: write to a sibling temp file,
  // fsync, then rename over `path`. On any failure the original file is left
  // untouched and the temp is removed. Returns false on failure.
  static bool write_string_atomic(const fs::path& path,
                                  std::string_view content);

  // Read the whole file. Returns std::nullopt if it doesn't exist or can't be
  // read (callers treat that as "use defaults", never as a crash).
  static std::optional<std::string> read_string(const fs::path& path);
};
}
```

### Reference implementation (for `files.cpp`)

```cpp
bool files::write_string_atomic(const fs::path& path, std::string_view content) {
  std::error_code ec;
  fs::create_directories(path.parent_path(), ec);
  // Temp MUST be in the same directory as the target so rename() is atomic
  // (a cross-filesystem rename is a copy+delete, which is not atomic).
  fs::path tmp = path;
  tmp += ".tmp";
  {
    std::ofstream f(tmp, std::ios::binary | std::ios::trunc);
    if (!f.good()) return false;
    f.write(content.data(), static_cast<std::streamsize>(content.size()));
    f.flush();
    if (!f.good()) { fs::remove(tmp, ec); return false; }
  }
  // (optional but recommended: fsync the fd before rename for crash-durability)
  fs::rename(tmp, path, ec);
  if (ec) { fs::remove(tmp, ec); return false; }
  return true;
}

std::optional<std::string> files::read_string(const fs::path& path) {
  std::ifstream f(path, std::ios::binary);
  if (!f.good()) return std::nullopt;
  std::ostringstream ss;
  ss << f.rdbuf();
  return ss.str();
}
```

`fsync` note: the fully-durable version needs the raw fd
(`open`/`write`/`fsync`/`close`) rather than `ofstream`, so a crash *and* a power
loss can't lose the rename. The `ofstream` version above is crash-safe (atomic
replace) but not power-loss-safe; that's an acceptable first cut and can be
upgraded later without changing the signature.

## How consumers adopt it

Purely mechanical — replace the `ofstream` write and the `ifstream` read:

```cpp
// before (settings.cpp / review_store.cpp / disk_cache.cpp)
std::ofstream f(path); f << j.dump(2);

// after
afterhours::files::write_string_atomic(path, j.dump(2));

// before
std::ifstream f(path); auto j = nlohmann::json::parse(f);

// after
if (auto s = afterhours::files::read_string(path))
    auto j = nlohmann::json::parse(*s);
```

No behavior change beyond crash-safety. floatinghotel's `review_store` (see the
review-persistence work) ships a private copy of `write_string_atomic` today;
once this lands in afterhours, delete the private copy and call the shared one —
and do the same for `settings.cpp` and hanabi.

---

## Related deferred local-first ideas (captured, not scheduled)

From the Ink & Switch "local-first software" article. Not part of the
persistence work; recorded here so they aren't lost.

- **Git-notes review sync (#4).** Store review comments/approvals as git notes
  or under a `refs/floatinghotel/reviews/*` ref instead of (or in addition to) a
  local JSON blob. Reviews would then replicate peer-to-peer through git's
  existing fetch/push — no server, matching the article's sync model. Merge
  semantics for concurrent edits (CRDT-lite) would need design.
- **Time-travel history (#6).** A scrubber over commit history that steps the
  diff/repo view back in time, with recently-arrived changes highlighted — the
  article's most-repeated UX finding ("visualizing document history is
  important"). We already render a commit stack/graph to build on.
