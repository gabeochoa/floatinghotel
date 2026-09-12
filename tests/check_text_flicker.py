import re
import sys
from pathlib import Path

from PIL import Image, ImageChops


directory = Path(sys.argv[1])
frames = sorted(directory.glob("idle_*.png"))
log = (directory / "native.log").read_text()
bench = re.search(r"bench_idle_frames: 120 frames, (\d+) rendered, (\d+) skipped", log)
assert bench, "Missing idle benchmark results"
rendered, skipped = map(int, bench.groups())
assert rendered + skipped == 120 and skipped >= 80, "Idle redraw skipping was not exercised"
assert len(frames) == rendered, f"Expected {rendered} rendered frames, got {len(frames)}"
reference = Image.open(directory / "text_flicker_ready.png").convert("RGB")
regions = {
    "menu": (0, 28, 280, 54),
    "sidebar": (0, 54, 280, 158),
    "code": (320, 226, 650, 337),
}
for name, bounds in regions.items():
    expected = reference.crop(bounds)
    foreground = sum(expected.convert("L").histogram()[101:])
    assert foreground > 100, f"Reference {name} has no readable text"
    for path in frames:
        actual = Image.open(path).convert("RGB").crop(bounds)
        assert ImageChops.difference(expected, actual).getbbox() is None, (
            f"{name} changed in {path.name} while idle"
        )
counts = [int(value) for value in re.findall(r"idle_capture frame=\d+ commands=(\d+)", log)]
assert len(counts) == len(frames), "Missing per-frame draw counts"
assert min(counts) > 0, "No UI commands rendered"
assert max(counts) == min(counts), f"Idle redraw accumulated UI commands: {counts}"
print(f"PASS: {len(frames)} rendered frames preserve text and draw {counts[0]} UI commands each")
