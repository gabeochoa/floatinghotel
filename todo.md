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
