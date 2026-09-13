import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--output", type=Path, required=True)
parser.add_argument("--snapshots", action="store_true")
args = parser.parse_args()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=False)
repo = out / "fixture"
repo.mkdir()
text = "start\r\n" + "éλ" * 300000 + "\r\nend"
(repo / "fragments.txt").write_bytes(text.encode())
for command in [("init", "-q", "-b", "main"), ("config", "user.name", "Fragment fixture"),
                ("config", "user.email", "fragments@example.invalid"), ("config", "commit.gpgsign", "false"),
                ("add", "."), ("commit", "-qm", "Fragment source")]:
    subprocess.run(["git", "-C", str(repo), *command], check=True, capture_output=True)
binary = ROOT / "output/floatinghotel.exe"
digest = hashlib.sha256(binary.read_bytes()).hexdigest()
timing_failures = []
for zoom in [100, 140, 200]:
    directory = out / str(zoom)
    directory.mkdir()
    settings = directory / "settings"
    settings.mkdir()
    def capture(label):
        return f"wait_for_refresh\nwait_frames 3\nwait_for_refresh\nwait_frames 15\nworkspace_checkpoint 2 {label}\nscreenshot {label}\n"
    script = 'resize 1700 1100\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
    script += 'native_menu_action "Zoom In"\n' * ((zoom - 100) // 10)
    script += 'screenshot zoom_ready\nkey CMD+P\nwait_for_refresh\nscreenshot picker\ntype "fragments.txt"\nkey ENTER\n' + capture("initial")
    for i in range(1, 5):
        script += "hover_ui diff_scroll\nscroll_wheel 0 -1000000\n" + capture(f"forward_{i}")
    script += "bench_frames 120\nexpect_p99_below 20\n"
    for i in range(1, 4):
        script += "hover_ui diff_scroll\nscroll_wheel 0 1000000\n" + capture(f"backward_{i}")
    path = directory / "journey.e2e"
    path.write_text(script)
    with (directory / "journey.log").open("w") as log:
        result = subprocess.run([str(binary), str(repo), "--test-mode", *(["--headless"] if args.snapshots else []),
            f"--test-script={path}", f"--screenshot-dir={directory}", "--e2e-timeout=240"], cwd=ROOT,
            env=dict(os.environ, FH_NATIVE_MENUS="1", FH_TEST_NATIVE_HIDDEN="1", FH_TEST_SETTINGS_DIR=str(settings)),
            stdout=log, stderr=subprocess.STDOUT, timeout=270)
    log_text = (directory / "journey.log").read_text()
    errors = [line for line in log_text.splitlines() if "[E2E ERROR]" in line or "[TIMEOUT]" in line]
    if result.returncode:
        assert result.returncode == 1 and errors and all("[E2E ERROR] expect_p99_below" in line for line in errors), directory / "journey.log"
        timing_failures.append(dict(case=directory.name, errors=errors))
    if not args.snapshots: assert "Native test window hidden=1 key=0" in log_text
    states = {}
    lines = text.splitlines(keepends=True)
    for label in ["initial", *[f"forward_{i}" for i in range(1, 5)], *[f"backward_{i}" for i in range(1, 4)]]:
        state = json.loads((directory / f"{label}.workspace.json").read_text())["source_pages"]
        states[label] = state
        assert state["bounded"] and not state["error"], (label, state)
        assert 1 <= len(state["pages"]) <= 3
        snapshot = json.loads((directory / f"{label}.json").read_text())
        rows = snapshot["reading_rows"]
        assert rows
        positions = [(r["line"], r["column"]) for r in rows]
        assert len(positions) == len(set(positions))
        for row in rows:
            assert lines[row["line"] - 1].removesuffix("\n")[row["column"] - 1:].startswith(row["text"]), (label, row)
    assert states["forward_3"]["pages"][0]["first_column"] > 1
    assert states["forward_4"]["pages"][-1]["end"] == len(text.encode())
    assert states["backward_3"]["pages"][0]["begin"] == 0
    assert (repo / "fragments.txt").read_bytes() == text.encode()
    print(f"PASS behavior {zoom}%: Unicode line fragments join, evict, and return without duplicate or missing text", flush=True)
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
(out / "result.json").write_text(json.dumps(dict(passed=not timing_failures, binary_sha256=digest, timing_failures=timing_failures), indent=2) + "\n")
assert not timing_failures, timing_failures
