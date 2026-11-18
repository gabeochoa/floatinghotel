import argparse
import json
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[1]

parser = argparse.ArgumentParser()
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=False)


def build(name, mutate):
    repo = out / name
    repo.mkdir()

    def git(*a):
        return subprocess.run(["git", "-C", str(repo), *a], check=True, capture_output=True, text=True)

    git("init", "-q", "-b", "main")
    git("config", "user.name", "Settle Fixture")
    git("config", "user.email", "settle@fixture.invalid")
    git("config", "commit.gpgsign", "false")
    for i in range(150):
        (repo / f"file{i:03}.cpp").write_text("".join(f"int v_{i}_{k} = {k};\n" for k in range(40)))
    git("add", ".")
    git("commit", "-qm", "root")
    mutate(repo)
    return repo


def measure(name, repo):
    script = out / f"{name}.e2e"
    script.write_text(
        "perf_mark start\nwait_for_refresh\nperf_mark settled\n"
        f"perf_report {out / (name + '.json')}\n")
    with (out / f"{name}.log").open("w") as log:
        result = subprocess.run([str(ROOT / "output/floatinghotel.exe"), str(repo), "--test-mode", "--headless",
            f"--test-script={script}", "--e2e-timeout=120"], cwd=ROOT,
            stdout=log, stderr=subprocess.STDOUT, timeout=300)
    assert result.returncode == 0, (out / f"{name}.log").read_text()[-1500:]
    marks = json.loads((out / f"{name}.json").read_text())["marks"]
    return marks["settled"] - marks["start"]


modified = build("modified150", lambda repo: [
    (repo / f"file{i:03}.cpp").write_text(
        (repo / f"file{i:03}.cpp").read_text().replace("= 20;", "= 2000;")) for i in range(150)])
untracked = build("untracked150", lambda repo: [
    (repo / f"new{i:03}.cpp").write_text("int x = 1;\n") for i in range(150)])

for name, repo in (("modified150", modified), ("untracked150", untracked)):
    print(f"{name}: settle {measure(name, repo):.0f} ms", flush=True)
print("PASS: untracked settle re-measured with the perf harness", flush=True)
