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
repo = out / "fixture"
repo.mkdir()


def git(*a, **kw):
    return subprocess.run(["git", "-C", str(repo), *a], check=True, capture_output=True, text=True, **kw)


git("init", "-q", "-b", "main")
git("config", "user.name", "Large Fixture")
git("config", "user.email", "large@fixture.invalid")
git("config", "commit.gpgsign", "false")
for i in range(2000):
    path = repo / f"src/mod{i % 25}/file{i:04}.cpp"
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text("".join(f"int v_{i}_{k} = {k};\n" for k in range(80)))
git("add", ".")
git("commit", "-qm", "Large root")
# Dirty every file (unstaged): the whole-repo diff now has 2000 files.
for i in range(2000):
    path = repo / f"src/mod{i % 25}/file{i:04}.cpp"
    path.write_text(path.read_text().replace("= 40;", "= 4000;"))

script = out / "journey.e2e"
script.write_text(
    "perf_mark start\n"
    "wait_frames 3\n"
    'wait_until 60 click_text "file0000.cpp"\n'
    'wait_until 30 expect_text "v_0_40 = 4000"\n'
    "screenshot first_diff\n"
    "perf_mark first_diff\n"
    "perf_report " + str(out / "perf.json") + "\n")
with (out / "run.log").open("w") as log:
    result = subprocess.run([str(ROOT / "output/floatinghotel.exe"), str(repo), "--test-mode", "--headless",
        f"--test-script={script}", f"--screenshot-dir={out}", "--e2e-timeout=120"], cwd=ROOT,
        stdout=log, stderr=subprocess.STDOUT, timeout=300)
assert result.returncode == 0, (out / "run.log").read_text()[-2000:]
perf = json.loads((out / "perf.json").read_text())
print("marks:", perf["marks"])
layout = json.loads((out / "first_diff.json").read_text())
texts = [n.get("text", "") for n in layout["nodes"] if n["rendered"]]
assert any("v_0_40 = 4000" in t for t in texts), "selected file diff not visible yet"
print("PASS: first selected-file diff visible on a 2000-file unstaged repo", flush=True)
