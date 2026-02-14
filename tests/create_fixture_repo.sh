#!/bin/bash
# Creates a self-contained fixture git repository for E2E testing.
# Produces a repo with: staged files, unstaged files, untracked files,
# multiple commits, multiple branches, file additions, deletions, and modifications.
#
# This script is idempotent: it deletes any existing fixture and rebuilds from scratch.

set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
FIXTURE_DIR="$SCRIPT_DIR/fixture_repo"

echo "Creating fixture repo at $FIXTURE_DIR ..."

rm -rf "$FIXTURE_DIR"
mkdir -p "$FIXTURE_DIR"
cd "$FIXTURE_DIR"

git init
git checkout -b main

# Configure local identity so commits work in any environment
git config user.email "test@floatinghotel.dev"
git config user.name "Fixture Bot"

# ============================================================
# Commit 1 — Initial commit with four files
# ============================================================

cat > README.md << 'READMEEOF'
# Weather Station

A small CLI tool that reads sensor data and prints weather summaries.

## Usage

```bash
python src/main.py --station KORD
```

## Configuration

Copy `config.json.example` to `config.json` and fill in your API key.
READMEEOF

mkdir -p src

cat > src/main.py << 'PYEOF'
"""Weather Station CLI — entry point."""

import argparse
import sys
from utils import load_config, fetch_readings


def parse_args():
    parser = argparse.ArgumentParser(description="Weather Station CLI")
    parser.add_argument("--station", required=True, help="ICAO station code")
    parser.add_argument("--units", default="metric", choices=["metric", "imperial"])
    return parser.parse_args()


def main():
    args = parse_args()
    config = load_config("config.json")
    readings = fetch_readings(config["api_key"], args.station)

    for r in readings:
        temp = r["temp_c"] if args.units == "metric" else r["temp_c"] * 9 / 5 + 32
        unit = "C" if args.units == "metric" else "F"
        print(f"{r['timestamp']}  {temp:.1f}°{unit}  {r['humidity']}% RH")


if __name__ == "__main__":
    main()
PYEOF

cat > src/utils.py << 'PYEOF'
"""Utility helpers for Weather Station."""

import json
import urllib.request


def load_config(path: str) -> dict:
    """Load JSON configuration from *path*."""
    with open(path) as fh:
        return json.load(fh)


def fetch_readings(api_key: str, station: str) -> list[dict]:
    """Return recent weather readings for *station*."""
    url = f"https://api.weather.example.com/v1/readings?station={station}"
    req = urllib.request.Request(url, headers={"Authorization": f"Bearer {api_key}"})
    with urllib.request.urlopen(req) as resp:
        return json.loads(resp.read())
PYEOF

cat > config.json << 'JSONEOF'
{
    "api_key": "sk-test-0000000000000000",
    "default_station": "KORD",
    "units": "metric",
    "cache_ttl_seconds": 300
}
JSONEOF

git add .
git commit -m "Initial commit

Add Weather Station CLI with main entry point, utility helpers,
and default configuration."

# ============================================================
# Commit 2 — Add a function to main.py, add helpers.py
# ============================================================

cat > src/main.py << 'PYEOF'
"""Weather Station CLI — entry point."""

import argparse
import sys
from utils import load_config, fetch_readings
from helpers import format_reading


def parse_args():
    parser = argparse.ArgumentParser(description="Weather Station CLI")
    parser.add_argument("--station", required=True, help="ICAO station code")
    parser.add_argument("--units", default="metric", choices=["metric", "imperial"])
    parser.add_argument("--json", action="store_true", help="Output as JSON")
    return parser.parse_args()


def summarize(readings, units="metric"):
    """Return a short plain-text summary of *readings*."""
    if not readings:
        return "No readings available."
    temps = [r["temp_c"] for r in readings]
    avg = sum(temps) / len(temps)
    if units == "imperial":
        avg = avg * 9 / 5 + 32
    unit = "C" if units == "metric" else "F"
    return f"Average: {avg:.1f}°{unit} over {len(readings)} readings"


def main():
    args = parse_args()
    config = load_config("config.json")
    readings = fetch_readings(config["api_key"], args.station)

    for r in readings:
        print(format_reading(r, args.units))

    print()
    print(summarize(readings, args.units))


if __name__ == "__main__":
    main()
PYEOF

cat > src/helpers.py << 'PYEOF'
"""Formatting helpers for Weather Station."""


def format_reading(reading: dict, units: str = "metric") -> str:
    """Format a single weather reading for display."""
    temp = reading["temp_c"]
    if units == "imperial":
        temp = temp * 9 / 5 + 32
    unit = "C" if units == "metric" else "F"
    return f"{reading['timestamp']}  {temp:.1f}°{unit}  {reading['humidity']}% RH"


def format_wind(speed_kph: float, direction: str, units: str = "metric") -> str:
    """Format wind information."""
    if units == "imperial":
        speed = speed_kph * 0.621371
        unit = "mph"
    else:
        speed = speed_kph
        unit = "km/h"
    return f"{direction} {speed:.0f} {unit}"
PYEOF

git add .
git commit -m "Add summarize function and formatting helpers

- main.py: add summarize() for average temperature, add --json flag
- helpers.py: new module with format_reading() and format_wind()"

# ============================================================
# Commit 3 — Delete config.json, update README
# ============================================================

git rm config.json

cat > README.md << 'READMEEOF'
# Weather Station

A small CLI tool that reads sensor data and prints weather summaries.

## Usage

```bash
python src/main.py --station KORD
python src/main.py --station EGLL --units imperial --json
```

## Configuration

Create a `config.json` in the project root with the following shape:

```json
{
    "api_key": "YOUR_API_KEY",
    "default_station": "KORD",
    "units": "metric",
    "cache_ttl_seconds": 300
}
```

You can obtain an API key at <https://weather.example.com/keys>.

## License

MIT
READMEEOF

git add .
git commit -m "Remove checked-in config, improve README

- Delete config.json (secrets should not be committed)
- Add setup instructions and license note to README"

# ============================================================
# Branch: feature/add-logging — one commit
# ============================================================

git checkout -b feature/add-logging

cat > src/logging.py << 'PYEOF'
"""Simple structured logger for Weather Station."""

import datetime
import json
import sys

LEVELS = {"DEBUG": 10, "INFO": 20, "WARNING": 30, "ERROR": 40}


class Logger:
    def __init__(self, name: str, level: str = "INFO", stream=None):
        self.name = name
        self.level = LEVELS.get(level, 20)
        self.stream = stream or sys.stderr

    def _emit(self, level: str, message: str, **extra):
        if LEVELS.get(level, 0) < self.level:
            return
        record = {
            "ts": datetime.datetime.utcnow().isoformat() + "Z",
            "logger": self.name,
            "level": level,
            "msg": message,
            **extra,
        }
        self.stream.write(json.dumps(record) + "\n")

    def debug(self, msg, **kw):
        self._emit("DEBUG", msg, **kw)

    def info(self, msg, **kw):
        self._emit("INFO", msg, **kw)

    def warning(self, msg, **kw):
        self._emit("WARNING", msg, **kw)

    def error(self, msg, **kw):
        self._emit("ERROR", msg, **kw)
PYEOF

# Modify utils.py on the branch to use the logger
cat > src/utils.py << 'PYEOF'
"""Utility helpers for Weather Station."""

import json
import urllib.request
from logging import Logger

log = Logger("utils")


def load_config(path: str) -> dict:
    """Load JSON configuration from *path*."""
    log.info("Loading config", path=path)
    with open(path) as fh:
        return json.load(fh)


def fetch_readings(api_key: str, station: str) -> list[dict]:
    """Return recent weather readings for *station*."""
    url = f"https://api.weather.example.com/v1/readings?station={station}"
    log.info("Fetching readings", station=station, url=url)
    req = urllib.request.Request(url, headers={"Authorization": f"Bearer {api_key}"})
    try:
        with urllib.request.urlopen(req) as resp:
            data = json.loads(resp.read())
            log.info("Received readings", count=len(data))
            return data
    except urllib.error.URLError as exc:
        log.error("Request failed", station=station, error=str(exc))
        raise
PYEOF

git add .
git commit -m "Add structured logging module

- logging.py: JSON-structured logger with level filtering
- utils.py: instrument load_config and fetch_readings with log calls"

# ============================================================
# Switch back to main and leave working-tree dirt
# ============================================================

git checkout main

# --- Unstaged modification to src/main.py ---
cat > src/main.py << 'PYEOF'
"""Weather Station CLI — entry point."""

import argparse
import json
import sys
from utils import load_config, fetch_readings
from helpers import format_reading


def parse_args():
    parser = argparse.ArgumentParser(description="Weather Station CLI")
    parser.add_argument("--station", required=True, help="ICAO station code")
    parser.add_argument("--units", default="metric", choices=["metric", "imperial"])
    parser.add_argument("--json", action="store_true", help="Output as JSON")
    return parser.parse_args()


def summarize(readings, units="metric"):
    """Return a short plain-text summary of *readings*."""
    if not readings:
        return "No readings available."
    temps = [r["temp_c"] for r in readings]
    avg = sum(temps) / len(temps)
    if units == "imperial":
        avg = avg * 9 / 5 + 32
    unit = "C" if units == "metric" else "F"
    return f"Average: {avg:.1f}°{unit} over {len(readings)} readings"


def main():
    args = parse_args()
    config = load_config("config.json")
    readings = fetch_readings(config["api_key"], args.station)

    if args.json:
        print(json.dumps(readings, indent=2))
    else:
        for r in readings:
            print(format_reading(r, args.units))

    print()
    print(summarize(readings, args.units))


if __name__ == "__main__":
    main()
PYEOF

# --- Staged modification to src/utils.py ---
cat > src/utils.py << 'PYEOF'
"""Utility helpers for Weather Station."""

import json
import urllib.request


def load_config(path: str) -> dict:
    """Load JSON configuration from *path*."""
    with open(path) as fh:
        data = json.load(fh)
    # Validate required keys
    for key in ("api_key", "default_station"):
        if key not in data:
            raise KeyError(f"Missing required config key: {key}")
    return data


def fetch_readings(api_key: str, station: str) -> list[dict]:
    """Return recent weather readings for *station*."""
    url = f"https://api.weather.example.com/v1/readings?station={station}"
    req = urllib.request.Request(url, headers={"Authorization": f"Bearer {api_key}"})
    with urllib.request.urlopen(req) as resp:
        return json.loads(resp.read())
PYEOF

git add src/utils.py

# --- Untracked file ---
cat > TODO.txt << 'TODOEOF'
- [ ] Add --output flag to write results to a file
- [ ] Support multiple stations in one invocation
- [ ] Add retry logic to fetch_readings
- [ ] Write unit tests for summarize()
TODOEOF

echo ""
echo "Fixture repo created successfully at $FIXTURE_DIR"
echo ""
echo "State summary:"
echo "  Commits on main:              3"
echo "  Branches:                     main, feature/add-logging"
echo "  Staged files:                 src/utils.py (modified)"
echo "  Unstaged modified files:      src/main.py"
echo "  Untracked files:              TODO.txt"
