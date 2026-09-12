# Capture UI spacing

Native test screenshots now include a JSON file with the same basename.
Both artifacts describe the same rendered frame. JSON includes every UI node,
with a `rendered` flag to distinguish visible nodes from retained containers.

Run the spacing scenarios:

```sh
nice -n 10 output/floatinghotel.exe . --test-mode --headless \
  --test-script=tests/review_focus/spacing_audit.e2e \
  --screenshot-dir=output/spacing-audit/capture --e2e-timeout=60
nice -n 10 python3 tests/check_layout_dump.py output/spacing-audit/capture
```

The checker requires Pillow. For a JSON-only capture, add `dump_ui_json name`
after a screenshot command in an E2E script. It writes `name.json` in the
screenshot directory. A preceding screenshot ensures the scene has rendered.

The document records viewport dimensions, UI zoom, and a flat node list linked
by `id`, `parent`, and `children`. Each node includes its debug name, untruncated
text, screen rectangle, computed padding, margins, gap, alignment, and scroll
offset. Geometry uses physical pixels. Divide by `ui_scale` for logical pixels.
`font_size` retains the framework's value and unit, rather than claiming to
measure the visible glyph height. `rendered` does not guarantee the entire node
is visible: ancestor clipping can hide part of a rendered node.

Dumps can contain code, paths, commit messages, and draft comments. Treat them
like screenshots of the repository and inspect them before sharing.

## Label inset overrides

Labels with an explicit inset include `text_inset` in logical pixels. This is
the requested configuration, not a measured glyph bound. The current framework
plain-text path can ignore that override when placing a line; see
`docs/afterhours-gaps.md`. Use the paired screenshot to check actual text edges.

`rect` includes all ancestor scroll offsets. `visible_rect` intersects that
rectangle with ancestor clipping bounds and the window. A rendered node can
have an empty visible rectangle. These bounds use the same helpers as native
rendering and hit-testing, including nested scroll areas.
