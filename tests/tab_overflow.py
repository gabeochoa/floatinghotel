import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

from PIL import Image

ROOT = Path(__file__).resolve().parents[1]
parser = argparse.ArgumentParser()
parser.add_argument("--output", type=Path, required=True)
args = parser.parse_args()
out = args.output.resolve()
out.mkdir(parents=True, exist_ok=False)
repo = out / "fixture"
repo.mkdir()
files = [f"source_{i:02d}_with_a_long_but_distinct_filename.cpp" for i in range(24)]
for name in files:
    (repo / name).write_text("".join(f"int file_{files.index(name)}_{line} = {line};\n" for line in range(100)))
for args in (("init", "-q", "-b", "main"), ("config", "user.name", "Tab fixture"),
             ("config", "user.email", "tabs@example.invalid"), ("config", "commit.gpgsign", "false"),
             ("add", "."), ("commit", "-qm", "Many sources")):
    subprocess.run(["git", "-C", str(repo), *args], check=True, capture_output=True)
binary = ROOT / "output/floatinghotel.exe"
digest = hashlib.sha256(binary.read_bytes()).hexdigest()


def capture(name):
    return f'workspace_checkpoint 25 {name}\nscreenshot {name}\n'


for zoom, steps in ((100, 0), (140, 4), (200, 10)):
    directory = out / str(zoom)
    directory.mkdir()
    script = 'resize 1200 850\nwait_for_refresh\nnative_menu_action "Reset Zoom"\n'
    script += 'native_menu_action "Zoom In"\n' * steps
    for path in files:
        script += f'key CMD+P\nclick_ui file_picker_input\nkey CMD+A\ntype "{path}"\nwait_frames 2\nkey ENTER\nwait_for_refresh\n'
    script += capture("last")
    script += 'click_ui scroll_tabs_left\nwait_frames 3\n' + capture("scrolled")
    script += 'hover_ui content_tab_viewport\nscroll_wheel 3 0\nwait_frames 3\n' + capture("wheel")
    script += 'click_ui open_tabs_menu\nwait_frames 3\nscreenshot menu\nkey DOWN\nkey ENTER\nwait_for_refresh\n' + capture("first")
    script += 'click_ui open_tabs_menu\nwait_frames 3\nkey UP\nscreenshot menu_last\nkey ENTER\nwait_for_refresh\n' + capture("last_keyboard")
    script += 'click_ui scroll_tabs_left\nwait_frames 3\nclick_ui open_tabs_menu\nwait_frames 3\nkey UP\nkey ENTER\nwait_for_refresh\n' + capture("same_revealed")
    script += 'click_ui open_tabs_menu\nwait_frames 3\nhover_ui context_menu\nscroll_wheel 0 -40\nwait_frames 3\nscreenshot menu_scrolled\n'
    script += f'click_ui "context_menu_item_{files[-2]}"\nwait_for_refresh\n' + capture("mouse")
    script += 'resize 950 700\nwait_frames 5\n' + capture("narrow")
    script += 'click_ui open_tabs_menu\nwait_frames 3\nkey ESCAPE\nwait_frames 3\n' + capture("dismissed")
    script += 'bench_frames 120\nexpect_p99_below 20\n'
    (directory / "journey.e2e").write_text(script)
    with (directory / "run.log").open("w") as log:
        result = subprocess.run([str(binary), str(repo), "--test-mode", "--headless",
            f"--test-script={directory / 'journey.e2e'}", f"--screenshot-dir={directory}", "--e2e-timeout=180"],
            cwd=ROOT, env=dict(os.environ, FH_NATIVE_MENUS="1"), stdout=log, stderr=subprocess.STDOUT, timeout=210)
    assert result.returncode == 0, directory / "run.log"
    positions = {}
    identity = None
    main_rect = None
    for name, active in (("last", 25), ("scrolled", 25), ("wheel", 25), ("first", 1),
                         ("last_keyboard", 25), ("same_revealed", 25), ("mouse", 24), ("narrow", 24), ("dismissed", 24)):
        workspace = json.loads((directory / f"{name}.workspace.json").read_text())
        assert workspace["active"] == active and workspace["inactive_payloads_empty"], (zoom, name, workspace)
        if identity is None:
            identity = workspace["tabs"]
        assert workspace["tabs"] == identity and len(identity) == 25
        snapshot = json.loads((directory / f"{name}.json").read_text())
        nodes = [n for n in snapshot["nodes"] if n["rendered"] and not n["hidden"]]
        viewport = next(n for n in nodes if n.get("name") == "content_tab_viewport")
        positions[name] = viewport["scroll"]["x"]
        assert viewport["scroll"]["y"] == 0
        assert viewport["scroll"]["content_width"] > viewport["rect"]["width"]
        if name not in ("scrolled", "wheel"):
            tab = next(n for n in nodes if n.get("name") == f"content_document_{active}")
            assert tab["visible_rect"]["width"] >= tab["rect"]["width"] - .2, (zoom, name, tab)
            close = next(n for n in nodes if n.get("name") == f"close_document_{active}")
            assert close["visible_rect"]["width"] >= close["rect"]["width"] - .2
        rect = next(n["rect"] for n in nodes if n.get("name") == "main_content")
        if main_rect is None:
            main_rect = rect
        if name not in ("narrow", "dismissed"):
            assert rect == main_rect, (zoom, name, "Main window moved during tab navigation")
        assert (directory / f"{name}.png").read_bytes().startswith(b"\x89PNG\r\n\x1a\n")
        chevron = next(n["visible_rect"] for n in nodes if n.get("name") == "open_tabs_chevron")
        assert chevron["width"] >= 16 * zoom / 100 - .2
        cx, cy = chevron["x"] + chevron["width"] / 2, chevron["y"] + chevron["height"] / 2
        radius = 6 * zoom / 100
        pixels = Image.open(directory / f"{name}.png").convert("RGB").crop((int(cx-radius), int(cy-radius), int(cx+radius), int(cy+radius)))
        assert len(pixels.getcolors(pixels.width * pixels.height)) > 1, (zoom, name, "Invisible open-tabs icon")
    assert positions["first"] == 0
    assert positions["wheel"] < positions["scrolled"] < positions["last"]
    assert positions["same_revealed"] == positions["last_keyboard"]
    for name in ("menu", "menu_last", "menu_scrolled"):
        snapshot = json.loads((directory / f"{name}.json").read_text())
        nodes = [n for n in snapshot["nodes"] if n["rendered"] and not n["hidden"]]
        panel = next(n["rect"] for n in nodes if n.get("name") == "context_menu")
        readers = [n for n in nodes if n.get("name") == "diff_scroll"]
        if readers:
            assert readers[0]["scroll"]["y"] == 0, (zoom, name, "Menu scrolling moved the reader")
        assert panel["y"] >= 0 and panel["y"] + panel["height"] <= 850 + .2
        rows = [n for n in nodes if n.get("name", "").startswith("context_menu_item_")]
        assert rows
        for row in rows:
            rect = row["rect"]
            assert panel["y"] <= rect["y"] and rect["y"] + rect["height"] <= panel["y"] + panel["height"] + .2
        if name == "menu_last":
            assert any(files[-1] in n.get("text", "") for n in rows)
            pixels = Image.open(directory / f"{name}.png").convert("RGB")
            for row in rows:
                rect = row["rect"]
                point = (int(rect["x"] + 6 * zoom / 100), int(rect["y"] + rect["height"] / 2))
                expected = (4, 57, 94) if files[-1] in row.get("text", "") else (45, 45, 45)
                assert pixels.getpixel(point) == expected, (zoom, name, "Menu highlight disagrees with keyboard selection", row.get("name"))
    print(f"PASS {zoom}%: 25 tabs, strip-only scrolling, keyboard/mouse menu, active reveal, narrow resize", flush=True)
assert hashlib.sha256(binary.read_bytes()).hexdigest() == digest
(out / "result.json").write_text(json.dumps(dict(passed=True, binary_sha256=digest, zooms=[100, 140, 200]), indent=2) + "\n")
