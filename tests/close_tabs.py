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
for name in ("a", "b", "c"):
    (repo / f"{name}.cpp").write_text("".join(f"int {name}_{i} = {i};\n" for i in range(500)))
for command in (("init", "-q", "-b", "main"), ("config", "user.name", "Close fixture"),
                ("config", "user.email", "close@example.invalid"), ("config", "commit.gpgsign", "false"),
                ("add", "."), ("commit", "-qm", "Reading files")):
    subprocess.run(["git", "-C", str(repo), *command], check=True, capture_output=True)
p = repo / "a.cpp"
p.write_text(p.read_text().replace("a_11 = 11", "a_11 = 999"))
binary = ROOT / "output/floatinghotel.exe"
digest = hashlib.sha256(binary.read_bytes()).hexdigest()
checkpoints = {"a": (2, 2), "b": (3, 3), "c": (4, 4), "inactive_closed": (3, 4),
               "reopened_b": (4, 3), "closed_b": (3, 4), "only_a": (1, 2),
               "fallback": (1, 5), "reopened_a": (2, 2)}


def capture(name):
    return f'workspace_checkpoint {checkpoints[name][0]} {name}\nscreenshot {name}\n'


for zoom, steps in ((100, 0), (140, 4), (200, 10)):
    directory = out / str(zoom)
    directory.mkdir()
    script = 'resize 1600 1000\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
    script += 'native_menu_action "Zoom In"\n' * steps
    for name, wheel in (("a", 20), ("b", 30), ("c", 40)):
        script += f'key CMD+P\nclick_ui file_picker_input\nkey CMD+A\ntype "{name}.cpp"\nwait_frames 2\nkey ENTER\nwait_for_refresh\n'
        script += f'hover_ui diff_scroll\nscroll_wheel 0 -{wheel}\nwait_frames 20\n' + capture(name)
    script += 'click_ui close_document_3\nwait_frames 5\n' + capture("inactive_closed")
    script += 'key CMD+SHIFT+T\nwait_for_refresh\nwait_frames 5\n' + capture("reopened_b")
    script += 'key CMD+W\nwait_for_refresh\nwait_frames 5\n' + capture("closed_b")
    script += 'click_ui scroll_tabs_left\nwait_frames 3\nright_click_ui content_document_2\nwait_frames 3\nclick_text "Close Others"\nwait_for_refresh\nwait_frames 5\n' + capture("only_a")
    script += 'key CMD+W\nwait_for_refresh\nwait_frames 5\n' + capture("fallback")
    script += 'key CMD+SHIFT+T\nwait_for_refresh\nwait_frames 5\n' + capture("reopened_a")
    script += 'bench_frames 120\nexpect_p99_below 20\n'
    (directory / "journey.e2e").write_text(script)
    with (directory / "run.log").open("w") as log:
        result = subprocess.run([str(binary), str(repo), "--test-mode", "--headless",
            f"--test-script={directory / 'journey.e2e'}", f"--screenshot-dir={directory}", "--e2e-timeout=90"],
            cwd=ROOT, env=dict(os.environ, FH_NATIVE_MENUS="1"), stdout=log, stderr=subprocess.STDOUT, timeout=120)
    assert result.returncode == 0, directory / "run.log"
    offsets = {}
    for name, (count, active) in checkpoints.items():
        workspace = json.loads((directory / f"{name}.workspace.json").read_text())
        assert workspace["active"] == active and len(workspace["tabs"]) == count and workspace["inactive_payloads_empty"], (zoom, name, workspace)
        assert all(not tab["preview"] for tab in workspace["tabs"])
        snapshot = json.loads((directory / f"{name}.json").read_text())
        nodes = [n for n in snapshot["nodes"] if n["rendered"] and not n["hidden"]]
        tab = next(n for n in nodes if n.get("name") == f"content_document_{active}")
        assert tab["visible_rect"]["width"] >= tab["rect"]["width"] - .2, (zoom, name, "Active tab clipped")
        assert next(n for n in nodes if n.get("name") == "main_content")["rect"]["height"] > 100
        if name == "fallback":
            assert workspace["tabs"][0]["kind"] == "review" and workspace["tabs"][0]["revision"] == "wt"
            assert any(n.get("name") == "file_header_label" and n.get("text") == "a.cpp" for n in nodes)
        else:
            offsets[name] = next(n for n in nodes if n.get("name") == "diff_scroll")["scroll"]["y"]
            assert offsets[name] > 100, (zoom, name, "Reading position reset")
        assert (directory / f"{name}.png").read_bytes().startswith(b"\x89PNG\r\n\x1a\n")
    for before, after in (("c", "inactive_closed"), ("b", "reopened_b"), ("c", "closed_b"), ("a", "only_a"), ("a", "reopened_a")):
        assert abs(offsets[before] - offsets[after]) < 1, (zoom, before, after, offsets)
    print(f"PASS {zoom}%: inactive close, Cmd+W, Cmd+Shift+T, nearest neighbor, exact scroll restoration, working-changes fallback", flush=True)
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
(out / "result.json").write_text(json.dumps(dict(passed=True, binary_sha256=digest, zooms=[100, 140, 200]), indent=2) + "\n")
