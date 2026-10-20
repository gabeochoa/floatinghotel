#!/bin/bash
# Wrap output/floatinghotel.exe + resources into output/FloatingHotel.app.
# MACOS_MIN (optional) sets LSMinimumSystemVersion; defaults to this machine's version.
set -e
cd "$(dirname "$0")/.."
APP=output/FloatingHotel.app
mkdir -p "$APP/Contents/MacOS" "$APP/Contents/Resources"
cp output/floatinghotel.exe "$APP/Contents/MacOS/floatinghotel"
rsync -a --delete output/resources/ "$APP/Contents/Resources/"
cat > "$APP/Contents/Info.plist" <<PLIST
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
    <key>CFBundleExecutable</key>
    <string>floatinghotel</string>
    <key>CFBundleIdentifier</key>
    <string>com.floatinghotel.app</string>
    <key>CFBundleName</key>
    <string>FloatingHotel</string>
    <key>CFBundleIconFile</key>
    <string>AppIcon</string>
    <key>CFBundleVersion</key>
    <string>1.0</string>
    <key>CFBundlePackageType</key>
    <string>APPL</string>
    <key>NSHighResolutionCapable</key>
    <true/>
    <key>LSMinimumSystemVersion</key>
    <string>${MACOS_MIN:-$(sw_vers -productVersion)}</string>
</dict>
</plist>
PLIST
# The linker signed the bare executable; resources added since make that
# signature invalid and Gatekeeper refuses the app. Re-sign the bundle
# (ad hoc; distribution needs a Developer ID and notarization).
codesign --force --sign - "$APP"
echo "Built $APP"
