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
config = out / "settings"
config.mkdir()
settings = config / "settings.json"
settings.write_text(json.dumps(dict(window_width=1472, window_height=912, window_shelf_collapsed=False,
    expanded_window_width=1472, sidebar_width=350)))


def replay(name, script):
    directory = out / name
    directory.mkdir()
    path = directory / "journey.e2e"
    path.write_text("wait_for_refresh\nwait_frames 10\nscreenshot initial\n" + script)
    with (directory / "run.log").open("w") as log:
        result = subprocess.run([str(ROOT / "output/floatinghotel.exe"), str(repo), "--test-mode", "--headless",
            f"--test-script={path}", f"--screenshot-dir={directory}", "--e2e-timeout=90"], cwd=ROOT,
            env=dict(os.environ, FH_NATIVE_MENUS="1", FH_TEST_SETTINGS_DIR=str(config)),
            stdout=log, stderr=subprocess.STDOUT, timeout=120)
    assert result.returncode == 0, directory
    return json.loads((directory / "initial.json").read_text())


probe = replay("probe", "")
handle = next(n["visible_rect"] for n in probe["nodes"] if n.get("name") == "sidebar_h_divider" and n["rendered"])
x, y = handle["x"] + handle["width"] / 2, handle["y"] + handle["height"] / 2
first = replay("resize", f"drag_to {x} {y} {x} {y - 60}\nwait_frames 5\n" + "resize 1333 999\nwait_frames 10\nsave_window_state\nscreenshot resized\n")
assert first["viewport"] == dict(width=1472, height=912), first["viewport"]
saved = json.loads(settings.read_text())
assert (saved["window_width"], saved["window_height"], saved["window_shelf_collapsed"]) == (1333, 999, False), saved
assert abs(saved["commit_log_ratio"] - .4) > .03, saved
second = replay("restart", "save_window_state\n")
resized = json.loads((out / "resize/resized.json").read_text())
def divider(snapshot):
    return next(n["rect"] for n in snapshot["nodes"] if n.get("name") == "sidebar_h_divider" and n["rendered"])
assert divider(resized) == divider(second), (divider(resized), divider(second))
assert second["viewport"] == dict(width=1333, height=999), second["viewport"]
assert any(n.get("name") == "working_review_heading" and n["visible_rect"]["height"] > 0 for n in second["nodes"])
saved.update(window_width=350, window_height=777, window_shelf_collapsed=True, expanded_window_width=1333)
settings.write_text(json.dumps(saved))
third = replay("dock", "save_window_state\n")
assert third["viewport"] == dict(width=350, height=777), third["viewport"]
saved = json.loads(settings.read_text())
assert (saved["window_width"], saved["window_height"], saved["expanded_window_width"], saved["window_shelf_collapsed"]) == (350, 777, 1333, True), saved
print("PASS: resized dimensions survive restart; dock preserves its size and expanded width", flush=True)
