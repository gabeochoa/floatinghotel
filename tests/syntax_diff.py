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
def git(*command):
    return subprocess.check_output(["git", "-C", str(repo), *command], text=True).strip()
for command in [("init", "-q", "-b", "main"), ("config", "user.name", "Syntax fixture"),
                ("config", "user.email", "syntax@example.invalid"), ("config", "commit.gpgsign", "false")]:
    git(*command)
def write(version):
    (repo / "prefix.cpp").write_text("".join(("/*" if i == 4070 else "*/" if i == 4110 else
        f"body_{i:05} {version if i == 4097 else 'unchanged'}") + "\n" for i in range(1, 4201)))
write("base")
(repo / "sides.cpp").write_text("/*\nbody_side\nbody_context\n*/\n")
git("add", ".")
git("commit", "-qm", "Syntax base")
write("committed")
(repo / "sides.cpp").write_text("//\nbody_side\nbody_context\n//\n")
git("add", ".")
git("commit", "-qm", "Syntax review")
write("staged")
git("add", ".")
write("working")
before = git("diff"), git("diff", "--cached")
binary = ROOT / "output/floatinghotel.exe"
digest = hashlib.sha256(binary.read_bytes()).hexdigest()
def capture(label, count):
    return f"wait_for_refresh\nwait_frames 3\nwait_for_refresh\nwait_frames 15\nworkspace_checkpoint {count} {label}\nscreenshot {label}\n"
results = []
for zoom in [100, 140, 200]:
    directory = out / str(zoom)
    directory.mkdir()
    settings = directory / "settings"
    settings.mkdir()
    script = 'resize 1700 1100\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
    script += 'native_menu_action "Zoom In"\n' * ((zoom - 100) // 10)
    script += "screenshot zoom_ready\nclick_ui review_unstaged_changes\n" + capture("working", 1)
    script += "click_ui review_staged_changes\n" + capture("index", 2)
    script += 'click_text "Syntax review"\n' + capture("commit", 2)
    script += 'click_text "Split"\n' + capture("split", 2)
    script += "hover_ui commit_detail_scroll\nscroll_wheel 0 -100000\n" + capture("sides", 2)
    script += 'click_text "Unified"\n' + capture("unified_sides", 2)
    script += "bench_frames 120\nexpect_p99_below 20\n"
    path = directory / "journey.e2e"
    path.write_text(script)
    with (directory / "journey.log").open("w") as log:
        result = subprocess.run([str(binary), str(repo), "--test-mode", *(["--headless"] if args.snapshots else []),
            f"--test-script={path}", f"--screenshot-dir={directory}", "--e2e-timeout=180"], cwd=ROOT,
            env=dict(os.environ, FH_NATIVE_MENUS="1", FH_TEST_NATIVE_HIDDEN="1", FH_TEST_SETTINGS_DIR=str(settings)),
            stdout=log, stderr=subprocess.STDOUT, timeout=210)
    assert result.returncode == 0, directory / "journey.log"
    if not args.snapshots: assert "Native test window hidden=1 key=0" in (directory / "journey.log").read_text()
    for label in ["working", "index", "commit", "split", "sides", "unified_sides"]:
        layout = json.loads((directory / f"{label}.json").read_text())
        marker = "body_side" if "sides" in label else "body_04097"
        rows = [row for row in layout["reading_rows"] if marker in row["text"]]
        assert rows, (zoom, label)
        for row in rows:
            node = next(n for n in layout["nodes"] if n["id"] == row["id"] and n["rendered"])
            assert node["visible_rect"]["height"] > 0 and node["visible_rect"]["width"] > 0, (zoom, label, row)
            span = next(s for s in node["text_spans"] if marker in s["text"])
            expected = [117, 129, 142, 255] if marker == "body_04097" or row["side"] == 1 else [228, 230, 235, 255]
            assert span["color"] == expected, (zoom, label, row, span, expected)
            results.append(dict(zoom=zoom, label=label, side=row["side"], actual=span["color"]))
    assert git("diff") == before[0] and git("diff", "--cached") == before[1]
    print(f"PASS {zoom}%: working/index/commit prefix state and independent split sides", flush=True)
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
(out / "result.json").write_text(json.dumps(dict(passed=True, binary_sha256=digest, results=results), indent=2) + "\n")
