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
binary = ROOT / "output/floatinghotel.exe"
digest = hashlib.sha256(binary.read_bytes()).hexdigest()
checkpoints = {
    "first": (2, 2, [2]),
    "replaced": (2, 3, [3]),
    "kept_review": (2, 3, []),
    "source": (3, 4, [4]),
    "second_source": (3, 5, [5]),
    "kept_source": (3, 5, []),
    "third_source": (4, 6, [6]),
    "reused": (4, 5, [6]),
    "kept_from_menu": (4, 5, []),
}


def capture(name):
    return f"workspace_checkpoint {checkpoints[name][0]} {name}\nscreenshot {name}\n"


def picker(path, keep=False):
    action = "key ENTER" if keep else "click_ui file_picker_result"
    return f'key CMD+P\nclick_ui file_picker_input\nkey CMD+A\ntype "{path}"\nwait_frames 3\n{action}\nwait_for_refresh\n'


for zoom, steps in ((100, 0), (140, 4), (200, 10)):
    directory = out / str(zoom)
    directory.mkdir()
    script = 'make_test_repo\nresize 1600 1000\nwait_for_refresh\n'
    script += 'native_menu_action "Reset Zoom"\n' + 'native_menu_action "Zoom In"\n' * steps
    script += 'click_text "Add contributing guidelines"\nwait_for_refresh\n' + capture("first")
    script += 'click_text "Add unit tests for utils"\nwait_for_refresh\n' + capture("replaced")
    script += 'key ENTER\nwait_frames 2\n' + capture("kept_review")
    script += 'click_ui open_full_file\nwait_for_refresh\n' + capture("source")
    script += picker("README.md") + capture("second_source")
    script += 'click_ui content_document_5\nwait_frames 1\nclick_ui content_document_5\nwait_frames 2\n' + capture("kept_source")
    script += picker("CONTRIBUTING.md") + capture("third_source")
    script += picker("README.md", True) + capture("reused")
    script += 'click_ui scroll_tabs_right\nwait_frames 3\nright_click_ui content_document_6\nwait_frames 2\nclick_text "Keep Open"\nwait_frames 2\n' + capture("kept_from_menu")
    script += 'bench_frames 120\nexpect_p99_below 20\n'
    path = directory / "journey.e2e"
    path.write_text(script)
    with (directory / "run.log").open("w") as log:
        result = subprocess.run([str(binary), "--test-mode", "--headless", f"--test-script={path}",
            f"--screenshot-dir={directory}", "--e2e-timeout=90"], cwd=ROOT,
            env=dict(os.environ, FH_NATIVE_MENUS="1"), stdout=log, stderr=subprocess.STDOUT, timeout=120)
    assert result.returncode == 0, directory / "run.log"
    for name, (count, active, previews) in checkpoints.items():
        workspace = json.loads((directory / (name + ".workspace.json")).read_text())
        assert workspace["active"] == active, (zoom, name, workspace)
        assert len(workspace["tabs"]) == count and workspace["inactive_payloads_empty"]
        assert [tab["id"] for tab in workspace["tabs"] if tab["preview"]] == previews, (zoom, name, workspace)
        layout = json.loads((directory / (name + ".json")).read_text())
        nodes = [node for node in layout["nodes"] if node["rendered"] and not node["hidden"]]
        tabs = [node for node in nodes if node.get("name", "").startswith("content_document_")]
        assert 1 <= len(tabs) <= count
        if name in ("first", "replaced", "kept_review"):
            assert next(tab for tab in tabs if tab["name"] == f"content_document_{active}")["focused"], (zoom, name, "Reader tab lost focus")
        for tab in tabs:
            assert tab["visible_rect"]["width"] <= tab["rect"]["width"] + .2, (zoom, name, tab)
        if name != "kept_from_menu":
            activeTab = next(tab for tab in tabs if tab["name"] == f"content_document_{active}")
            assert activeTab["visible_rect"]["width"] >= activeTab["rect"]["width"] - .2, (zoom, name, "Active tab clipped")
        assert sum(node.get("text", "").startswith("Preview · ") for node in nodes) <= len(previews), (zoom, name)
        assert (directory / (name + ".png")).read_bytes().startswith(b"\x89PNG\r\n\x1a\n")
    assert [tab["id"] for tab in workspace["tabs"]] == [1, 3, 5, 6]
    print(f"PASS {zoom}%: one preview, Enter/double-click/menu keep, origin retention and destination reuse", flush=True)
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
(out / "result.json").write_text(json.dumps(dict(passed=True, binary_sha256=digest, zooms=[100, 140, 200]), indent=2) + "\n")
