import argparse
import json
import os
from pathlib import Path
import subprocess

from reading_journey import ROOT, fixture

parser = argparse.ArgumentParser()
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=False)
repo = out / "fixture"
fixture(repo)
for zoom, steps in ((100, 0), (140, 4), (200, 10)):
    directory = out / str(zoom)
    directory.mkdir()
    script = 'resize 1600 1000\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
    script += 'native_menu_action "Zoom In"\n' * steps
    script += 'click_text "Reading root"\nwait_for_refresh\nscreenshot before\n'
    script += 'hover_ui commit_detail_scroll\nscroll_wheel 0 -8\nwait_frames 30\nscreenshot after\n'
    (directory / "journey.e2e").write_text(script)
    with (directory / "run.log").open("w") as log:
        result = subprocess.run([str(ROOT / "output/floatinghotel.exe"), str(repo), "--test-mode", "--headless",
            f"--test-script={directory / 'journey.e2e'}", f"--screenshot-dir={directory}", "--e2e-timeout=90"],
            cwd=ROOT, env=dict(os.environ, FH_NATIVE_MENUS="1"), stdout=log, stderr=subprocess.STDOUT, timeout=120)
    assert result.returncode == 0, directory
    snapshots = [json.loads((directory / (name + ".json")).read_text()) for name in ("before", "after")]
    def node(snapshot, name):
        return next(n for n in snapshot["nodes"] if n.get("name") == name and n["rendered"])
    before, after = snapshots
    assert node(before, "commit_heading")["rect"] == node(after, "commit_heading")["rect"]
    assert node(after, "commit_heading")["rect"]["height"] <= 60.1 * zoom / 100
    assert node(after, "commit_detail_subject")["visible_rect"]["height"] > 0
    assert node(after, "commit_sticky_revision")["visible_rect"]["height"] > 0
    assert node(after, "commit_detail_scroll")["scroll"]["y"] > 100
    assert node(before, "commit_meta_compact")["visible_rect"]["height"] > 0
    assert node(after, "commit_meta_compact")["visible_rect"]["height"] == 0
    for name in ("before", "after"):
        assert (directory / (name + ".png")).read_bytes().startswith(b"\x89PNG\r\n\x1a\n")
    print(f"PASS {zoom}%: metadata scrolls away, compact subject/revision remain visible", flush=True)
