import argparse
import os
from pathlib import Path
import subprocess
import json
from reading_journey import ROOT

parser = argparse.ArgumentParser()
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=False)
repo = out / "fixture"
repo.mkdir()

def git(*args):
    subprocess.run(["git", "-C", str(repo), *args], check=True, capture_output=True)

git("init", "-q", "-b", "main")
git("config", "user.name", "Large reader fixture")
git("config", "user.email", "large@example.invalid")
git("config", "commit.gpgsign", "false")
(repo / "large.cpp").write_text("".join(f"int value_{i:04d} = {i:04d}; // source line {i:04d}\n" for i in range(4000)))
git("add", ".")
git("commit", "-qm", "Large reader fixture")
script = out / "journey.e2e"
script.write_text('resize 1600 1000\nwait_for_refresh\nkey CMD+P\nclick_ui file_picker_input\ntype "large.cpp"\nkey ENTER\nwait_for_refresh\nwait_frames 20\nscreenshot reader\nvalidate diff_metrics_bounded=true\nbench_frames 120\nexpect_p99_below 20\nvalidate diff_metrics_reused=true\n')
with (out / "run.log").open("w") as log:
    result = subprocess.run([str(ROOT / "output/floatinghotel.exe"), str(repo), "--test-mode", "--headless",
        f"--test-script={script}", f"--screenshot-dir={out}", "--e2e-timeout=120"], cwd=ROOT,
        env=dict(os.environ, FH_NATIVE_MENUS="1"), stdout=log, stderr=subprocess.STDOUT, timeout=150)
assert result.returncode == 0, out / "run.log"
snapshot = json.loads((out / "reader.json").read_text())
assert len(snapshot["reading_rows"]) < 200
print("PASS: 4,000 source lines keep bounded rendered rows, reused metrics and p99 below20ms", flush=True)
