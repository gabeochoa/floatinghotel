#!/usr/bin/env python3
"""ThemeDefaults live theme is overwritten from app_default every frame.

1c1db13 fixed two instances in one commit: preload's font tiers and
ui::zoom's ui_scale both wrote the live theme directly and silently
reverted after frame one. Sanctioned paths only: preload via
defaults.set_theme(copy), zoom via ui::zoom::set (writes BOTH slots).
Any other '.theme.<field> =' or 'defaults.theme' in src/ fails here.
Fails on 1c1db13^ preload.cpp, passes now.
"""
import pathlib, re, sys
ALLOW={"src/ui/zoom.h","src/preload.cpp"}
bad=[]
for p in pathlib.Path("src").rglob("*"):
    if p.suffix not in (".h",".cpp") or str(p) in ALLOW: continue
    for i,l in enumerate(p.read_text(errors="ignore").splitlines(),1):
        if re.search(r"\.theme\.\w+\s*=", l) or "defaults.theme" in l or "ThemeDefaults::get().theme" in l: bad.append(f"{p}:{i}: {l.strip()}")
# preload itself must use set_theme, never a direct live write
t=pathlib.Path("src/preload.cpp").read_text()
for i,l in enumerate(t.splitlines(),1):
    if re.search(r"defaults\.theme\.", l) or "ThemeDefaults::get().theme" in l: bad.append(f"src/preload.cpp:{i}: {l.strip()}")
if bad: print("FAIL direct ThemeDefaults live-theme writes:"); print("\n".join(bad)); sys.exit(1)
print("ok theme-writes")
