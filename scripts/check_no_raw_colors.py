#!/usr/bin/env python3
"""No raw Color{...} literals outside src/ui/theme.h (and preload's library-theme setup).

a4c2cfb routed hardcoded colours through the palette for the light theme;
raw literals kept being copied afterwards, so they render dark-only.
Theme-independent exceptions, matched literally: transparent {0,0,0,0},
black overlay/shadow {0,0,0,N}, white-on-accent {255,255,255,255}.
Everything else: add a palette global + LIGHT swatch in theme.h, or
theme::pick(dark, light) for a true one-off.
Fails on a4c2cfb^ diff_renderer.h; passes now.
"""
import pathlib, re, sys
ALLOW_FILES={"src/ui/theme.h","src/preload.cpp"}
OK=re.compile(r"Color\{(0, 0, 0, \d+|255, 255, 255, 255)\}")
bad=[]
for base in (pathlib.Path("src"),):
    for p in base.rglob("*"):
        if p.suffix not in (".h",".cpp") or str(p) in ALLOW_FILES: continue
        for i,l in enumerate(p.read_text(errors="ignore").splitlines(),1):
            for m in re.finditer(r"Color\{[^}]+\}", l):
                if not OK.fullmatch(m.group(0)): bad.append(f"{p}:{i}: {m.group(0)}")
if bad: print("FAIL raw colours outside theme.h:"); print("\n".join(bad)); sys.exit(1)
print("ok raw-colours")
