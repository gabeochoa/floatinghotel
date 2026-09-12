import argparse
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=False)
for zoom, steps in ((100, 0), (140, 4), (200, 10)):
    directory = out / str(zoom)
    directory.mkdir()
    setup = 'make_test_repo\nresize 1600 1000\nwait_for_refresh\nclick_text "Add contributing guidelines"\nwait_for_refresh\n'
    setup += 'native_menu_action "Reset Zoom"\n' + 'native_menu_action "Zoom In"\n' * steps
    setup += 'native_menu_action "Toggle Command Log"\nwait_frames 5\nscreenshot before\n'
    def replay(name, script):
        path = directory / (name + ".e2e")
        path.write_text(script)
        with (directory / (name + ".log")).open("w") as log:
            result = subprocess.run([str(ROOT / "output/floatinghotel.exe"), "--test-mode", "--headless",
                f"--test-script={path}", f"--screenshot-dir={directory}", "--e2e-timeout=90"],
                cwd=ROOT, env=dict(os.environ, FH_NATIVE_MENUS="1"), stdout=log, stderr=subprocess.STDOUT, timeout=120)
        assert result.returncode == 0, (zoom, name)
    def handle(name):
        snapshot = json.loads((directory / (name + ".json")).read_text())
        return next(n["visible_rect"] for n in snapshot["nodes"] if n.get("name") == "cmdlog_drag_handle" and n["rendered"])
    replay("probe", setup)
    before = handle("before")
    assert before["width"] > 100 and before["height"] > 0
    x = before["x"] + before["width"] / 2
    y = before["y"] + before["height"] / 2
    replay("drag", setup + f"drag_to {x} {y} {x} {y - 40}\nwait_frames 10\nscreenshot after\n")
    after = handle("after")
    assert abs((before["y"] - after["y"]) - 40) <= 3, (zoom, before, after)
    print(f"PASS {zoom}%: command-log divider follows pointer by 40 rendered pixels", flush=True)
