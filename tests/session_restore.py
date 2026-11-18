import argparse
import json
import os
from pathlib import Path
import subprocess

from reading_journey import ROOT, fixture, quick_open

parser = argparse.ArgumentParser()
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=False)
repo = out / "fixture"
fixture(repo)
config = out / "settings"
config.mkdir()


def replay(name, script):
    directory = out / name
    directory.mkdir()
    path = directory / "journey.e2e"
    path.write_text(script)
    with (directory / "run.log").open("w") as log:
        result = subprocess.run([str(ROOT / "output/floatinghotel.exe"), str(repo), "--test-mode", "--headless",
            f"--test-script={path}", f"--screenshot-dir={directory}", "--e2e-timeout=90"], cwd=ROOT,
            env=dict(os.environ, FH_TEST_SETTINGS_DIR=str(config)),
            stdout=log, stderr=subprocess.STDOUT, timeout=120)
    assert result.returncode == 0, (name, (directory / "run.log").read_text()[-2000:])
    return json.loads((directory / "initial.json").read_text())


# First run: open two files, then persist the session explicitly.
first_script = ("resize 1280 800\nwait_for_refresh\n" + quick_open("alpha.cpp") +
                quick_open("beta.cpp") + "wait_frames 5\nsave_window_state\nscreenshot initial\n")
replay("first", first_script)
saved = json.loads((config / "settings.json").read_text())
sessions = json.dumps(saved)
assert "alpha.cpp" in sessions and "beta.cpp" in sessions, saved.keys()

# Second run: only a few frames in — the restored document tabs/layout must
# already be present (session restore is synchronous on activation; file
# content fills in asynchronously behind it).
second = replay("second", "wait_frames 3\nscreenshot initial\n")
texts = [n.get("text", "") for n in second["nodes"] if n["rendered"] and not n["hidden"]]
assert "alpha.cpp" in texts, texts
assert "beta.cpp" in texts, texts
tabs = [n for n in second["nodes"] if n.get("name") == "content_tabs" and n["rendered"]]
assert tabs and tabs[0]["visible_rect"]["width"] > 100, tabs
print("PASS: reading session tabs/layout restored in the first frames after relaunch", flush=True)
