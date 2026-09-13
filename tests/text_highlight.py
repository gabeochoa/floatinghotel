import argparse
import json
import os
from pathlib import Path
import subprocess

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
git("config", "user.name", "Highlight fixture")
git("config", "user.email", "highlight@example.invalid")
git("config", "commit.gpgsign", "false")
tail = "".join(f"row{i:02d} abcdefghijklmnopqrstuvwxyz\n" for i in range(1, 81))
(repo / "sample.txt").write_text("0123XX6789 abcdefghijklmnopqrstuvwxyz\n" + tail)
git("add", ".")
git("commit", "-qm", "Highlight fixture")
(repo / "sample.txt").write_text("0123456789 abcdefghijklmnopqrstuvwxyz\nsecond line for selection\n" + tail)

for zoom, steps in ((100, 0), (140, 4), (200, 10)):
    for mode in ("unified", "split", "source"):
        directory = out / f"{zoom}-{mode}"
        directory.mkdir()
        setup = 'resize 1600 1100\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
        setup += 'native_menu_action "Zoom In"\n' * steps
        setup += 'click_ui review_unstaged_changes\nwait_for_refresh\n'
        if mode == "split":
            setup += 'click_text "Split"\nwait_frames 8\n'
        if mode == "source":
            setup += 'key CMD+P\nwait_frames 3\ntype "sample.txt"\nwait_for_refresh\nkey ENTER\nwait_for_refresh\nwait_frames 3\nwait_for_refresh\n'
        setup += 'mouse_move 1500 20\nwait_frames 5\nscreenshot before\n'

        def replay(name, script):
            path = directory / (name + ".e2e")
            path.write_text(script)
            with (directory / (name + ".log")).open("w") as log:
                result = subprocess.run([str(ROOT / "output/floatinghotel.exe"), str(repo), "--test-mode", "--headless",
                    f"--test-script={path}", f"--screenshot-dir={directory}", "--e2e-timeout=90"], cwd=ROOT,
                    env=dict(os.environ, FH_NATIVE_MENUS="1"), stdout=log, stderr=subprocess.STDOUT, timeout=120)
            assert result.returncode == 0, directory

        def row(snapshot, sign, line=1):
            return next(r for r in snapshot["reading_rows"] if r["path"] == "sample.txt" and r["line"] == line and r["sign"] == sign)

        def glyph_width(snapshot, record):
            node = next(n for n in snapshot["nodes"] if n["id"] == record["id"])
            return node["measured_text_width"] / len(node["text"])

        bottom_probe = 'hover_ui diff_scroll\nscroll_wheel 0 -10000\nwait_frames 30\nscreenshot bottom_probe\n' if mode == "source" else ""
        replay("probe", setup + bottom_probe)
        before = json.loads((directory / "before.json").read_text())
        sign = " " if mode == "source" else "+"
        record = row(before, sign)
        width = glyph_width(before, record)
        x, y = record["content_x"], record["rect"]["y"] + record["rect"]["height"] / 2
        script = setup + f"drag_to {x} {y} {x + 10 * width} {y}\nwait_frames 5\nscreenshot selected\n"
        script += f"drag_to {x + 10 * width} {y} {x} {y}\nwait_frames 5\nscreenshot reversed\n"
        if mode != "source":
            old = row(before, "-")
            old_x, old_y = old["content_x"], old["rect"]["y"] + old["rect"]["height"] / 2
            script += f"drag_to {old_x} {old_y} {old_x + 10 * width} {old_y}\nwait_frames 5\nscreenshot deleted\n"
            for current in (record, old):
                change = next(n for n in before["nodes"] if n.get("name") == "intraline_change" and n["parent"] == current["id"] and n["rendered"])
                assert abs(change["rect"]["x"] - (current["content_x"] + 4 * width)) < 1
                assert abs(change["rect"]["width"] - 2 * width) < 1
        script += 'key CMD+F\nclick_ui diff_find_input\nkey CMD+A\ntype "3456"\nwait_frames 5\nscreenshot found\n'
        if mode == "source":
            script += 'click_ui diff_find_close\nhover_ui diff_scroll\nscroll_wheel 0 -10000\nwait_frames 30\nscreenshot bottom\n'
            bottom = json.loads((directory / "bottom_probe.json").read_text())
            last = row(bottom, " ", 82)
            assert last["rect"]["y"] + last["rect"]["height"] <= 1100
            bx, by = last["content_x"], last["rect"]["y"] + last["rect"]["height"] / 2
            script += f"drag_to {bx} {by} {bx + 5 * width} {by}\nwait_frames 5\nscreenshot bottom_selected\n"
        replay("interact", script)
        if mode != "source":
            deleted = json.loads((directory / "deleted.json").read_text())
            assert deleted["selection_text"] == "0123XX6789"
        for name in ("selected", "reversed"):
            snapshot = json.loads((directory / (name + ".json")).read_text())
            assert snapshot["selection_text"] == "0123456789", (zoom, mode, name, snapshot["selection_text"])
            if mode != "source":
                first_header = next(n["rect"] for n in before["nodes"] if n.get("name") == "file_header_row" and n["rendered"])
                current_header = next(n["rect"] for n in snapshot["nodes"] if n.get("name") == "file_header_row" and n["rendered"])
                assert first_header == current_header, "Selecting code moved the reading surface"
            highlight = next(n for n in snapshot["nodes"] if n.get("name") == "diff_sel_hl" and n["rendered"])
            assert abs(highlight["rect"]["x"] - x) < 1
            assert abs(highlight["rect"]["width"] - width * 10) < 1
        found = json.loads((directory / "found.json").read_text())
        record = row(found, sign)
        match = next(n for n in found["nodes"] if n.get("name") == "diff_find_match" and n["rendered"])
        assert abs(match["rect"]["x"] - (record["content_x"] + 3 * width)) < 1
        assert abs(match["rect"]["width"] - 4 * width) < 1
        if mode == "source":
            bottom = json.loads((directory / "bottom.json").read_text())
            assert row(bottom, " ", 82)["text"].startswith("row80")
            selected_bottom = json.loads((directory / "bottom_selected.json").read_text())
            assert selected_bottom["selection_text"] == "row80", selected_bottom["selection_text"]
        print(f"PASS {zoom}% {mode}: forward/reverse selection, Find geometry and source ending", flush=True)
