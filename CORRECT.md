# CORRECT — floatinghotel (2026-10-04)
Branch fix-untracked-unstaged. Pre-existing dirty makefile change (git_parser.cpp in test_git_commands rule) was the same defect as class 1 and is included in that commit, disclosed there.

## Classes (>=2x)
| # | Class | Evidence |
|---|---|---|
| 1 | Unit-test registry drift: suite exists but runs nowhere / link deps differ per registry | makefile 16 suites vs runner 53 vs 57 files; test_git_commands makefile-only + missing git_parser; test_outline in neither; test_welcome/fold/history makefile-only |
| 2 | Raw `Color{...}` bypassing palette → dark-only rendering in light theme | a4c2cfb routed 21 files; afterwards raw literals reappeared in diff_renderer (7), sidebar, chrome_icons |
| 3 | Writing ThemeDefaults live theme, overwritten from app_default next frame | 1c1db13: preload font tiers AND ui::zoom ui_scale, same bug twice in one commit |
Not counted: h720/w1280 mixing (1b37d60 basket, 3131db1 sidebar) — remaining h720 uses are documented intentional in 3131db1 (presets/command-log/toolbar), so no honest global gate.

## Fix level + why
1. Architecture: single registry. Runner is sole list, `make test` delegates, check_unit_registry.py gates. Added 5 missing suites (all pass individually).
2. Types/palette + lint: new palette globals with LIGHT swatches (highest level available — colours are values, not types), check_no_raw_colors.py gates.
3. Lint: check_theme_writes.py — sanctioned writers already exist (set_theme, zoom::set both-slots); ban the alias/direct pattern elsewhere.

## Commits (local, not pushed)
- 6e71c99 test: one unit-test registry, make test delegates
- 7e0f109 theme: route reader highlights through palette, lint raw colours
- 9103b12 lint: forbid direct ThemeDefaults live-theme writes
- (this) docs: CORRECT.md + CLAUDE.md

## Proof
- check_unit_registry: FAIL on HEAD (5 missing), ok 57 now. `make test`: 56/57 — the 1 failure is pre-existing deterministic test_review_target (reselecting_a_review, line 1200, code untouched here) which the old 16-suite make test hid; runner exposed it. Not fixed here (product bug, separate work).
- check_no_raw_colors: FAIL on a4c2cfb^ diff_renderer.h (17 literals), ok now.
- check_theme_writes: FAIL on 1c1db13^ preload.cpp:147, ok now.
- App binary not rebuilt for colour routing (header-only palette values, dark values unchanged byte-for-byte; light values new).

## Rule table
| Rule | Gate |
|---|---|
| Register unit tests only in run_unit_tests.sh | check_unit_registry.py + make test |
| No raw Color{} outside theme.h/preload | check_no_raw_colors.py |
| No direct live-theme writes | check_theme_writes.py |
