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

repos = []
for name in ("alpha", "beta"):
    repo = out / name
    repo.mkdir()
    def git(*a):
        return subprocess.run(["git", "-C", str(repo), *a], check=True, capture_output=True, text=True)
    git("init", "-q", "-b", "main")
    git("config", "user.name", "Tab test")
    git("config", "user.email", "tabs@example.invalid")
    (repo / f"{name}-only.txt").write_text(f"{name}\n")
    git("add", ".")
    git("commit", "-qm", f"{name} baseline")
    (repo / f"{name}-only.txt").write_text(f"{name}\ndirty\n")
    repos.append(str(repo))

(out / "repos").write_text("\n".join(repos) + "\n")
script = out / "journey.e2e"
script.write_text(
    "wait_for_refresh\n"
    "wait 3\n"  # let the restored-tab warmer finish loading alpha
    "perf_mark before_switch\n"
    "hold_key 343\nkey 1\nrelease_key 343\n"
    "wait_frames 3\n"
    "perf_mark after_switch\n"
    "screenshot switched\n"
    'wait_until 30 expect_text "alpha-only.txt"\n'
    "perf_mark content_visible\n"
    "perf_report " + str(out / "perf.json") + "\n")
with (out / "run.log").open("w") as log:
    result = subprocess.run([str(ROOT / "output/floatinghotel.exe"), "--test-mode", "--headless",
        f"--test-script={script}", f"--screenshot-dir={out}", "--e2e-timeout=60"], cwd=ROOT,
        env=dict(os.environ, FH_NAVIGATION_REPOS=str(out / "repos")),
        stdout=log, stderr=subprocess.STDOUT, timeout=120)
assert result.returncode == 0, (out / "run.log").read_text()[-2000:]
marks = json.loads((out / "perf.json").read_text())["marks"]
delta = marks["after_switch"] - marks["before_switch"]
content = marks["content_visible"] - marks["before_switch"]
print(f"warm switch: tab in {delta:.1f} ms, file list visible at {content:.1f} ms")
assert delta < 100, delta
assert content < 1000, content
print("PASS: warm repo switch under 100 ms, content follows without a cold load", flush=True)
