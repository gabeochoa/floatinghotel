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
git("config", "user.name", "Wrap fixture")
git("config", "user.email", "wrap@example.invalid")
git("config", "commit.gpgsign", "false")
text = 'const char* message = "' + 'Unicode新しい🙂👨‍👩‍👧‍👦🇺🇸é words\tand spaces  ' * 12 + 'WRAPPED_END";\r'
(repo / "wrap.cpp").write_bytes((text + "\nint second_line = 2;").encode())
git("add", ".")
git("commit", "-qm", "Wrapped root")

for zoom, steps in ((100, 0), (140, 4), (200, 10)):
    for mode in ("unified", "split", "source"):
        directory = out / f"{zoom}-{mode}"
        directory.mkdir()
        setup = 'resize 1600 1100\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
        setup += 'native_menu_action "Zoom In"\n' * steps
        setup += 'click_text "Wrapped root"\nwait_for_refresh\n'
        if mode == "split":
            setup += 'click_text "Split"\nwait_frames 8\n'
        if mode == "source":
            setup += 'click_ui open_full_file\nwait_for_refresh\n'
        setup += 'screenshot wrapped\n'

        def replay(name, script):
            path = directory / (name + ".e2e")
            path.write_text(script)
            with (directory / (name + ".log")).open("w") as log:
                result = subprocess.run([str(ROOT / "output/floatinghotel.exe"), str(repo), "--test-mode", "--headless",
                    f"--test-script={path}", f"--screenshot-dir={directory}", "--e2e-timeout=90"], cwd=ROOT,
                    env=dict(os.environ, FH_NATIVE_MENUS="1"), stdout=log, stderr=subprocess.STDOUT, timeout=120)
            assert result.returncode == 0, directory

        replay("probe", setup)
        snapshot = json.loads((directory / "wrapped.json").read_text())
        nodes = {n["id"]: n for n in snapshot["nodes"]}
        rows = [r for r in snapshot["reading_rows"] if r["path"] == "wrap.cpp" and r["line"] == 1]
        assert len(rows) > 1 and rows[0]["offset"] == 0, (zoom, mode, rows)
        for previous, row in zip(rows, rows[1:]):
            assert row["offset"] == previous["offset"] + len(previous["text"].encode()), (previous, row)
            assert row["rect"]["y"] > previous["rect"]["y"]
        for row in rows:
            r = nodes[row["id"]]["rect"]
            assert r["x"] >= 0 and r["x"] + r["width"] <= 1601, r
            assert nodes[row["id"]]["measured_text_width"] <= r["width"] - 5, nodes[row["id"]]
        visible = [r for r in rows if nodes[r["id"]]["visible_rect"]["height"] >= r["rect"]["height"] - .1]
        assert len(visible) >= 3, (zoom, mode, visible)
        first, last = visible[:3][0], visible[:3][-1]
        x1, y1 = first["content_x"], first["rect"]["y"] + first["rect"]["height"] / 2
        x2, y2 = last["content_x"], last["rect"]["y"] + last["rect"]["height"] / 2
        scroll = "diff_scroll" if mode == "source" else "commit_detail_scroll"
        replay("select", setup + f"drag_to {x1} {y1} {x2} {y2}\nwait_frames 5\nscreenshot selected\n" +
            'key CMD+F\nwait_frames 5\nscreenshot seeded\nkey ESCAPE\nwait_frames 5\n' +
            'native_menu_action "Show Whitespace (toggle)"\nwait_frames 5\nscreenshot whitespace\n' +
            'native_menu_action "Show Whitespace (toggle)"\nwait_frames 5\n' +
            f"hover_ui {scroll}\nscroll_wheel 0 -100\nwait_frames 30\nscreenshot end\nbench_frames 120\nexpect_p99_below 20\n")
        selected = json.loads((directory / "selected.json").read_text())
        expected = text.encode()[first["offset"]:last["offset"]].decode()
        assert selected["selection_text"] == expected, (zoom, mode, selected["selection_text"], expected)
        assert selected["selection_location"] == "wrap.cpp:L1\n" + expected
        seeded = json.loads((directory / "seeded.json").read_text())
        field = next(n for n in seeded["nodes"] if n["focused"])
        assert field["text"] == expected, (zoom, mode, "Wrapped selection did not seed Find")
        assert seeded["reading_rows"] == selected["reading_rows"], (zoom, mode, "Find moved wrapped code")
        whitespace = json.loads((directory / "whitespace.json").read_text())
        assert whitespace["selection_text"] == "", "Reflow retained stale selection entities"
        for node in whitespace["nodes"]:
            if node.get("name") in ("diff_line", "sbs_cell") and node["rendered"]:
                assert node["measured_text_width"] <= node["rect"]["width"] - 5, (zoom, mode, node)
        end = json.loads((directory / "end.json").read_text())
        fragments = {r["offset"]: r["text"] for r in rows}
        fragments.update({r["offset"]: r["text"] for r in end["reading_rows"] if r["path"] == "wrap.cpp" and r["line"] == 1})
        assert "".join(fragments[offset] for offset in sorted(fragments)) == text
        assert any(r["line"] == 2 for r in end["reading_rows"])
        print(f"PASS {zoom}% {mode}: wrapped rows retain original line, UTF-8 and exact selection bytes", flush=True)
