#!/usr/bin/env python3
"""Guard the UI type scale: every literal font size in src/ must be one of
the three scale tiers (Small 12, Medium 14, Large 16 — see preload.cpp;
XL is an alias of Large). 32 is allowed only for pictograph glyphs
(spinner/empty-state icons). Code views use the code-font setting, which
itself snaps to the same tiers (see settings.cpp), so they stay on-scale.
Fails with the offending sites listed."""
import pathlib
import re
import sys

ALLOWED = {12.0, 14.0, 16.0, 32.0}
PATTERN = re.compile(
    r'with_font_size\(pixels\(([0-9]+(?:\.[0-9]+)?)\)\)'
    r'|with_font\("[a-z-]+",\s*pixels\(([0-9]+(?:\.[0-9]+)?)\)\)')

bad = []
# The FontSize-tier overload stores sizes as h720() (screen-percent), so
# they render at the nominal size only on a 720pt-tall window and inflate
# on taller ones, while pixels() sizes stay fixed. Tier-sized and
# fixed-sized text side by side is what made the UI look like it had many
# font sizes. Ban every height-relative font size in app code.
SCALED = re.compile(
    r'with_font_size\((?:afterhours::ui::)?FontSize::|with_font_tier\(|TypographyScale::'
    r'|with_font_size\(h720\(|with_font\("[a-z-]+",\s*h720\(')
for path in sorted(pathlib.Path("src").rglob("*")):
    if path.suffix not in (".h", ".cpp", ".mm"):
        continue
    for lineno, line in enumerate(path.read_text().splitlines(), 1):
        if SCALED.search(line):
            bad.append(f"{path}:{lineno}: height-scaled font size "
                           f"(use pixels(12/14/16)): {line.strip()}")
        for match in PATTERN.finditer(line):
            size = float(match.group(1) or match.group(2))
            if size not in ALLOWED:
                bad.append(f"{path}:{lineno}: font size {size:g} not in scale "
                           f"{{12, 14, 16}}: {line.strip()}")
if bad:
    print("\n".join(bad))
    sys.exit(1)
print("PASS all literal font sizes are on the 12/14/16 scale")
