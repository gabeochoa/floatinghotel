import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--output", type=Path, required=True)
parser.add_argument("--baseline", action="store_true")
parser.add_argument("--snapshots", action="store_true")
parser.add_argument("--zooms", nargs="+", type=int, default=[100, 140, 200])
args = parser.parse_args()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=False)
repo = out / "fixture"
repo.mkdir()
for python in [False, True]:
    lines = []
    for number in range(1, 501):
        if number == 1: line = "def outer():" if python else "void outer() {"
        elif number == 3: line = "    if ready:" if python else "  if (ready) {"
        elif number == 8: line = "    body_8()" if python else "  }"
        elif number == 40: line = "outside_40()" if python else "}"
        elif number == 43: line = "def other():" if python else "void other() {"
        elif number == 48: line = "outside_48()" if python else "}"
        elif 1 < number < 40 or 43 < number < 48:
            line = ("        " if python and 3 < number < 8 else "    ") + f"body_{number}()" + ("" if python else ";")
        else: line = ("#" if python else "//") + f" padding {number}"
        if not python and 80 <= number <= 83:
            line = ['const char* continued = "' + chr(92), '{ ' + chr(92), 'ignored ' + chr(92), '}";'][number - 80]
        lines.append(line)
    (repo / ("fold.py" if python else "fold.cpp")).write_bytes((("\n" if python else "\r\n").join(lines) + ("\n" if python else "\r\n")).encode())
for command in [("init", "-q", "-b", "main"), ("config", "user.name", "Folding fixture"),
                ("config", "user.email", "folding@example.invalid"), ("config", "commit.gpgsign", "false"),
                ("add", "."), ("commit", "-qm", "Source folding")]:
    subprocess.run(["git", "-C", str(repo), *command], check=True, capture_output=True)
binary = ROOT / "output/floatinghotel.exe"
digest = hashlib.sha256(binary.read_bytes()).hexdigest()
def capture(label, count):
    return f"wait_for_refresh\nwait_frames 3\nwait_for_refresh\nwait_frames 15\nworkspace_checkpoint {count} {label}\nscreenshot {label}\n"
def open_file(name):
    return f'key CMD+P\nwait_for_refresh\nscreenshot picker\ntype "{name}"\nkey ENTER\n'
def options(action):
    return f'click_ui full_file_options\nscreenshot menu\nclick_ui context_menu_item_{action}\n'
results = []
for zoom in args.zooms:
    directory = out / str(zoom)
    directory.mkdir()
    settings = directory / "settings"
    settings.mkdir()
    script = 'resize 1700 1100\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
    script += 'native_menu_action "Zoom In"\n' * ((zoom - 100) // 10)
    script += "screenshot zoom_ready\n" + open_file("fold.cpp") + capture("initial", 2)
    if not args.baseline:
        script += "click_ui source_fold_3\n" + capture("inner", 2)
        script += "click_ui source_fold_1\n" + capture("outer", 2)
        script += open_file("fold.py") + capture("python", 3)
        script += "click_ui source_fold_1\n" + capture("python_closed", 3)
        script += "click_ui open_tabs_menu\nscreenshot tabs\nclick_ui context_menu_item_fold.cpp\n" + capture("returned", 3)
        script += 'key CTRL+G\nwait_frames 4\nclick_ui line_picker_input\nkey CMD+A\ntype "5"\nkey ENTER\n' + capture("revealed", 3)
        script += options("Fold block at caret") + capture("menu_fold", 3)
        script += options("Unfold all") + capture("unfolded", 3)
        script += "click_ui source_fold_1\nkey CMD+W\n" + capture("closed", 2)
        script += "key CMD+SHIFT+T\n" + capture("reopened", 3)
        script += "resize 1350 900\nkey CMD+EQUAL\n" + capture("resized", 3)
        script += "click_ui source_fold_1\n" + capture("font_unfolded", 3)
        script += "click_ui source_fold_1\n" + capture("font_folded", 3)
        script += 'key CMD+F\ntype "body_5()"\n' + capture("find_revealed", 3)
        script += "key ESCAPE\nbench_frames 120\nexpect_p99_below 20\n"
    path = directory / "journey.e2e"
    path.write_text(script)
    with (directory / "journey.log").open("w") as log:
        result = subprocess.run([str(binary), str(repo), "--test-mode", *(["--headless"] if args.snapshots else []),
            f"--test-script={path}", f"--screenshot-dir={directory}", "--e2e-timeout=180"], cwd=ROOT,
            env=dict(os.environ, FH_NATIVE_MENUS="1", FH_TEST_NATIVE_HIDDEN="1", FH_TEST_SETTINGS_DIR=str(settings)),
            stdout=log, stderr=subprocess.STDOUT, timeout=210)
    assert result.returncode == 0, directory / "journey.log"
    if not args.snapshots: assert "Native test window hidden=1 key=0" in (directory / "journey.log").read_text()
    initial = json.loads((directory / "initial.json").read_text())
    buttons = [n for n in initial["nodes"] if n["rendered"] and n.get("name", "").startswith("source_fold_")]
    if args.baseline: assert not buttons
    else:
        assert buttons
        state = json.loads((directory / "initial.workspace.json").read_text())
        assert [r["first"] for r in state["source_folds"]["ranges"]] == [1, 3, 43]
        for label, absent, present, retained in [("inner", range(4, 8), [3, 8], 1), ("outer", range(2, 40), [1, 40], 2),
                ("python_closed", range(2, 40), [1, 40], 1), ("returned", range(2, 40), [1, 40], 2),
                ("revealed", [], [5], 0), ("menu_fold", range(4, 8), [3, 8], 1),
                ("unfolded", [], [5], 0), ("reopened", range(2, 40), [1, 40], 1),
                ("resized", range(2, 40), [1, 40], 1), ("font_unfolded", [], [1, 5], 0),
                ("font_folded", range(2, 40), [1, 40], 1), ("find_revealed", [], [5], 0)]:
            state = json.loads((directory / f"{label}.workspace.json").read_text())
            layout = json.loads((directory / f"{label}.json").read_text())
            rows = layout["reading_rows"]
            numbers = {r["line"] for r in rows}
            assert not numbers.intersection(absent) and set(present) <= numbers, (zoom, label, sorted(numbers))
            assert state["source_folds"]["retained"] == retained, (zoom, label, state["source_folds"])
            for node in layout["nodes"]:
                if node["rendered"] and node.get("name", "").startswith("source_fold_") and node["visible_rect"]["height"] > 0:
                    row = next(r for r in rows if r["line"] == int(node["name"].split("_")[-1]) and r["offset"] == 0)
                    assert node["rect"]["x"] + node["rect"]["width"] <= row["content_x"], (label, node, row)
            results.append(dict(zoom=zoom, label=label, retained=retained))
    assert not subprocess.check_output(["git", "-C", str(repo), "status", "--porcelain"], text=True).strip()
    print(f"PASS {zoom}%: " + ("folding absent before change" if args.baseline else "nested folds, Python, explicit reveal, tabs, reopen, resize, and gutter geometry"), flush=True)
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
(out / "result.json").write_text(json.dumps(dict(passed=True, baseline=args.baseline, binary_sha256=digest, results=results), indent=2) + "\n")
