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
parser.add_argument("--scopes", nargs="+", default=["working", "index", "commit"])
args = parser.parse_args()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=False)
repo = out / "fixture"
repo.mkdir()
cases = [("block.cpp", "/*", "*/", [117, 129, 142, 255]),
         ("triple.py", chr(34) * 3, chr(34) * 3, [183, 205, 159, 255]),
         ("template.js", "`", "`;", [183, 205, 159, 255])]
for name, opening, closing, color in cases:
    text = "".join((opening if i == 4070 else closing if i == 4110 else f"body_{i:05} éλ") + "\n" for i in range(1, 6001))
    (repo / name).write_text(text)
for command in [("init", "-q", "-b", "main"), ("config", "user.name", "Syntax fixture"),
                ("config", "user.email", "syntax@example.invalid"), ("config", "commit.gpgsign", "false"),
                ("add", "."), ("commit", "-qm", "Multiline syntax")]:
    subprocess.run(["git", "-C", str(repo), *command], check=True, capture_output=True)
binary = ROOT / "output/floatinghotel.exe"
digest = hashlib.sha256(binary.read_bytes()).hexdigest()
commit = subprocess.check_output(["git", "-C", str(repo), "rev-parse", "HEAD"], text=True).strip()
results = []
for zoom in [100, 140, 200]:
  for scope in args.scopes:
    directory = out / f"{zoom}-{scope}"
    directory.mkdir()
    settings = directory / "settings"
    settings.mkdir()
    def capture(label, count):
        return f"wait_for_refresh\nwait_frames 3\nwait_for_refresh\nwait_frames 15\nworkspace_checkpoint {count} {label}\nscreenshot {label}\n"
    script = 'resize 1700 1100\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
    script += 'native_menu_action "Zoom In"\n' * ((zoom - 100) // 10)
    script += "screenshot zoom_ready\n"
    script += "click_ui review_unstaged_changes\n" + capture("review", 1)
    if scope == "index": script += "click_ui review_staged_changes\n" + capture("scope", 2)
    if scope == "commit": script += 'click_text "Multiline syntax"\n' + capture("scope", 2)
    extra = scope != "working"
    for index, (name, opening, closing, color) in enumerate(cases):
        script += f'key CMD+P\nwait_for_refresh\nscreenshot picker_{index}\ntype "{name}:4097"\nkey ENTER\n'
        script += capture(f"inside_{index}", index + 2 + extra)
        script += 'key CTRL+G\nwait_frames 4\nclick_ui line_picker_input\nkey CMD+A\ntype "4120"\nkey ENTER\n'
        script += capture(f"after_{index}", index + 2 + extra)
    path = directory / "journey.e2e"
    path.write_text(script)
    with (directory / "journey.log").open("w") as log:
        result = subprocess.run([str(binary), str(repo), "--test-mode", *(["--headless"] if args.snapshots else []),
            f"--test-script={path}", f"--screenshot-dir={directory}", "--e2e-timeout=180"], cwd=ROOT,
            env=dict(os.environ, FH_NATIVE_MENUS="1", FH_TEST_NATIVE_HIDDEN="1", FH_TEST_SETTINGS_DIR=str(settings)),
            stdout=log, stderr=subprocess.STDOUT, timeout=210)
    assert result.returncode == 0, directory / "journey.log"
    if not args.snapshots: assert "Native test window hidden=1 key=0" in (directory / "journey.log").read_text()
    for index, (name, opening, closing, color) in enumerate(cases):
        for label, number, expected in [("inside", 4097, color), ("after", 4120, [228, 230, 235, 255])]:
            layout = json.loads((directory / f"{label}_{index}.json").read_text())
            row = next(r for r in layout["reading_rows"] if r["path"] == name and r["line"] == number)
            node = next(n for n in layout["nodes"] if n["id"] == row["id"] and n["rendered"])
            assert node["visible_rect"]["height"] > 0 and node["visible_rect"]["width"] > 0, (zoom, name, label, row)
            span = next(s for s in node["text_spans"] if f"body_{number:05}" in s["text"])
            matched = span["color"] == expected
            if args.baseline and label == "inside": assert not matched, (zoom, name, span)
            else: assert matched, (zoom, name, label, span, expected)
            results.append(dict(zoom=zoom, scope=scope, path=name, location=label, expected=expected, actual=span["color"]))
    print(f"PASS {zoom}% {scope}: " + ("missing multiline state reproduced" if args.baseline else "multiline state follows direct line destinations"), flush=True)
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
(out / "result.json").write_text(json.dumps(dict(passed=True, baseline=args.baseline, binary_sha256=digest, results=results), indent=2) + "\n")
