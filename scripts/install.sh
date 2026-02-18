#!/bin/bash
# install.sh - install floatinghotel so it can run from anywhere
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
EXE="$PROJECT_DIR/output/floatinghotel.exe"
RES_SRC="$PROJECT_DIR/output/resources"
LINK="$HOME/.local/bin/fh"

if [ ! -f "$EXE" ]; then
    echo "Error: $EXE not found. Run 'make output' first."
    exit 1
fi

if [ ! -d "$RES_SRC" ]; then
    echo "Error: $RES_SRC not found. Run 'make output' to copy resources."
    exit 1
fi

mkdir -p "$(dirname "$LINK")"
echo "Linking $LINK -> $EXE"
ln -sf "$EXE" "$LINK"
echo "Done. You can now run 'fh <repo-path>' from anywhere."
echo "Resources are loaded from: $RES_SRC"
