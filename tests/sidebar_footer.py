import argparse
import hashlib
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
repo = out / "fixture"
repo.mkdir()
(repo / "a.cpp").write_text("int a = 1;\n")
for command in (("init", "-q", "-b", "main"), ("config", "user.name", "Footer fixture"),
                ("config", "user.email", "footer@example.invalid"), ("config", "commit.gpgsign", "false"),
                ("add", "."), ("commit", "-qm", "Footer fixture")):
    subprocess.run(["git", "-C", str(repo), *command], check=True, capture_output=True)
binary = ROOT / "output/floatinghotel.exe"
digest = hashlib.sha256(binary.read_bytes()).hexdigest()
for state in ("clean", "changed"):
    if state == "changed":
        (repo / "a.cpp").write_text("int a = 2;\n")
    for zoom, steps in ((100, 0), (140, 4), (200, 10)):
        directory = out / f"{state}-{zoom}"
        directory.mkdir()
        script = 'resize 350 800\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
        script += 'native_menu_action "Zoom In"\n' * steps
        script += 'wait_frames 3\nscreenshot dock\nresize 1600 1000\nclick_text "Footer fixture"\nwait_for_refresh\nscreenshot expanded\nvalidate footer_fixed=true\n'
        (directory / "journey.e2e").write_text(script)
        with (directory / "run.log").open("w") as log:
            result = subprocess.run([str(binary), str(repo), "--test-mode", "--headless",
                f"--test-script={directory / 'journey.e2e'}", f"--screenshot-dir={directory}", "--e2e-timeout=90"],
                cwd=ROOT, env=dict(os.environ, FH_NATIVE_MENUS="1"), stdout=log, stderr=subprocess.STDOUT, timeout=120)
        assert result.returncode == 0, directory / "run.log"
        for name in ("dock", "expanded"):
            snapshot = json.loads((directory / f"{name}.json").read_text())
            nodes = [n for n in snapshot["nodes"] if n["rendered"] and not n["hidden"]]
            assert not any(n.get("name") == "sidebar_worktree_status" for n in nodes)
            footer = next(n["rect"] for n in nodes if n.get("name") == "status_bar_bg")
            history = next(n["rect"] for n in nodes if n.get("name") == "sidebar_log")
            assert abs(history["y"] + history["height"] - footer["y"]) < 1, (state, zoom, name, "Unused sidebar strip", history, footer)
            assert abs(footer["y"] + footer["height"] - snapshot["viewport"]["height"]) < .2
            if name == "expanded":
                counts = next(n for n in nodes if n.get("name") == "status_counts")
                assert counts["text"] == ("Working tree clean" if state == "clean" else "1 unstaged")
            assert (directory / f"{name}.png").read_bytes().startswith(b"\x89PNG\r\n\x1a\n")
        print(f"PASS {state} {zoom}%: no duplicate sidebar status, history reaches the fixed footer in dock and expanded views", flush=True)
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
(out / "result.json").write_text(json.dumps(dict(passed=True, binary_sha256=digest, states=["clean", "changed"], zooms=[100, 140, 200]), indent=2) + "\n")
