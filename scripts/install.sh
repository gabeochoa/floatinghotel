#!/bin/bash
# install.sh - symlink fh to output/floatinghotel
set -e

SCRIPT_DIR="$(cd "$(dirname "$0")" && pwd)"
PROJECT_DIR="$(cd "$SCRIPT_DIR/.." && pwd)"
EXE="$PROJECT_DIR/output/floatinghotel"
LINK="/usr/local/bin/fh"

if [ ! -f "$EXE" ]; then
    echo "Error: $EXE not found. Run 'make' first."
    exit 1
fi

echo "Linking $LINK -> $EXE"
ln -sf "$EXE" "$LINK"
echo "Done. You can now run 'fh' from anywhere."
