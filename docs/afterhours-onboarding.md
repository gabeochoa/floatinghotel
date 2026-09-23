# Afterhours bump: b385dc9 → a7d7afc (112 commits)

## Onboarding progress (2026-09-23)

- [x] 1 Virtual lists — `mk_keyed`+`measure_config` already flow through
  `imm::virtual_list` at all 7 call sites via `ui::virtual_list`;
  `anchor_scroll` enabled only on the recent-repos list (diff/commit/tree
  scrolls have custom anchors that would fight it).
- [x] 2 Draw assertions — `tests/e2e_scripts/draw_assertions.e2e` (passes);
  sokol capture records primitives only (triangle/circle/line/quad), not
  text/rectangle ops — upstream gap.
- [x] 3 E2E commands — `expect_text_i`/`dump_focus` covered in the draw
  script; `register_ui_commands` already registered. Harness `E2EArgs`
  deferred: it has no `=`-form, `--e2e-timeout` or `--validation-report`,
  which every runner here uses.
- [x] 4 Motion — `ui_motion::register_bridge` in `registerUIRenderSystems`;
  `presets::hover_lift` on recent-repo rows (first trigger-block adoption).
- [x] 5 Pinch — already installed (`main.cpp`) + consumed (`ZoomSystem`).
- [x] 6 Corner radius — already migrated (`RADIUS_BUTTON`/`RADIUS_BOX`,
  `with_corner_radius`); remaining `with_roundness` are pill/circle/dot
  fractions, correctly fractions.
- [ ] 7 Entity indexes — deferred: no hot scan identified; `whereID`/
  child lookups already O(1)/optimized upstream. Revisit with profiler data.
- [ ] 8 Public text measuring — deferred: diff measuring is lexer/glyph
  aware (`code_wrap` callables), not plain font+size wrapping.
- [ ] 9 Modal contract — deferred: dialogs use bespoke open flags +
  already-registered modal plugin; migration is a dedicated pass.
- [ ] 10 `sync_group` — N/A: split diff renders in one scroll view, not two.


Pulled 2026-09-22. Build + unit suite (51/51) + `local_context` e2e pass.
`hunk_chrome` 200% failure and `check_diff_context.sh` stale
`Show surrounding lines` expectation pre-date this bump (verified on the
old baseline).

## Local vendor patch (push upstream, then drop)

`begin_shader_mode` is ambiguous with the sokol backend: the unqualified
calls in `plugins/ui/render_primitives.h` and `plugins/ui/rendering.h`
(sh shader-scope feature, 809bdd1) find both `afterhours::` (drawing
helpers) and `afterhours::graphics::`. Qualified to `graphics::` in both
files (begin + end). Working tree of `vendor/afterhours` is dirty with
exactly this; commit it in the afterhours repo and re-point.

## Breaking changes already adapted in this bump

| Change | Adaptation |
| --- | --- |
| `detail::apply_scroll_offset` removed | Renamed to `detail::apply_ancestor_transform` (scroll+scale+translate, one source for render/hit-test) — 8 call sites |
| `RenderScrollbars` system deleted; scrollbars drawn by `RenderImm` | Deleted `RenderFloatingScrollbars`; floating replay via `RenderFloatingUI` paints them for layer ≥ 100 |
| `builtin_profile::totals()` removed | Perf provider + bench log read `profiling::default_collector().snapshot().systems` |
| E2E quoted args tokenized by runner (197d54e) | `e2e_joined_text()` in `e2e_command_handlers.h`; `native_menu_action`/`show_toast`/`expect_review_export` no longer re-parse quotes |
| `wait_frames`/injected `scroll_wheel` now actually work | `tests/local_context.py` overlap step needs a `screenshot` render barrier before `right_click_text` |
| Animation plugin rewritten (`afterhours::motion`) | Only `animation::set_instant` was used; it survives — no call-site change |
| `HasScrollView::viewport_size` is `optional` | Assignments still work; reads already used `viewport_or_zero()` |
| Labels lost the free 5px inset (`Theme::text_inset=0`) | No code change; watch spacing/layout tests — text shifts left, wraps re-break |
| Scrollbars now drawn by default | Floatinghotel no longer draws its own for layer ≥ 100; check for double bars elsewhere before adding more |

## Onboard next, in value order

1. **Virtual-list primitives** — `imm::mk_keyed`, `measure_config`,
   `HasScrollView::anchor_scroll`, real `virtual_list` culling. Replace the
   hand-rolled diff/file-tree windowing and scroll anchoring; directly
   serves the "refresh without moving things" work.
2. **Draw assertions in e2e** — `capture::enable`, `expect_drawn`/
   `expect_not_drawn`/`expect_drawn_at`/`dump_draws`,
   `expect_draw_calls_below`. Assert borders/fills/text instead of
   screenshot + layout-JSON archaeology; `DrawnCall` carries entity+layer.
3. **E2E harness** — `e2e_testing/harness.h` (`E2EArgs`, `configure_runner`)
   retires floatinghotel's own arg parsing in `main.cpp`; also
   `dump_focus`/`expect_focused` (names the real focus holder),
   `expect_text_i`, `--e2e-speed`.
4. **Motion** — trigger blocks `.on_hover/.on_press/.on_focus/.on_appear/
   .on_state/.on_change` + `Spring::*` + `animation_presets.h`
   (`fade_up`, `pop_in`, `hover_lift`, `press_squash`…). Replace ad-hoc
   opacity/hover state in hunk headers, menus, toasts. Closed-form springs
   are stall-proof — good fit for the headless frame pacer.
5. **Pinch zoom (macOS)** — already compiled with
   `AFTER_HOURS_ENABLE_MACOS_GESTURES`; just `gestures::install_pinch_monitor()`
   + `input::get_pinch_delta()` → code zoom. e2e: `pinch <delta>`.
6. **`with_corner_radius(px)`** — migrate `with_roundness(fraction)` sites
   that meant pixels (audit presets/badges); fraction-of-short-side caused
   the changelog's floatinghotel warning list.
7. **Entity indexes** — `add_index<C>`/`whereIndexed<C>` for
   children-of-entity and per-file lookups in sidebar/diff; rebuilt per
   collection change, zero cost when unused.
8. **Public text measuring** — `ui::measure_text_wrapped`/`ui::wrap_text`
   (font name + size) replaces local measure callables in wrap code.
9. **Modal contract + popups** — Entering/Visible/Exiting/Hidden states,
   consistent popup dismissal and focus ownership; use for compare/relink/
   commit dialogs instead of bespoke open/close flags.
10. **`HasScrollView::sync_group`** — lock unified/split or two-pane
    scrolling without app code.

## Free wins (no onboarding, already active)

Text measured once (memo, ~99% hit), `imm::mk` allocation-free, font
glyphs now cover arrows/checkmarks/ellipsis (audit `↑`/`↓`/icons that
silently drew nothing), scrollbar drag, `whereID` O(1), headless Metal
resize pipeline leak fixed (resize soak RSS), `expect_no_text`/scroll
injection in e2e now test what they claim.

## Later / skip for now

`effects::Effect`/`BlurPass` are raylib-first (sokol path is upstream
todo) — shader scope `with_shader` works via framebuffer; particles,
`draw_quad`, dashed `polyline`, per-unit text motion are polish for
feedback/celebration moments, not review flow. Command-picker and
terminal plugins are larger product decisions (command log exists).
