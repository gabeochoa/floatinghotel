import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--output", type=Path, required=True)
parser.add_argument("--zooms", nargs="+", type=int, default=[100, 140, 200])
args = parser.parse_args()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=False)
repo = out / "fixture"
repo.mkdir()
(repo / "blocks.cpp").write_text("".join(f"void f{i}() {{\n    call_{i}();\n}}\n" for i in range(6000)))
for command in [("init", "-q", "-b", "main"), ("config", "user.name", "Folding fixture"),
                ("config", "user.email", "folding@example.invalid"), ("config", "commit.gpgsign", "false"),
                ("add", "."), ("commit", "-qm", "Many blocks")]:
    subprocess.run(["git", "-C", str(repo), *command], check=True, capture_output=True)
binary = ROOT / "output/floatinghotel.exe"
digest = hashlib.sha256(binary.read_bytes()).hexdigest()
def capture(label):
    return f"wait_for_refresh\nwait_frames 3\nwait_for_refresh\nwait_frames 15\nworkspace_checkpoint 2 {label}\nscreenshot {label}\n"
timing_failures = []
for zoom in args.zooms:
    directory = out / str(zoom)
    directory.mkdir()
    settings = directory / "settings"
    settings.mkdir()
    script = 'resize 1700 1100\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
    script += 'native_menu_action "Zoom In"\n' * ((zoom - 100) // 10)
    script += 'screenshot zoom_ready\nkey CMD+P\nwait_for_refresh\nscreenshot picker\ntype "blocks.cpp"\nkey ENTER\n' + capture("initial")
    for line in [1, 4, 7]: script += f"click_ui source_fold_{line}\n" + capture(f"fold_{line}")
    script += "bench_frames 120\nexpect_p99_below 20\n"
    script += 'key CTRL+G\nwait_frames 4\nclick_ui line_picker_input\nkey CMD+A\ntype "9000"\nkey ENTER\n' + capture("later")
    script += "bench_frames 120\nexpect_p99_below 20\nkey ALT+LEFT\n" + capture("back")
    script += "bench_frames 120\nexpect_p99_below 20\n"
    path = directory / "journey.e2e"
    path.write_text(script)
    with (directory / "journey.log").open("w") as log:
        result = subprocess.run([str(binary), str(repo), "--test-mode", f"--test-script={path}",
            f"--screenshot-dir={directory}", "--e2e-timeout=180"], cwd=ROOT,
            env=dict(os.environ, FH_NATIVE_MENUS="1", FH_TEST_NATIVE_HIDDEN="1", FH_TEST_SETTINGS_DIR=str(settings)),
            stdout=log, stderr=subprocess.STDOUT, timeout=210)
    log_text = (directory / "journey.log").read_text()
    if result.returncode:
        errors = [line for line in log_text.splitlines() if "[E2E ERROR]" in line or "[TIMEOUT]" in line]
        assert result.returncode == 1 and errors and all("[E2E ERROR] expect_p99_below" in line for line in errors), directory / "journey.log"
        timing_failures.append(dict(zoom=zoom, errors=errors))
    for label in ["initial", "fold_7", "later", "back"]:
        state = json.loads((directory / f"{label}.workspace.json").read_text())
        layout = json.loads((directory / f"{label}.json").read_text())
        assert state["source_pages"]["bounded"]
        assert len(state["source_folds"]["ranges"]) > 1000
        assert len(layout["reading_rows"]) < 300
        assert state["source_folds"]["retained"] == (0 if label == "initial" else 3)
        if label in ["fold_7", "back"]:
            assert not {2, 5, 8}.intersection(r["line"] for r in layout["reading_rows"])
    print(f"PASS {zoom}%: thousands of candidate ranges stay virtualized, with preserved folds across page navigation", flush=True)
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
(out / "result.json").write_text(json.dumps(dict(passed=not timing_failures, binary_sha256=digest, timing_failures=timing_failures), indent=2) + "\n")

assert not timing_failures, timing_failures
