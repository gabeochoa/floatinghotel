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
env = dict(os.environ, GIT_AUTHOR_DATE="2026-01-01T00:00:00Z", GIT_COMMITTER_DATE="2026-01-01T00:00:00Z")


def git(*args):
    return subprocess.run(["git", "-C", str(repo), *args], env=env, check=True, capture_output=True, text=True).stdout.strip()


git("init", "-q", "-b", "main")
git("config", "user.name", "Title fixture")
git("config", "user.email", "titles@example.invalid")
git("config", "commit.gpgsign", "false")
for parent in ("left", "right"):
    (repo / parent).mkdir()
    (repo / parent / "a.cpp").write_text(f'int {parent} = 1;\n')
git("add", ".")
git("commit", "-qm", "Original source")
(repo / "left/a.cpp").write_text('int left = 2;\n')
git("add", ".")
git("commit", "-qm", "Recognizable subject")
head = git("rev-parse", "HEAD")
(repo / "left/a.cpp").write_text('int left = 3;\n')
binary = ROOT / "output/floatinghotel.exe"
digest = hashlib.sha256(binary.read_bytes()).hexdigest()


def picker(path):
    return f'key CMD+P\nclick_ui file_picker_input\nkey CMD+A\ntype "{path}"\nwait_frames 3\nkey ENTER\nwait_for_refresh\n'


for zoom, steps in ((100, 0), (140, 4), (200, 10)):
    directory = out / str(zoom)
    directory.mkdir()
    script = f'resize {int(1800 * zoom / 100)} {int(800 * zoom / 100)}\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
    script += 'native_menu_action "Zoom In"\n' * steps
    script += 'click_text "Recognizable subject"\nwait_for_refresh\nkey ENTER\nworkspace_checkpoint 2 commit\nscreenshot commit\n'
    script += 'click_ui open_full_file\nwait_for_refresh\nkey ENTER\n'
    script += picker("left/a.cpp") + picker("right/a.cpp")
    script += 'workspace_checkpoint 5 files\nscreenshot files\n'
    script += 'click_ui content_document_3\nwait_for_refresh\nworkspace_checkpoint 5 historical\nscreenshot historical\n'
    script += 'click_ui content_document_4\nwait_for_refresh\nworkspace_checkpoint 5 working\nscreenshot working\n'
    script += 'click_ui content_document_2\nwait_for_refresh\nworkspace_checkpoint 5 return\nscreenshot return\n'
    script += 'bench_frames 120\nexpect_p99_below 20\n'
    (directory / "journey.e2e").write_text(script)
    with (directory / "run.log").open("w") as log:
        result = subprocess.run([str(binary), str(repo), "--test-mode", "--headless",
            f"--test-script={directory / 'journey.e2e'}", f"--screenshot-dir={directory}", "--e2e-timeout=90"],
            cwd=ROOT, env=dict(os.environ, FH_NATIVE_MENUS="1"), stdout=log, stderr=subprocess.STDOUT, timeout=120)
    assert result.returncode == 0, directory / "run.log"
    for name, active in (("commit", 2), ("files", 5), ("historical", 3), ("working", 4), ("return", 2)):
        workspace = json.loads((directory / f"{name}.workspace.json").read_text())
        assert workspace["active"] == active and workspace["inactive_payloads_empty"], workspace
        assert all(not tab["preview"] for tab in workspace["tabs"])
        tabs = {tab["id"]: tab for tab in workspace["tabs"]}
        assert tabs[2]["title"] == "Recognizable subject" and head in tabs[2]["tooltip"]
        expected = ["Recognizable subject"]
        if name != "commit":
            assert tabs[3]["title"] == tabs[4]["title"] == "a.cpp · left"
            assert tabs[5]["title"] == "a.cpp · right"
            assert tabs[3]["badge"] == head[:7] and tabs[4]["badge"] == "Working tree"
            expected += ["a.cpp · left", "a.cpp · right", head[:7], "Working tree"]
        snapshot = json.loads((directory / f"{name}.json").read_text())
        nodes = [node for node in snapshot["nodes"] if node["rendered"] and not node["hidden"]]
        strip = next(node for node in nodes if node.get("name") == "content_tabs")["rect"]
        labels = [node for node in nodes if strip["y"] <= node["rect"]["y"] < strip["y"] + strip["height"]]
        for text in expected:
            matches = [node for node in labels if node.get("text") == text]
            assert matches, (zoom, name, text)
            for node in matches:
                assert node["visible_rect"]["width"] >= node["rect"]["width"] - .2, (zoom, name, node)
                assert node["visible_rect"]["height"] > 0
                inset = node.get("text_inset", {}).get("x", 0) * zoom / 100
                assert node["rect"]["width"] + .2 >= node["measured_text_width"] + inset * 2, (zoom, name, "Ellipsized title", node)
        assert (directory / f"{name}.png").read_bytes().startswith(b"\x89PNG\r\n\x1a\n")
    print(f"PASS {zoom}%: subjects, duplicate paths, revision badges, mouse tab activation", flush=True)
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
(out / "result.json").write_text(json.dumps(dict(passed=True, binary_sha256=digest, zooms=[100, 140, 200]), indent=2) + "\n")
