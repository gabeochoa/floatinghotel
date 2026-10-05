# Agent rules — see CORRECT.md for evidence
- Unit tests: register ONLY in tests/run_unit_tests.sh (`make test` delegates there); run scripts/check_unit_registry.py.
- Colours: palette globals in src/ui/theme.h (+LIGHT swatch) or theme::pick(); no raw Color{...} elsewhere (black overlay/white/transparent excepted). Run scripts/check_no_raw_colors.py.
- Never write ThemeDefaults live `.theme` directly — preload set_theme() / ui::zoom::set() only. Run scripts/check_theme_writes.py.
- Known red at HEAD: test_review_target reselecting_a_review (pre-existing, untouched by CORRECT).
