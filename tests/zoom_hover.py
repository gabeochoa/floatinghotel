import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]


def check(directory, zoom):
    neutral = Image.open(directory / "neutral.png").convert("RGB")
    for label, target in (("file", "commit_changed_file"), ("icon", "commit_changed_file"),
                          ("count", "commit_changed_file"), ("close", "close_document_2")):
        snapshot = json.loads((directory / f"hover_{label}.json").read_text())
        assert abs(snapshot["ui_scale"] - zoom / 100) < .001
        nodes = [node for node in snapshot["nodes"] if node["rendered"] and not node["hidden"]]
        hot = [node for node in nodes if node["hot"]]
        assert len(hot) == 1 and hot[0].get("name") == target, (zoom, label, hot)
        rect = hot[0]["visible_rect"]
        pointer = snapshot["pointer"]
        assert rect["x"] <= pointer["x"] < rect["x"] + rect["width"]
        assert rect["y"] <= pointer["y"] < rect["y"] + rect["height"]
        if label == "close":
            glyph = next(n["rect"] for n in nodes if n.get("name") == "document_close_glyph" and n["parent"] == hot[0]["id"])
            scale = zoom / 100
            assert abs(glyph["height"] - 20 * scale) < .1
            assert abs(glyph["x"] + glyph["width"] / 2 - rect["x"] - rect["width"] / 2) < .1
            assert abs(glyph["y"] + glyph["height"] / 2 - rect["y"] - rect["height"] / 2) < .1
            assert rect["height"] > glyph["height"] + 10 * scale
            hovered = Image.open(directory / "hover_close.png").convert("RGB")
            center_x = int(rect["x"] + rect["width"] / 2)
            for y in (int(rect["y"] + 2 * scale), int(rect["y"] + rect["height"] - 4 * scale)):
                assert neutral.getpixel((center_x, y)) == hovered.getpixel((center_x, y)), "Full-height close hover"
            point = (int(glyph["x"] + 2 * scale), int(glyph["y"] + glyph["height"] / 2))
            assert neutral.getpixel(point) != hovered.getpixel(point), "Missing compact close hover"
            outer = (int(rect["x"] + 1), int(rect["y"] + 2 * scale))
            neighbor = (int(rect["x"] - 1), outer[1])
            assert neutral.getpixel(outer) == neutral.getpixel(neighbor), "Close control has a permanent fill"
            continue
        hovered = Image.open(directory / f"hover_{label}.png").convert("RGB")
        y = int(rect["y"] + rect["height"] / 2)
        for x in (int(rect["x"] + 5), int(rect["x"] + rect["width"] - 5)):
            assert neutral.getpixel((x, y)) != hovered.getpixel((x, y)), (zoom, label, x, y)
        for other in nodes:
            if other.get("name") != target or other["id"] == hot[0]["id"]:
                continue
            r = other["visible_rect"]
            if r["height"] < 20:
                continue
            point = (int(r["x"] + 5), int(r["y"] + r["height"] / 2))
            assert neutral.getpixel(point) == hovered.getpixel(point), (zoom, label, "neighbor")
    neutral_layout = json.loads((directory / "neutral.json").read_text())
    by_id = {n["id"]: n for n in neutral_layout["nodes"]}
    for row in neutral_layout["nodes"]:
        if row.get("name") != "commit_row" or not row["rendered"]:
            continue
        children = [n for n in neutral_layout["nodes"] if n.get("parent") in by_id and by_id[n["parent"]].get("parent") == row["id"]]
        dot = next(n["rect"] for n in children if n.get("name") == "commit_dot")
        subject = next(n["rect"] for n in children if n.get("name") == "commit_subject")
        gap = (subject["x"] - dot["x"] - dot["width"]) / (zoom / 100)
        assert 0 <= gap <= 8, (zoom, "Commit title gap", gap)
    selected = json.loads((directory / "selected.json").read_text())
    assert any(n.get("name") == "commit_detail_subject" and n.get("text") == "Add contributing guidelines"
               and n["visible_rect"]["height"] > 0 for n in selected["nodes"])


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    args.output.mkdir(parents=True, exist_ok=False)
    binary = ROOT / "output/floatinghotel.exe"
    digest = hashlib.sha256(binary.read_bytes()).hexdigest()
    for zoom, steps in ((100, 0), (140, 4), (200, 10)):
        directory = args.output.resolve() / str(zoom)
        directory.mkdir()
        script = "make_test_repo\nresize 1600 1000\nwait_for_refresh\n"
        script += 'click_text "Add contributing guidelines"\nwait_for_refresh\n'
        script += 'native_menu_action "Reset Zoom"\n' + 'native_menu_action "Zoom In"\n' * steps
        script += 'mouse_move 1500 20\nwait_frames 5\nscreenshot neutral\n'
        for label, target in (("file", "commit_changed_file"), ("icon", "tree_file_type"),
                              ("count", "tree_additions"), ("close", "close_document_2")):
            script += f"hover_ui {target}\nwait_frames 3\nscreenshot hover_{label}\n"
        script += 'click_ui tree_file_type\nwait_for_refresh\nvalidate selected_file=CONTRIBUTING.md\nscreenshot selected\n'
        (directory / "journey.e2e").write_text(script)
        with (directory / "run.log").open("w") as log:
            result = subprocess.run([str(binary), "--test-mode", "--headless",
                f"--test-script={directory / 'journey.e2e'}", f"--screenshot-dir={directory}", "--e2e-timeout=90"],
                cwd=ROOT, env=dict(os.environ, FH_NATIVE_MENUS="1"), stdout=log, stderr=subprocess.STDOUT, timeout=120)
        assert result.returncode == 0, directory
        check(directory, zoom)
        neutral_layout = json.loads((directory / "neutral.json").read_text())
        close = next(n["visible_rect"] for n in neutral_layout["nodes"] if n.get("name") == "close_document_2" and n["rendered"])
        x, y = close["x"] + close["width"] / 2, close["y"] + 2 * zoom / 100
        edge_script = script.split("mouse_move 1500 20")[0] + f"mouse_move {x} {y}\nwait_frames 5\nscreenshot close_edge\nclick {x} {y}\nwait_for_refresh\nscreenshot closed_at_edge\n"
        edge_path = directory / "edge.e2e"
        edge_path.write_text(edge_script)
        with (directory / "edge.log").open("w") as log:
            result = subprocess.run([str(binary), "--test-mode", "--headless", f"--test-script={edge_path}",
                f"--screenshot-dir={directory}", "--e2e-timeout=90"], cwd=ROOT, env=dict(os.environ, FH_NATIVE_MENUS="1"),
                stdout=log, stderr=subprocess.STDOUT, timeout=120)
        assert result.returncode == 0, (zoom, "edge")
        edge_image = Image.open(directory / "close_edge.png").convert("RGB")
        neutral = Image.open(directory / "neutral.png").convert("RGB")
        sample = (int(close["x"] + 4 * zoom / 100), int(close["y"] + close["height"] / 2))
        assert edge_image.getpixel(sample) == neutral.getpixel(sample), "Hover outside the glyph changed its background"
        closed = json.loads((directory / "closed_at_edge.json").read_text())
        assert not any(n.get("name") == "content_document_2" and n["rendered"] for n in closed["nodes"]), "Full-height close target did not close tab"
        print(f"PASS {zoom}%: row, icon, count and close hover match rendered targets; icon click selects correct file", flush=True)
    assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
    (args.output / "result.json").write_text(json.dumps(dict(passed=True, binary_sha256=digest, zooms=[100, 140, 200]), indent=2) + "\n")


if __name__ == "__main__":
    main()
