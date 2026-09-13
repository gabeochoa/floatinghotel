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
    for view in ("review", "files"):
        directory = out / f"{view}-{zoom}"
        directory.mkdir()
        setup = 'make_test_repo\nresize 1600 1000\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
        setup += 'native_menu_action "Zoom In"\n' * steps
        if view == "files":
            setup += 'click_ui sidebar_working_files\nwait_frames 3\n'
        setup += 'screenshot before\n'

        def replay(name, script):
            path = directory / f"{name}.e2e"
            path.write_text(script)
            with (directory / f"{name}.log").open("w") as log:
                result = subprocess.run([str(ROOT / "output/floatinghotel.exe"), "--test-mode", "--headless",
                    f"--test-script={path}", f"--screenshot-dir={directory}", "--e2e-timeout=90"],
                    cwd=ROOT, env=dict(os.environ, FH_NATIVE_MENUS="1"), stdout=log, stderr=subprocess.STDOUT, timeout=120)
            assert result.returncode == 0, directory / f"{name}.log"

        def rect(name):
            data = json.loads((directory / f"{name}.json").read_text())
            nodes = [n for n in data["nodes"] if n["rendered"] and not n["hidden"]]
            assert not any(n.get("name") == "sidebar_worktree_status" for n in nodes)
            footer = next(n["rect"] for n in nodes if n.get("name") == "status_bar_bg")
            assert abs(footer["y"] + footer["height"] - data["viewport"]["height"]) < .2
            return next(n["rect"] for n in nodes if n.get("name") == "sidebar_h_divider")

        replay("probe", setup)
        before = rect("before")
        x, y = before["x"] + before["width"] / 2, before["y"] + before["height"] / 2
        drag = f'mouse_down {x} {y}\nwait_frames 5\nscreenshot held\nmouse_move {x} {y+40}\nwait_frames 5\nscreenshot down\nwait_frames 15\nscreenshot still\n'
        drag += f'mouse_move {x} {y+10}\nwait_frames 5\nscreenshot up\nmouse_move {x} 995\nwait_frames 5\nscreenshot high_limit\nmouse_move {x} 975\nwait_frames 5\nscreenshot high_reverse\n'
        drag += f'mouse_move {x} 0\nwait_frames 5\nscreenshot low_limit\nmouse_move {x} 20\nwait_frames 5\nscreenshot low_reverse\nmouse_up\nwait_frames 5\nscreenshot released\nvalidate footer_fixed=true\n'
        replay("drag", setup + drag)
        assert abs(rect("held")["y"] - before["y"]) < .2
        assert abs(rect("down")["y"] - before["y"] - 40) < 2
        assert abs(rect("still")["y"] - rect("down")["y"]) < .2
        assert abs(rect("up")["y"] - before["y"] - 10) < 2
        assert abs(rect("high_reverse")["y"] - rect("high_limit")["y"] + 20) < 2
        assert abs(rect("low_reverse")["y"] - rect("low_limit")["y"] - 20) < 2
        assert abs(rect("released")["y"] - rect("low_reverse")["y"]) < .2
        print(f"PASS {view} {zoom}%: divider follows rendered pointer, holds steady, reverses at both limits, footer stays fixed", flush=True)
